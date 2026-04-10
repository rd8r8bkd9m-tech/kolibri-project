/*
 * kolibri_swarm_node.c — Kolibri Swarm Node with Knowledge Base
 * 
 * Loads Q&A knowledge base and answers questions using keyword scoring.
 * Supports up to 2000 Q&A pairs with efficient keyword matching.
 *
 * Compile: gcc -O2 -o kolibri_swarm kolibri_swarm_node.c -lm
 * Run:     ./kolibri_swarm 8002 --peer 217.60.249.157:8001
 *
 * Knowledge file: knowledge/knowledge_base.md
 * Format: ### Q: question\n\n**Ответ:** answer\n\n---
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <time.h>

#define MAX_KB 2000
#define MAX_Q_LEN 512
#define MAX_A_LEN 2048
#define PORT_DEFAULT 8002
#define MAX_PEERS 8
#define MAX_PEERS_STR 256
#define BUF_SIZE 65536

typedef struct {
    char question[MAX_Q_LEN];
    char answer[MAX_A_LEN];
    char keywords[256];  /* space-separated lowercase keywords */
} QAPair;

static QAPair knowledge_base[MAX_KB];
static int kb_count = 0;

static char peer_hosts[MAX_PEERS][MAX_PEERS_STR];
static int peer_count = 0;

/* ─── Knowledge Base Loading ─── */
static void to_lower(char *dst, const char *src, int max) {
    int i;
    for (i = 0; i < max - 1 && src[i]; i++) {
        unsigned char c = (unsigned char)src[i];
        /* Simple ASCII lowercase */
        if (c >= 'A' && c <= 'Z') c += 32;
        dst[i] = c;
    }
    dst[i] = 0;
}

static void extract_keywords(char *out, const char *text, int max) {
    char lower[1024];
    to_lower(lower, text, sizeof(lower));
    
    int out_pos = 0;
    char *tok = strtok(lower, " ,.!?;:—–()\n\t\"*");
    while (tok && out_pos < max - 20) {
        int len = strlen(tok);
        if (len >= 3) {
            if (out_pos > 0) {
                out[out_pos++] = ' ';
            }
            int copy = len < 30 ? len : 30;
            memcpy(out + out_pos, tok, copy);
            out_pos += copy;
            out[out_pos] = 0;
        }
        tok = strtok(NULL, " ,.!?;:—–()\n\t\"*");
    }
}

static int load_knowledge_base(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "⚠️  Cannot open %s\n", path);
        return 0;
    }
    
    char line[4096];
    int state = 0; /* 0=expect Q, 1=expect answer, 2=expect --- */
    int loaded = 0;
    
    while (fgets(line, sizeof(line), f) && kb_count < MAX_KB) {
        /* Remove trailing newline */
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) len--;
        line[len] = 0;
        
        /* Skip empty lines */
        if (len == 0) continue;
        
        /* Detect question line: ### Q... */
        if (strncmp(line, "### Q", 5) == 0) {
            if (kb_count < MAX_KB) {
                char *colon = strchr(line + 5, ':');
                if (colon) colon++;
                else colon = line + 5;
                while (*colon == ' ') colon++;
                
                /* Clean and add question mark */
                int qlen = strlen(colon);
                if (qlen > 0 && colon[qlen-1] != '?') {
                    colon[qlen] = '?';
                    colon[qlen+1] = 0;
                    qlen++;
                }
                if (qlen >= MAX_Q_LEN) qlen = MAX_Q_LEN - 1;
                memcpy(knowledge_base[kb_count].question, colon, qlen);
                knowledge_base[kb_count].question[qlen] = 0;
                
                /* Extract keywords */
                extract_keywords(knowledge_base[kb_count].keywords, colon, sizeof(knowledge_base[kb_count].keywords));
                
                knowledge_base[kb_count].answer[0] = 0;
                state = 1;
            }
        }
        /* Detect separator */
        else if (strncmp(line, "---", 3) == 0) {
            if (state >= 1 && knowledge_base[kb_count].answer[0]) {
                kb_count++;
                loaded++;
            }
            state = 0;
        }
        /* Answer line (between ### Q and ---) */
        else if (state == 1 && kb_count < MAX_KB) {
            int alen = len;
            if (alen >= MAX_A_LEN) alen = MAX_A_LEN - 1;
            memcpy(knowledge_base[kb_count].answer, line, alen);
            knowledge_base[kb_count].answer[alen] = 0;
            state = 2;
        }
    }
    
    fclose(f);
    return loaded;
}

/* ─── Answer Finding ─── */
static int find_best_answer(const char *question, char *answer, int max_ans) {
    char q_lower[1024];
    to_lower(q_lower, question, sizeof(q_lower));
    
    /* Extract query keywords */
    char q_keywords[256];
    extract_keywords(q_keywords, question, sizeof(q_keywords));
    
    int best_score = 0;
    int best_idx = -1;
    
    for (int i = 0; i < kb_count; i++) {
        int score = 0;
        
        /* Exact match bonus */
        if (strstr(q_lower, knowledge_base[i].question) || 
            strstr(knowledge_base[i].question, q_lower)) {
            score += 100;
        }
        
        /* Keyword matching */
        char *kb_kw = knowledge_base[i].keywords;
        char kw_copy[256];
        strncpy(kw_copy, kb_kw, sizeof(kw_copy) - 1);
        kw_copy[sizeof(kw_copy) - 1] = 0;
        
        char *kw = strtok(kw_copy, " ");
        while (kw) {
            if (strstr(q_lower, kw)) {
                score += 10;
            }
            kw = strtok(NULL, " ");
        }
        
        /* Check answer contains query keywords */
        char a_lower[MAX_A_LEN];
        to_lower(a_lower, knowledge_base[i].answer, sizeof(a_lower));
        kw = strtok(q_keywords, " ");
        while (kw) {
            if (strstr(a_lower, kw)) {
                score += 5;
            }
            kw = strtok(NULL, " ");
        }
        
        /* Number matching */
        if (isdigit((unsigned char)question[0])) {
            /* Query starts with number, check if answer contains it */
            if (strstr(knowledge_base[i].answer, question) ||
                strstr(knowledge_base[i].answer, q_lower)) {
                score += 50;
            }
        }
        
        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }
    
    if (best_idx >= 0 && best_score >= 10) {
        strncpy(answer, knowledge_base[best_idx].answer, max_ans - 1);
        answer[max_ans - 1] = 0;
        return best_score;
    }
    
    return 0;
}

/* ─── HTTP Server ─── */
static void send_response(int fd, int status, const char *ctype, const char *body) {
    char hdr[512];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\n\r\n",
        status, ctype, strlen(body));
    write(fd, hdr, hlen);
    if (body && body[0]) write(fd, body, strlen(body));
}

static void send_json(int fd, const char *json) {
    send_response(fd, 200, "application/json", json);
}

static void get_json_str(const char *json, const char *key, char *out, int max) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *p = strstr(json, search);
    if (!p) { out[0] = 0; return; }
    p += strlen(search);
    int i = 0;
    while (*p && *p != '"' && i < max - 1) out[i++] = *p++;
    out[i] = 0;
}

static void json_escape(char *out, const char *in, int max) {
    int j = 0;
    for (int i = 0; in[i] && j < max - 3; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"') { out[j++] = '\\'; out[j++] = '"'; }
        else if (c == '\\') { out[j++] = '\\'; out[j++] = '\\'; }
        else if (c == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
        else if (c == '\t') { out[j++] = '\\'; out[j++] = 't'; }
        else if (c < 0x20) { /* skip */ }
        else { out[j++] = c; }
    }
    out[j] = 0;
}

static void handle_chat(int fd, const char *body) {
    char message[2048] = {0}, conv[256] = {0};
    get_json_str(body, "message", message, sizeof(message));
    get_json_str(body, "conversation_id", conv, sizeof(conv));
    if (!conv[0]) strcpy(conv, "default");
    
    char answer[MAX_A_LEN] = {0};
    int score = find_best_answer(message, answer, sizeof(answer));
    
    char safe[MAX_A_LEN * 2] = {0};
    if (answer[0]) {
        json_escape(safe, answer, sizeof(safe));
    } else {
        snprintf(safe, sizeof(safe), "Нет точного ответа на \"%s\". Доступно %d фактов.", message, kb_count);
    }
    
    char resp[8192];
    double conf = score > 50 ? 0.95 : (score > 20 ? 0.9 : (score > 10 ? 0.7 : 0.3));
    snprintf(resp, sizeof(resp),
        "{\"response\":\"%s\",\"conversation_id\":\"%s\",\"method\":\"knowledge_base\",\"confidence\":%.2f,\"duration_ms\":0}",
        safe, conv, conf);
    send_json(fd, resp);
}

static void handle_health(int fd) {
    char resp[512];
    snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"facts\":%d,\"peers\":%d,\"requests\":0}", kb_count, peer_count);
    send_json(fd, resp);
}

static void handle_export(int fd) {
    /* Export knowledge as NDJSON for swarm sync */
    char hdr[256];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\n"
        "Access-Control-Allow-Origin: *\r\n\r\n");
    write(fd, hdr, hlen);
    
    for (int i = 0; i < kb_count; i++) {
        char qsafe[MAX_Q_LEN * 2] = {0}, asafe[MAX_A_LEN * 2] = {0};
        json_escape(qsafe, knowledge_base[i].question, sizeof(qsafe));
        json_escape(asafe, knowledge_base[i].answer, sizeof(asafe));
        char line[4096];
        snprintf(line, sizeof(line), "{\"q\":\"%s\",\"a\":\"%s\"}\n", qsafe, asafe);
        write(fd, line, strlen(line));
    }
}

static void handle_request(int fd, const char *req) {
    /* Parse method and path */
    char method[16] = {0}, path[1024] = {0};
    sscanf(req, "%15s %1023s", method, path);
    
    if (strcmp(method, "OPTIONS") == 0) {
        send_response(fd, 200, "text/plain", "");
        return;
    }
    
    /* Find body after \r\n\r\n */
    const char *body = strstr(req, "\r\n\r\n");
    if (body) body += 4;
    else body = "";
    
    if (strcmp(path, "/api/v1/health") == 0 && strcmp(method, "GET") == 0) {
        handle_health(fd);
    } else if (strcmp(path, "/api/v1/swarm/export") == 0 && strcmp(method, "GET") == 0) {
        handle_export(fd);
    } else if ((strcmp(path, "/api/v1/ai/chat") == 0 || strncmp(path, "/api/v1/ai/chat/", 16) == 0) && strcmp(method, "POST") == 0) {
        handle_chat(fd, body);
    } else {
        send_json(fd, "{\"error\":\"not found\"}");
    }
}

/* ─── Main ─── */
int main(int argc, char *argv[]) {
    int port = PORT_DEFAULT;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--peer") == 0 && i + 1 < argc) {
            i++;
            if (peer_count < MAX_PEERS) {
                strncpy(peer_hosts[peer_count], argv[i], MAX_PEERS_STR - 1);
                peer_hosts[peer_count][MAX_PEERS_STR - 1] = 0;
                peer_count++;
            }
        } else if (argv[i][0] >= '0' && argv[i][0] <= '9') {
            port = atoi(argv[i]);
        }
    }
    
    /* Load knowledge base */
    printf("🐦 Kolibri Swarm Node — port %d, %d peers\n", port, peer_count);
    
    /* Try multiple knowledge base paths */
    const char *kb_paths[] = {
        "knowledge/knowledge_base.md",
        "/opt/kp/knowledge/knowledge_base.md",
        "/home/ladik/kolibri/knowledge/knowledge_base.md",
        NULL
    };
    
    int loaded = 0;
    for (int i = 0; kb_paths[i] && !loaded; i++) {
        loaded = load_knowledge_base(kb_paths[i]);
        if (loaded) printf("  ✅ Loaded %d Q&A facts from %s\n", loaded, kb_paths[i]);
    }
    
    if (!loaded) {
        printf("  ⚠️  No knowledge base found, starting with 0 facts\n");
    }
    
    printf("🌐 Listening on http://0.0.0.0:%d\n\n", port);
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
        perror("bind"); close(srv); return 1;
    }
    if (listen(srv, 32) < 0) {
        perror("listen"); close(srv); return 1;
    }
    
    char buf[BUF_SIZE];
    while (1) {
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);
        int fd = accept(srv, (struct sockaddr*)&cli, &clen);
        if (fd < 0) continue;
        
        int n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            handle_request(fd, buf);
        }
        close(fd);
    }
    
    close(srv);
    return 0;
}
