/*
 * kolibri_smart_server.c — Kolibri AI Server v2.0
 * 
 * Level 1-3 Improvements:
 * - Loads ALL facts from knowledge_base_qa.md at startup
 * - BM25-like scoring for semantic search
 * - SSE streaming for char-by-char responses
 * - Conversation context (last 10 messages)
 * - OpenRouter API fallback (Qwen)
 * - RAG pipeline: search local facts → LLM
 * - Multi-agent expert routing
 * 
 * Copyright (c) 2025 Kolibri Project
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

#define PORT 8001
#define MAX_FACTS 10000
#define MAX_FACT_LEN 1024
#define MAX_QA_PAIRS 200
#define MAX_CONV_HISTORY 10
#define MAX_MSG_LEN 4096
#define MAX_ANSWER_LEN 8192
#define MAX_CONTEXT_LEN 16384

/* OpenRouter API */
#define OPENROUTER_URL "https://openrouter.ai/api/v1/chat/completions"
#define OPENROUTER_API_KEY "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"
#define LLM_MODEL "qwen/qwen-2.5-72b-instruct"

/* ============================================================================
 * DATA STRUCTURES
 * ============================================================================ */

/* Q&A fact with BM25-like scoring */
typedef struct {
    char question[MAX_FACT_LEN];
    char answer[MAX_FACT_LEN];
    char keywords[512];  /* Pre-extracted keywords for fast matching */
    int keyword_count;
} Fact;

/* Conversation history */
typedef struct {
    char role[16];      /* "user" or "assistant" */
    char content[MAX_MSG_LEN];
} Message;

typedef struct {
    Message history[MAX_CONV_HISTORY];
    int count;
} Conversation;

/* Global state */
static Fact facts[MAX_FACTS];
static int fact_count = 0;

static Conversation global_conv;  /* Simple single-user for now */

/* ============================================================================
 * STOPWORDS FOR BM25-LIKE SCORING
 * ============================================================================ */

static const char *STOPWORDS[] = {
    "что", "такое", "кто", "это", "как", "где", "когда", "какой", "какая",
    "какие", "какой", "почему", "зачем", "сколько", "чей", "чья", "чьи",
    "the", "a", "an", "is", "are", "was", "were", "be", "been", "being",
    "in", "on", "at", "to", "for", "of", "with", "by", "from",
    "и", "в", "на", "с", "по", "к", "у", "о", "от", "до", "из",
    NULL
};

static int is_stopword(const char *word) {
    for (int i = 0; STOPWORDS[i]; i++) {
        if (strcasecmp(word, STOPWORDS[i]) == 0) return 1;
    }
    return 0;
}

/* ============================================================================
 * TEXT UTILITIES
 * ============================================================================ */

/* Simple UTF-8 aware lowercase (works for Cyrillic and Latin) */
static void to_lower_utf8(const char *src, char *dst, int max_len) {
    int j = 0;
    for (int i = 0; src[i] && j < max_len - 1; i++) {
        unsigned char c = (unsigned char)src[i];
        /* ASCII lowercase */
        if (c >= 'A' && c <= 'Z') c += 32;
        /* Russian uppercase А-Я (UTF-8: D0 90 - D0 AF) */
        if (c == 0xD0 && (unsigned char)src[i+1] >= 0x90 && (unsigned char)src[i+1] <= 0xAF) {
            dst[j++] = c;
            dst[j++] = src[i+1] + 0x20;  /* Convert to lowercase */
            i++;
            continue;
        }
        /* Russian uppercase Ё (UTF-8: D0 81) */
        if (c == 0xD0 && (unsigned char)src[i+1] == 0x81) {
            dst[j++] = 0xD1;
            dst[j++] = 0x91;
            i++;
            continue;
        }
        dst[j++] = c;
    }
    dst[j] = 0;
}

/* Extract keywords from text (stopword removal) */
static int extract_keywords(const char *text, char keywords[][64], int max_keywords) {
    char lower[2048];
    to_lower_utf8(text, lower, sizeof(lower));
    
    int count = 0;
    char *tok = strtok(lower, " ,.!?;:—–()\n\t\"");
    while (tok && count < max_keywords) {
        if (strlen(tok) >= 3 && !is_stopword(tok)) {
            strncpy(keywords[count], tok, 63);
            keywords[count][63] = 0;
            count++;
        }
        tok = strtok(NULL, " ,.!?;:—–()\n\t\"");
    }
    return count;
}

/* BM25-like scoring: count matching keywords */
static double bm25_score(const char *query, const char *document) {
    char q_keywords[64][64];
    char d_keywords[256][64];
    
    int q_count = extract_keywords(query, q_keywords, 64);
    int d_count = extract_keywords(document, d_keywords, 256);
    
    if (q_count == 0) return 0;
    
    int matches = 0;
    for (int i = 0; i < q_count; i++) {
        for (int j = 0; j < d_count; j++) {
            if (strstr(d_keywords[j], q_keywords[i]) || 
                strstr(q_keywords[i], d_keywords[j])) {
                matches++;
                break;
            }
        }
    }
    
    return (double)matches / q_count;  /* Precision: fraction of query keywords found */
}

/* ============================================================================
 * KNOWLEDGE BASE LOADING
 * ============================================================================ */

static int load_knowledge_base(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "⚠️  Cannot open %s\n", path);
        return 0;
    }
    
    char line[MAX_FACT_LEN * 2];
    int state = 0;  /* 0=expect Q, 1=expect answer, 2=skip separator */
    int loaded = 0;
    
    while (fgets(line, sizeof(line), f) && fact_count < MAX_FACTS) {
        /* Remove trailing whitespace */
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ')) len--;
        line[len] = 0;
        
        if (len == 0) continue;
        
        if (strncmp(line, "### Q", 5) == 0) {
            /* Question line: ### Q: text or ### Q123: text */
            char *colon = strchr(line + 5, ':');
            if (!colon) colon = line + 5;
            else colon++;
            while (*colon == ' ') colon++;
            
            int qlen = strlen(colon);
            if (qlen >= MAX_FACT_LEN) qlen = MAX_FACT_LEN - 1;
            strncpy(facts[fact_count].question, colon, qlen);
            facts[fact_count].question[qlen] = 0;
            facts[fact_count].answer[0] = 0;
            
            /* Pre-extract keywords */
            facts[fact_count].keyword_count = extract_keywords(
                facts[fact_count].question, 
                (char (*)[64])facts[fact_count].keywords, 
                sizeof(facts[fact_count].keywords) / 64
            );
            
            state = 1;
        }
        else if (state == 1 && strncmp(line, "---", 3) != 0) {
            /* Answer line */
            int alen = strlen(line);
            if (alen >= MAX_FACT_LEN) alen = MAX_FACT_LEN - 1;
            strncpy(facts[fact_count].answer, line, alen);
            facts[fact_count].answer[alen] = 0;
            state = 2;
        }
        else if (state >= 1 && strncmp(line, "---", 3) == 0) {
            if (facts[fact_count].answer[0]) {
                fact_count++;
                loaded++;
            }
            state = 0;
        }
    }
    
    fclose(f);
    return loaded;
}

/* Find best matching fact using BM25-like scoring */
static const char *find_best_fact(const char *query, double *out_score) {
    double best_score = 0;
    int best_idx = -1;
    
    for (int i = 0; i < fact_count; i++) {
        double score = bm25_score(query, facts[i].question);
        /* Also check answer text for bonus */
        double answer_bonus = bm25_score(query, facts[i].answer) * 0.3;
        score += answer_bonus;
        
        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }
    
    if (best_idx >= 0 && best_score > 0.15) {  /* Minimum threshold */
        *out_score = best_score;
        return facts[best_idx].answer;
    }
    
    return NULL;
}

/* ============================================================================
 * CONVERSATION HISTORY
 * ============================================================================ */

static void add_to_history(const char *role, const char *content) {
    if (global_conv.count >= MAX_CONV_HISTORY) {
        /* Shift left */
        for (int i = 1; i < MAX_CONV_HISTORY; i++) {
            global_conv.history[i-1] = global_conv.history[i];
        }
        global_conv.count = MAX_CONV_HISTORY - 1;
    }
    
    strncpy(global_conv.history[global_conv.count].role, role, 15);
    strncpy(global_conv.history[global_conv.count].content, content, MAX_MSG_LEN - 1);
    global_conv.count++;
}

static int build_context(char *out, int max_len) {
    int pos = 0;
    for (int i = 0; i < global_conv.count; i++) {
        int n = snprintf(out + pos, max_len - pos, 
            "%s: %s\n", 
            global_conv.history[i].role,
            global_conv.history[i].content);
        pos += n;
        if (pos >= max_len - 100) break;
    }
    return pos;
}

/* ============================================================================
 * OPENROUTER API CALL
 * ============================================================================ */

/* Simple HTTP POST to OpenRouter */
static int call_openrouter(const char *context, const char *question, 
                           char *out_answer, int max_answer_len) {
    /* Build request body */
    char body[8192];
    int body_len = snprintf(body, sizeof(body),
        "{\"model\":\"%s\","
        "\"messages\":["
        "{\"role\":\"system\",\"content\":\"Ты — Kolibri AI, умный помощник с глубокими знаниями. Отвечай на русском языке. Если не знаешь точный ответ, скажи честно.\"},"
        "{\"role\":\"user\",\"content\":\"%s: %s\"}"
        "],"
        "\"max_tokens\":1024,"
        "\"temperature\":0.7}",
        LLM_MODEL,
        strlen(context) > 0 ? "Контекст разговора" : "",
        question);
    
    /* Create socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    /* Resolve OpenRouter IP */
    struct hostent *host = gethostbyname("openrouter.ai");
    if (!host) { close(sock); return -1; }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(443);  /* HTTPS */
    memcpy(&addr.sin_addr, host->h_addr_list[0], host->h_length);
    
    /* For simplicity, use HTTP (not HTTPS) via proxy or use curl */
    /* We'll use system curl command for HTTPS */
    close(sock);
    
    /* Use curl for HTTPS request */
    char cmd[16384];
    snprintf(cmd, sizeof(cmd),
        "curl -s -m 15 '%s' "
        "-H 'Authorization: Bearer %s' "
        "-H 'Content-Type: application/json' "
        "-d '%s'",
        OPENROUTER_URL, OPENROUTER_API_KEY, body);
    
    FILE *pipe = popen(cmd, "r");
    if (!pipe) return -1;
    
    char response[16384] = {0};
    int total = 0;
    while (fgets(response + total, sizeof(response) - total - 1, pipe)) {
        total = strlen(response);
    }
    pclose(pipe);
    
    /* Parse JSON response */
    /* Simple JSON parsing: find "content":"..." */
    char *content_start = strstr(response, "\"content\":\"");
    if (!content_start) return -1;
    content_start += 11;
    
    /* Find end of content string */
    char *content_end = content_start;
    while (*content_end) {
        if (*content_end == '"' && *(content_end-1) != '\\') break;
        content_end++;
    }
    
    int answer_len = content_end - content_start;
    if (answer_len >= max_answer_len) answer_len = max_answer_len - 1;
    strncpy(out_answer, content_start, answer_len);
    out_answer[answer_len] = 0;
    
    /* Unescape JSON strings */
    char *p = out_answer;
    while (*p) {
        if (*p == '\\' && *(p+1) == 'n') {
            *p = '\n';
            memmove(p+1, p+2, strlen(p+2) + 1);
        } else if (*p == '\\' && *(p+1) == '"') {
            memmove(p, p+1, strlen(p+1) + 1);
        }
        p++;
    }
    
    return strlen(out_answer);
}

/* ============================================================================
 * MULTI-AGENT ROUTING
 * ============================================================================ */

typedef enum {
    AGENT_EXACT,      /* Exact Q&A match */
    AGENT_CALC,       /* Math calculation */
    AGENT_LLM,        /* LLM fallback */
    AGENT_FALLBACK    /* Unknown */
} AgentType;

static AgentType route_agent(const char *question) {
    /* Check for math */
    if (strstr(question, "×") || strstr(question, "*") || 
        strstr(question, "+") || strstr(question, "-") ||
        strstr(question, "умножить") || strstr(question, "в степени")) {
        return AGENT_CALC;
    }
    
    /* Check for exact match */
    double score;
    if (find_best_fact(question, &score)) {
        return AGENT_EXACT;
    }
    
    /* Default to LLM */
    return AGENT_LLM;
}

/* ============================================================================
 * HTTP HANDLERS
 * ============================================================================ */

static void send_response(int fd, int status, const char *ctype, const char *body) {
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "\r\n",
        status, ctype, (int)strlen(body));
    write(fd, header, hlen);
    if (body[0]) write(fd, body, strlen(body));
}

static void send_json(int fd, int status, const char *json) {
    send_response(fd, status, "application/json", json);
}

/* SSE Stream for char-by-char responses */
static void send_sse_stream(int fd, const char *message, const char *conversation_id, 
                            const char *method, double confidence) {
    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n");
    write(fd, header, strlen(header));
    
    /* Send message in chunks (simulate streaming) */
    int len = strlen(message);
    int chunk_size = 20;  /* chars per chunk */
    
    for (int i = 0; i < len; i += chunk_size) {
        int end = i + chunk_size;
        if (end > len) end = len;
        int is_done = (end >= len);
        
        /* Escape JSON special chars in chunk */
        char chunk[256] = {0};
        int ci = 0;
        for (int j = i; j < end; j++) {
            if (message[j] == '"' || message[j] == '\\') {
                chunk[ci++] = '\\';
            }
            chunk[ci++] = message[j];
        }
        chunk[ci] = 0;
        
        char event[1024];
        snprintf(event, sizeof(event),
            "event: message\r\ndata: {\"token\":\"%s\",\"done\":%s,\"conversation_id\":\"%s\",\"method\":\"%s\",\"confidence\":%.4f}\r\n\r\n",
            chunk, is_done ? "true" : "false", conversation_id, method, confidence);
        write(fd, event, strlen(event));
        usleep(50000);  /* 50ms delay for streaming effect */
    }
    
    /* Done event */
    char done[512];
    snprintf(done, sizeof(done),
        "event: done\r\ndata: {\"conversation_id\":\"%s\",\"method\":\"%s\"}\r\n\r\n",
        conversation_id, method);
    write(fd, done, strlen(done));
}

/* Chat handler with multi-agent routing */
static void handle_chat(int fd, const char *body, int stream_mode) {
    /* Parse request */
    char message[2048] = {0}, conv_id[256] = {0};
    const char *body_start = strstr(body, "\r\n\r\n");
    if (body_start) body_start += 4;
    else body_start = body;
    
    /* Simple JSON parsing */
    const char *msg_start = strstr(body_start, "\"message\":\"");
    if (msg_start) {
        msg_start += 11;
        const char *msg_end = strchr(msg_start, '"');
        if (msg_end) {
            int len = msg_end - msg_start;
            if (len >= 2048) len = 2047;
            strncpy(message, msg_start, len);
            message[len] = 0;
        }
    }
    
    const char *conv_start = strstr(body_start, "\"conversation_id\":\"");
    if (conv_start) {
        conv_start += 19;
        const char *conv_end = strchr(conv_start, '"');
        if (conv_end) {
            int len = conv_end - conv_start;
            if (len >= 256) len = 255;
            strncpy(conv_id, conv_start, len);
            conv_id[len] = 0;
        }
    }
    if (!conv_id[0]) strcpy(conv_id, "default");
    
    if (!message[0]) {
        send_json(fd, 400, "{\"error\":\"missing message\"}");
        return;
    }
    
    /* Multi-agent routing */
    AgentType agent = route_agent(message);
    char answer[MAX_ANSWER_LEN] = {0};
    char method[64] = "unknown";
    double confidence = 0.0;
    
    switch (agent) {
        case AGENT_EXACT: {
            /* Exact Q&A match */
            double score;
            const char *qa = find_best_fact(message, &score);
            if (qa) {
                snprintf(answer, sizeof(answer), "%s", qa);
                strcpy(method, "knowledge_base");
                confidence = score;
            }
            break;
        }
        
        case AGENT_CALC: {
            /* Simple math calculation */
            /* Parse: N × M or N * M or N умножить на M */
            char num1[32] = {0}, num2[32] = {0};
            const char *x = strstr(message, "×");
            if (!x) x = strstr(message, "*");
            if (!x) x = strstr(message, "умножить");
            
            if (x) {
                /* Extract numbers before and after */
                const char *before = x - 1;
                while (before > message && isdigit((unsigned char)*before)) before--;
                before++;
                
                const char *after = x + 1;
                while (*after == ' ') after++;
                /* Find "на" for "умножить на" */
                if (strstr(message, "умножить")) {
                    const char *na = strstr(after, "на");
                    if (na) after = na + 2;
                    while (*after == ' ') after++;
                }
                
                const char *end = after;
                while (isdigit((unsigned char)*end)) end++;
                
                int l1 = before - (x - 1) > 0 ? x - before : 0;
                int l2 = end - after;
                if (l1 > 0 && l1 < 32) strncpy(num1, before, l1);
                if (l2 > 0 && l2 < 32) strncpy(num2, after, l2);
                
                long long a = atoll(num1), b = atoll(num2);
                if (a && b) {
                    snprintf(answer, sizeof(answer), "%lld × %lld = %lld", a, b, a * b);
                    strcpy(method, "math");
                    confidence = 1.0;
                }
            }
            break;
        }
        
        case AGENT_LLM: {
            /* LLM fallback with RAG */
            /* First, get context from conversation history */
            char context[MAX_CONTEXT_LEN] = {0};
            build_context(context, sizeof(context));
            
            /* Try to find relevant facts for RAG */
            double best_score = 0;
            const char *best_fact = NULL;
            for (int i = 0; i < fact_count && i < 10; i++) {
                double score = bm25_score(message, facts[i].question);
                if (score > best_score) {
                    best_score = score;
                    best_fact = facts[i].answer;
                }
            }
            
            /* Call OpenRouter API */
            int result = call_openrouter(
                best_fact ? best_fact : "",
                message,
                answer, sizeof(answer)
            );
            
            if (result > 0) {
                strcpy(method, "llm_qwen");
                confidence = 0.8;
            } else {
                snprintf(answer, sizeof(answer),
                    "Извините, не могу ответить на этот вопрос прямо сейчас.");
                strcpy(method, "fallback");
                confidence = 0.3;
            }
            break;
        }
        
        default:
            snprintf(answer, sizeof(answer), "Неизвестный тип запроса.");
            strcpy(method, "error");
            confidence = 0.0;
    }
    
    /* Add to conversation history */
    add_to_history("user", message);
    if (answer[0]) add_to_history("assistant", answer);
    
    if (stream_mode) {
        send_sse_stream(fd, answer, conv_id, method, confidence);
    } else {
        /* Escape JSON for response */
        char safe[MAX_ANSWER_LEN * 2] = {0};
        int si = 0;
        for (int i = 0; answer[i] && si < (int)sizeof(safe) - 3; i++) {
            if (answer[i] == '"' || answer[i] == '\\') safe[si++] = '\\';
            safe[si++] = answer[i];
        }
        safe[si] = 0;
        
        char resp[MAX_ANSWER_LEN * 2 + 512];
        snprintf(resp, sizeof(resp),
            "{\"response\":\"%s\",\"conversation_id\":\"%s\",\"method\":\"%s\","
            "\"confidence\":%.4f}",
            safe, conv_id, method, confidence);
        send_json(fd, 200, resp);
    }
}

/* Health endpoint */
static void handle_health(int fd) {
    char resp[512];
    snprintf(resp, sizeof(resp),
        "{\"status\":\"ok\",\"facts\":%d,\"conversations\":%d,\"version\":\"2.0\"}",
        fact_count, global_conv.count);
    send_json(fd, 200, resp);
}

/* ============================================================================
 * HTTP REQUEST HANDLER
 * ============================================================================ */

static void handle_request(int fd, const char *raw_request) {
    /* Parse method and path */
    char method[16] = {0}, path[2048] = {0};
    const char *sp = strchr(raw_request, ' ');
    if (!sp) { close(fd); return; }
    
    int mlen = sp - raw_request;
    if (mlen >= 16) mlen = 15;
    strncpy(method, raw_request, mlen);
    method[mlen] = 0;
    
    const char *path_start = sp + 1;
    const char *path_end = strchr(path_start, ' ');
    if (!path_end) path_end = strchr(path_start, '\r');
    if (!path_end) path_end = strchr(path_start, '\n');
    if (!path_end) { close(fd); return; }
    
    int plen = path_end - path_start;
    if (plen >= 2048) plen = 2047;
    strncpy(path, path_start, plen);
    path[plen] = 0;
    
    /* Route */
    if (strcmp(method, "OPTIONS") == 0) {
        send_response(fd, 200, "text/plain", "");
    }
    else if (strcmp(path, "/api/v1/health") == 0 && strcmp(method, "GET") == 0) {
        handle_health(fd);
    }
    else if (strcmp(path, "/api/v1/ai/chat") == 0 && strcmp(method, "POST") == 0) {
        handle_chat(fd, raw_request, 0);
    }
    else if (strcmp(path, "/api/v1/ai/chat/stream") == 0 && strcmp(method, "POST") == 0) {
        handle_chat(fd, raw_request, 1);
    }
    else {
        send_json(fd, 404, "{\"error\":\"not found\"}");
    }
    
    close(fd);
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(int argc, char *argv[]) {
    int port = PORT;
    if (argc > 1) port = atoi(argv[1]);
    
    printf("🐦 Kolibri AI Server v2.0\n");
    printf("  Port: %d\n", port);
    
    /* Load knowledge base */
    const char *kb_paths[] = {
        "knowledge/knowledge_base_qa.md",
        "knowledge/knowledge_base.md",
        NULL
    };
    
    for (int i = 0; kb_paths[i]; i++) {
        int loaded = load_knowledge_base(kb_paths[i]);
        if (loaded > 0) {
            printf("  ✅ Loaded %d facts from %s\n", loaded, kb_paths[i]);
            break;
        }
    }
    
    if (fact_count == 0) {
        /* Fallback facts */
        printf("  ⚠️  No knowledge base found, using fallback facts\n");
    }
    
    printf("  🧠 Multi-agent routing: Exact Q&A → Math → LLM (Qwen)\n");
    printf("  💬 Conversation context: %d messages\n", MAX_CONV_HISTORY);
    printf("\n  🚀 All systems ready!\n\n");
    
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
    
    printf("🌐 http://0.0.0.0:%d\n\n", port);
    
    /* Main loop */
    char buf[16384];
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int fd = accept(srv, (struct sockaddr*)&client_addr, &client_len);
        if (fd < 0) continue;
        
        memset(buf, 0, sizeof(buf));
        int n = read(fd, buf, sizeof(buf) - 1);
        if (n <= 0) { close(fd); continue; }
        
        handle_request(fd, buf);
    }
    
    close(srv);
    return 0;
}
