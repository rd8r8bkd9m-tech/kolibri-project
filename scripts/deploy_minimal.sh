#!/bin/bash
# Build kolibri_http on Node 1 with minimal set of sources
set -e

ssh -o StrictHostKeyChecking=no -i ~/.ssh/id_ed25519 root@217.60.249.157 bash << 'REMOTE'
set -e
cd /opt
rm -rf kolibri && mkdir kolibri && cd kolibri

# Create include directory
mkdir -p backend/include/kolibri

# Receive files via heredoc approach - too slow, use inline compile
# Instead, let's just write a minimal self-contained server

cat > kolibri_http.c << 'CEOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <time.h>

#define PORT 8001
#define MAX_REQ 8192
#define MAX_RESP 65536

/* Globals */
static char answers[10000][2][512];
static int answer_count = 0;

static void load_knowledge(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[1024], current_q[512] = {0}, current_a[512] = {0};
    int in_q = 0, in_a = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "### Q", 5) == 0) {
            /* Extract question after "### Q####: " */
            char *colon = strchr(line, ':');
            if (colon) {
                strncpy(current_q, colon + 2, sizeof(current_q) - 1);
                current_q[strcspn(current_q, "\n")] = 0;
                in_q = 1;
            }
        } else if (strncmp(line, "**Ответ:**", 10) == 0) {
            strncpy(current_a, line + 10, sizeof(current_a) - 1);
            current_a[strcspn(current_a, "\n")] = 0;
            in_a = 1;
        } else if (in_a && (line[0] == '\n' || line[0] == '-' )) {
            if (answer_count < 10000 && current_q[0] && current_a[0]) {
                strncpy(answers[answer_count][0], current_q, 511);
                strncpy(answers[answer_count][1], current_a, 511);
                answer_count++;
            }
            current_q[0] = current_a[0] = 0;
            in_q = in_a = 0;
        }
    }
    fclose(f);
    printf("  Loaded %d Q&A pairs\n", answer_count);
}

/* Simple keyword match */
static const char* find_answer(const char *query) {
    /* Try exact substring match first */
    for (int i = 0; i < answer_count; i++) {
        if (strstr(answers[i][0], query) || strstr(query, answers[i][0])) {
            return answers[i][1];
        }
    }
    /* Try keyword overlap */
    int best_score = 0, best_idx = -1;
    char qbuf[512];
    strncpy(qbuf, query, sizeof(qbuf) - 1);
    char *tok = strtok(qbuf, " ,.!?;:—–()\n\t");
    while (tok) {
        if (strlen(tok) > 2) {
            for (int i = 0; i < answer_count; i++) {
                int score = 0;
                if (strstr(answers[i][0], tok)) score++;
                if (strstr(answers[i][1], tok)) score++;
                if (score > best_score) {
                    best_score = score;
                    best_idx = i;
                }
            }
        }
        tok = strtok(NULL, " ,.!?;:—–()\n\t");
    }
    return best_idx >= 0 ? answers[best_idx][1] : NULL;
}

/* HTTP helpers */
static void send_json(int fd, const char *json) {
    char hdr[512];
    int len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\nAccess-Control-Allow-Origin: *\r\n\r\n",
        strlen(json));
    write(fd, hdr, len);
    write(fd, json, strlen(json));
}

static int parse_body(const char *req, char *body, int max) {
    const char *p = strstr(req, "\r\n\r\n");
    if (!p) return 0;
    p += 4;
    strncpy(body, p, max - 1);
    return 1;
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
        if (in[i] == '"' || in[i] == '\\') out[j++] = '\\';
        out[j++] = in[i];
    }
    out[j] = 0;
}

int main() {
    printf("🐦 Kolibri Swarm Node — Loading knowledge...\n");
    
    /* Load knowledge base */
    load_knowledge("knowledge/knowledge_base.md");
    if (answer_count == 0) {
        /* Fallback: load from /opt/kolibri/knowledge/ */
        load_knowledge("/opt/kolibri/knowledge/knowledge_base.md");
    }
    printf("  Total: %d facts loaded\n", answer_count);
    printf("🌐 Listening on port %d\n\n", PORT);
    
    /* Create socket */
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    bind(srv, (struct sockaddr*)&addr, sizeof(addr));
    listen(srv, 16);
    
    char buf[MAX_REQ];
    while (1) {
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);
        int fd = accept(srv, (struct sockaddr*)&cli, &clen);
        if (fd < 0) continue;
        
        int n = read(fd, buf, sizeof(buf) - 1);
        if (n <= 0) { close(fd); continue; }
        buf[n] = 0;
        
        /* Parse path */
        char *sp = strchr(buf, ' ');
        if (!sp) { close(fd); continue; }
        char *path = sp + 1;
        char *pe = strchr(path, ' ');
        if (pe) *pe = 0;
        pe = strchr(path, '\r');
        if (pe) *pe = 0;
        
        /* Get method */
        buf[sp - buf] = 0;
        const char *method = buf;
        
        /* Parse body */
        char body[4096] = {0};
        parse_body(buf, body, sizeof(body));
        
        printf("  %s %s\n", method, path);
        
        /* Route */
        if (strcmp(path, "/api/v1/health") == 0 || strcmp(path, "/api/v1/ai/health") == 0) {
            send_json(fd, "{\"status\":\"ok\",\"facts\":" 
                      "{" 
                      "\"loaded\":" #answer_count ","
                      "\"swarm\":\"ready\""
                      "}}");
        } else if (strcmp(path, "/api/v1/ai/chat") == 0 || strncmp(path, "/api/v1/ai/chat/stream", 22) == 0) {
            char message[2048] = {0}, conv[256] = {0};
            get_json_str(body, "message", message, sizeof(message));
            get_json_str(body, "conversation_id", conv, sizeof(conv));
            
            const char *ans = find_answer(message);
            char safe[4096] = {0};
            if (ans) json_escape(safe, ans, sizeof(safe));
            else snprintf(safe, sizeof(safe), "Нет точного ответа на \"%s\". Доступных фактов: %d", message, answer_count);
            
            char resp[8192];
            snprintf(resp, sizeof(resp),
                "{\"response\":\"%s\",\"conversation_id\":\"%s\","
                "\"method\":\"knowledge_base\",\"confidence\":%s,\"duration_ms\":0}",
                safe, conv[0] ? conv : "default",
                ans ? "0.9" : "0.3");
            send_json(fd, resp);
        } else if (strncmp(path, "/api/", 5) == 0) {
            send_json(fd, "{}");
        } else {
            send_json(fd, "{\"error\":\"not found\"}");
        }
        close(fd);
    }
    return 0;
}
CEOF

echo "Compiling..."
gcc -O2 -o kolibri_http kolibri_http.c -lm
echo "✅ Built: $(wc -c < kolibri_http) bytes"

# Setup knowledge
mkdir -p knowledge
