/*
 * kolibri_swarm_node.c — v2
 * Minimal standalone swarm node with working HTTP body parsing
 * Single-file compile: gcc -O2 -o kolibri_swarm kolibri_swarm_node.c -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#define PORT 8001
#define MAX_REQ 16384
#define MAX_RESP 16384
#define MAX_QA 30000

/* Globals */
static char qa_q[MAX_QA][512];
static char qa_a[MAX_QA][1024];
static int qa_count = 0;
static int total_facts = 0;

/* Peer list for swarm */
typedef struct { char host[256]; int port; } Peer;
static Peer peers[32];
static int peer_count = 0;

/* ─── Knowledge loader ─── */
static int load_knowledge(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    
    char line[2048], cur_q[1024] = {0}, cur_a[1024] = {0};
    int in_answer = 0;
    
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "### Q", 5) == 0) {
            if (cur_q[0] && cur_a[0] && qa_count < MAX_QA) {
                strncpy(qa_q[qa_count], cur_q, 511);
                strncpy(qa_a[qa_count], cur_a, 1023);
                qa_count++;
            }
            char *colon = strchr(line, ':');
            if (colon) {
                strncpy(cur_q, colon + 2, sizeof(cur_q) - 1);
                cur_q[strcspn(cur_q, "\r\n")] = 0;
                cur_a[0] = 0;
                in_answer = 0;
                total_facts++;
            }
        } else if (strncmp(line, "**Ответ:**", 15) == 0) {
            const char *p = line + 15;
            while (*p == ' ' || *p == '\t') p++;  /* Skip leading spaces */
            strncpy(cur_a, p, sizeof(cur_a) - 1);
            cur_a[strcspn(cur_a, "\r\n")] = 0;
            in_answer = 1;
        } else if (in_answer && line[0] != '#' && line[0] != '-' &&
                   strncmp(line, "---", 3) != 0 && strlen(line) > 2) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            size_t len = strlen(p);
            if (len > 2 && len < 500 && strlen(cur_a) + len < 1020) {
                /* Strip trailing newline/spaces */
                while (len > 0 && (p[len-1] == '\n' || p[len-1] == '\r' || p[len-1] == ' ')) len--;
                if (len > 0) {
                    strncat(cur_a, " ", sizeof(cur_a) - strlen(cur_a) - 1);
                    strncat(cur_a, p, sizeof(cur_a) - strlen(cur_a) - 1);
                    cur_a[strcspn(cur_a, "\r\n")] = 0;
                }
            }
        } else if (strncmp(line, "---", 3) == 0) {
            if (cur_q[0] && cur_a[0] && qa_count < MAX_QA) {
                strncpy(qa_q[qa_count], cur_q, 511);
                strncpy(qa_a[qa_count], cur_a, 1023);
                qa_count++;
            }
            cur_q[0] = cur_a[0] = 0;
            in_answer = 0;
        }
    }
    
    if (cur_q[0] && cur_a[0] && qa_count < MAX_QA) {
        strncpy(qa_q[qa_count], cur_q, 511);
        strncpy(qa_a[qa_count], cur_a, 1023);
        qa_count++;
    }
    
    fclose(f);
    return qa_count;
}

/* ─── Keyword search ─── */
static const char* find_answer(const char *query) {
    if (!query || !query[0]) return NULL;
    
    /* Tokenize query */
    char tokens[64][64];
    int ntokens = 0;
    char qbuf[1024];
    strncpy(qbuf, query, sizeof(qbuf) - 1);
    char *tok = strtok(qbuf, " ,.!?;:—–()\n\t");
    while (tok && ntokens < 64) {
        if (strlen(tok) > 2) {
            strncpy(tokens[ntokens], tok, 63);
            ntokens++;
        }
        tok = strtok(NULL, " ,.!?;:—–()\n\t");
    }
    if (ntokens == 0) return NULL;
    
    /* Score each Q&A */
    int best = 0, best_idx = -1;
    for (int i = 0; i < qa_count; i++) {
        int score_q = 0, score_a = 0;
        for (int t = 0; t < ntokens; t++) {
            if (strstr(qa_q[i], tokens[t])) score_q += 10;  /* High weight for question match */
            if (strstr(qa_a[i], tokens[t])) score_a += 1;   /* Low weight for answer match */
        }
        /* Prefer question matches over answer-only matches */
        int total = score_q * 3 + score_a;
        if (total > best) { best = total; best_idx = i; }
    }
    
    /* Require at least one question token to match */
    if (best_idx >= 0) {
        int q_match = 0;
        for (int t = 0; t < ntokens; t++) {
            if (strstr(qa_q[best_idx], tokens[t])) { q_match = 1; break; }
        }
        if (q_match || best > 20) return qa_a[best_idx];
    }
    return NULL;
}

/* ─── HTTP helpers ─── */
static void send_json(int fd, int status, const char *body) {
    char hdr[512];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d OK\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\nAccess-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n\r\n",
        status, strlen(body));
    write(fd, hdr, hlen);
    if (body[0]) write(fd, body, strlen(body));
}

/* ─── Parse JSON string value (FIXED) ─── */
static void json_get_str(const char *json, const char *key, char *out, int max) {
    out[0] = 0;
    if (!json) return;
    
    /* Build search pattern: "key":" */
    char pat[128];
    int pat_len = 0;
    pat[pat_len++] = '"';
    for (int i = 0; key[i] && pat_len < 120; i++) pat[pat_len++] = key[i];
    pat[pat_len++] = '"';
    pat[pat_len++] = ':';
    pat[pat_len++] = '"';
    pat[pat_len] = 0;
    
    const char *p = strstr(json, pat);
    if (!p) return;
    
    p += pat_len;
    int i = 0;
    while (*p && *p != '"' && i < max - 1) {
        /* Handle escape sequences */
        if (*p == '\\' && *(p+1)) {
            p++;
            switch (*p) {
                case '"': case '\\': case '/': out[i++] = *p; break;
                case 'n': out[i++] = '\n'; break;
                case 't': out[i++] = '\t'; break;
                case 'r': out[i++] = '\r'; break;
                default: out[i++] = *p; break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = 0;
}

static void json_escape(char *out, const char *in, int max) {
    int j = 0;
    int len = strlen(in);
    for (int i = 0; i < len && j < max - 3; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"') { out[j++] = '\\'; out[j++] = '"'; }
        else if (c == '\\') { out[j++] = '\\'; out[j++] = '\\'; }
        else if (c == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
        else if (c == '\t') { out[j++] = '\\'; out[j++] = 't'; }
        else if (c == '\r') { out[j++] = '\\'; out[j++] = 'r'; }
        else if (c < 0x20) { /* skip control chars */ }
        else { out[j++] = c; }  /* Copy byte as-is, including UTF-8 */
    }
    out[j] = 0;
}

/* ─── Read full HTTP body respecting Content-Length ─── */
static int read_full_body(int fd, const char *headers, int headers_len,
                          char *body, int max_body) {
    /* Find Content-Length */
    const char *cl = NULL;
    const char *p = headers;
    while (p < headers + headers_len - 16) {
        if (strncmp(p, "Content-Length: ", 16) == 0) {
            cl = p + 16;
            break;
        }
        p++;
    }
    
    int content_len = cl ? atoi(cl) : 0;
    
    /* Body starts after \r\n\r\n */
    const char *body_start = headers;
    int already = 0;
    for (int i = 0; i < headers_len - 3; i++) {
        if (headers[i] == '\r' && headers[i+1] == '\n' &&
            headers[i+2] == '\r' && headers[i+3] == '\n') {
            body_start = headers + i + 4;
            already = headers_len - (i + 4);
            break;
        }
    }
    
    if (already > 0 && already < max_body) {
        memcpy(body, body_start, already);
    }
    
    /* Read remaining bytes */
    while (already < content_len && already < max_body - 1) {
        int r = read(fd, body + already, content_len - already);
        if (r <= 0) break;
        already += r;
    }
    body[already < max_body ? already : max_body - 1] = 0;
    return already;
}

/* ─── Knowledge sync from peer ─── */
static int sync_from_peer(const char *host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    
    /* Request all Q&A from peer */
    const char *req = "GET /api/v1/swarm/export HTTP/1.1\r\nHost: peer\r\n\r\n";
    write(fd, req, strlen(req));
    
    /* Read response */
    char resp[65536];
    int total = 0;
    while (total < (int)sizeof(resp) - 1) {
        int r = read(fd, resp + total, sizeof(resp) - 1 - total);
        if (r <= 0) break;
        total += r;
    }
    resp[total] = 0;
    close(fd);
    
    /* Find body after \r\n\r\n */
    char *body = strstr(resp, "\r\n\r\n");
    if (!body) return 0;
    body += 4;
    
    /* Parse NDJSON: one JSON object per line */
    int added = 0;
    char *line = strtok(body, "\n");
    while (line && qa_count < MAX_QA - 100) {
        char q[512] = {0}, a[1024] = {0};
        json_get_str(line, "q", q, sizeof(q));
        json_get_str(line, "a", a, sizeof(a));
        if (q[0] && a[0]) {
            /* Check for duplicate */
            int dup = 0;
            for (int i = 0; i < qa_count; i++) {
                if (strcmp(qa_q[i], q) == 0) { dup = 1; break; }
            }
            if (!dup) {
                strncpy(qa_q[qa_count], q, 511);
                strncpy(qa_a[qa_count], a, 1023);
                qa_count++;
                total_facts++;
                added++;
            }
        }
        line = strtok(NULL, "\n");
    }
    
    return added;
}

/* ─── Main ─── */
int main(int argc, char *argv[]) {
    int port = PORT;
    if (argc > 1) port = atoi(argv[1]);
    
    /* Parse peer arguments: --peer host:port */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--peer") == 0 && i + 1 < argc) {
            i++;
            char *colon = strchr(argv[i], ':');
            if (colon) {
                *colon = 0;
                strncpy(peers[peer_count].host, argv[i], 255);
                peers[peer_count].port = atoi(colon + 1);
                peer_count++;
                printf("  Peer: %s:%d\n", peers[peer_count-1].host, peers[peer_count-1].port);
            }
        }
    }
    
    printf("🐦 Kolibri Swarm Node v2\n");
    printf("  Port: %d\n", port);
    printf("  Peers: %d\n", peer_count);
    
    /* Load knowledge */
    const char *paths[] = {
        "knowledge/knowledge_base.md",
        "/opt/kp/knowledge/knowledge_base.md",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        if (load_knowledge(paths[i]) > 0) {
            printf("  ✅ Loaded %d Q&A from %s\n", qa_count, paths[i]);
            break;
        }
    }
    if (qa_count == 0) printf("  ⚠️  No knowledge loaded\n");
    
    /* Sync from peers */
    for (int i = 0; i < peer_count; i++) {
        printf("  🔄 Syncing from %s:%d... ", peers[i].host, peers[i].port);
        int added = sync_from_peer(peers[i].host, peers[i].port);
        if (added > 0) {
            printf("✅ +%d facts (total: %d)\n", added, qa_count);
        } else {
            printf("⚠️  no new facts\n");
        }
    }
    
    printf("\n📊 Total facts: %d\n", qa_count);
    printf("🌐 http://0.0.0.0:%d\n\n", port);
    fflush(stdout);
    
    /* Create socket */
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }
    
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    listen(srv, 32);
    
    char buf[MAX_REQ];
    int req_count = 0;
    
    while (1) {
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);
        int fd = accept(srv, (struct sockaddr*)&cli, &clen);
        if (fd < 0) continue;
        
        /* Read headers first */
        int n = read(fd, buf, sizeof(buf) - 1);
        if (n <= 0) { close(fd); continue; }
        buf[n] = 0;
        
        /* Parse method */
        char *sp = strchr(buf, ' ');
        if (!sp) { close(fd); continue; }
        *sp = 0;
        const char *method = buf;
        
        /* Parse path */
        char *path = sp + 1;
        char *pe = strchr(path, ' ');
        if (pe) *pe = 0;
        pe = strchr(path, '\r');
        if (pe) *pe = 0;
        
        /* Read full body */
        char body[8192] = {0};
        read_full_body(fd, buf, n, body, sizeof(body));
        
        req_count++;
        printf("  %s %s\n", method, path);
        fflush(stdout);
        
        /* ─── Routes ─── */
        if (strcmp(method, "OPTIONS") == 0) {
            send_json(fd, 200, "");
        }
        else if (strcmp(path, "/api/v1/health") == 0) {
            char resp[512];
            snprintf(resp, sizeof(resp),
                "{\"status\":\"ok\",\"facts\":%d,\"peers\":%d,\"requests\":%d}",
                qa_count, peer_count, req_count);
            send_json(fd, 200, resp);
        }
        else if (strcmp(path, "/api/v1/swarm/peers") == 0) {
            char resp[2048];
            int off = snprintf(resp, sizeof(resp), "{\"peers\":[");
            for (int i = 0; i < peer_count; i++) {
                off += snprintf(resp + off, sizeof(resp) - off,
                    "%s{\"host\":\"%s\",\"port\":%d}",
                    i > 0 ? "," : "", peers[i].host, peers[i].port);
            }
            snprintf(resp + off, sizeof(resp) - off, "],\"count\":%d}", peer_count);
            send_json(fd, 200, resp);
        }
        else if (strcmp(path, "/api/v1/swarm/sync") == 0 && strcmp(method, "POST") == 0) {
            /* Add new peer from request body */
            char host[256] = {0};
            int p = 0;
            json_get_str(body, "host", host, sizeof(host));
            char port_str[16] = {0};
            json_get_str(body, "port", port_str, sizeof(port_str));
            if (port_str[0]) p = atoi(port_str);
            
            if (host[0] && p > 0 && peer_count < 32) {
                /* Check if already exists */
                int exists = 0;
                for (int i = 0; i < peer_count; i++) {
                    if (strcmp(peers[i].host, host) == 0 && peers[i].port == p) {
                        exists = 1; break;
                    }
                }
                if (!exists) {
                    strncpy(peers[peer_count].host, host, 255);
                    peers[peer_count].port = p;
                    peer_count++;
                    
                    /* Sync from new peer */
                    int added = sync_from_peer(host, p);
                    char resp[512];
                    snprintf(resp, sizeof(resp),
                        "{\"status\":\"peer_added\",\"total_peers\":%d,\"facts_added\":%d,\"total_facts\":%d}",
                        peer_count, added, qa_count);
                    send_json(fd, 200, resp);
                } else {
                    send_json(fd, 200, "{\"status\":\"already_peer\"}");
                }
            } else {
                send_json(fd, 400, "{\"error\":\"missing host/port\"}");
            }
        }
        else if (strcmp(path, "/api/v1/swarm/export") == 0) {
            /* Export all Q&A as NDJSON */
            /* Send headers first */
            char hdr[256];
            /* We don't know exact size, so use chunked or approximate */
            int estimated = qa_count * 800;
            snprintf(hdr, sizeof(hdr),
                "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\n"
                "Content-Length: %d\r\n\r\n", estimated);
            write(fd, hdr, strlen(hdr));
            
            for (int i = 0; i < qa_count; i++) {
                char qsafe[1024] = {0}, asafe[2048] = {0};
                json_escape(qsafe, qa_q[i], sizeof(qsafe));
                json_escape(asafe, qa_a[i], sizeof(asafe));
                char line[4096];
                int len = snprintf(line, sizeof(line),
                    "{\"q\":\"%s\",\"a\":\"%s\"}\n", qsafe, asafe);
                write(fd, line, len);
            }
        }
        else if (strncmp(path, "/api/v1/ai/chat", 15) == 0 && strcmp(method, "POST") == 0) {
            char message[2048] = {0}, conv[256] = {0};
            json_get_str(body, "message", message, sizeof(message));
            json_get_str(body, "conversation_id", conv, sizeof(conv));
            
            const char *ans = find_answer(message);
            char safe[4096] = {0};

            if (ans) {
                json_escape(safe, ans, sizeof(safe));
            } else {
                char msg_escaped[2048] = {0};
                json_escape(msg_escaped, message, sizeof(msg_escaped));
                snprintf(safe, sizeof(safe),
                    "Нет точного ответа на %s. Доступно %d фактов. "
                    "Попробуйте вопрос из математики, физики, химии, IT, географии или истории.",
                    msg_escaped, qa_count);
            }
            
            char resp[MAX_RESP];
            snprintf(resp, sizeof(resp),
                "{\"response\":\"%s\",\"conversation_id\":\"%s\","
                "\"method\":\"knowledge_base\",\"confidence\":%s,\"duration_ms\":0}",
                safe, conv[0] ? conv : "default",
                ans ? "0.9" : "0.3");
            send_json(fd, 200, resp);
        }
        else if (strncmp(path, "/api/", 5) == 0) {
            send_json(fd, 200, "{}");
        }
        else {
            send_json(fd, 404, "{\"error\":\"not found\"}");
        }
        
        close(fd);
    }
    
    close(srv);
    return 0;
}
