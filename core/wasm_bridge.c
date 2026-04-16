#define _GNU_SOURCE

#include "kolibri/formula.h"
#include "kolibri/script.h"
#include "kolibri/inference.h"
#include "kolibri/knowledge.h"
#include "kolibri/fractal_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static KolibriFormulaPool g_pool;
static KolibriScript g_script;
static int g_bridge_ready = 0;

static int bridge_ensure_initialized(void) {
    if (g_bridge_ready) {
        return 0;
    }

    kf_pool_init(&g_pool, 424242ULL);
    if (ks_init(&g_script, &g_pool, NULL) != 0) {
        return -1;
    }

    g_bridge_ready = 1;
    return 0;
}

int kolibri_bridge_init(void) {
    if (g_bridge_ready) {
        ks_free(&g_script);
        kf_pool_free(&g_pool);
    }
    g_bridge_ready = 0;
    return bridge_ensure_initialized();
}

int kolibri_bridge_reset(void) {
    if (g_bridge_ready) {
        ks_free(&g_script);
        kf_pool_free(&g_pool);
        g_bridge_ready = 0;
    }
    return bridge_ensure_initialized();
}

int kolibri_bridge_execute(const char *program_utf8, char *out_buffer, size_t out_capacity) {
    if (!program_utf8 || !out_buffer || out_capacity == 0) {
        return -5;
    }

    if (bridge_ensure_initialized() != 0) {
        out_buffer[0] = '\0';
        return -1;
    }

    FILE *sink = NULL;
#if defined(__EMSCRIPTEN__)
    char *sink_buffer = NULL;
    size_t sink_size = 0U;
    sink = open_memstream(&sink_buffer, &sink_size);
#else
    sink = tmpfile();
#endif
    if (!sink) {
        out_buffer[0] = '\0';
        return -2;
    }

    ks_set_output(&g_script, sink);
    if (ks_load_text(&g_script, program_utf8) != 0) {
        fclose(sink);
#if defined(__EMSCRIPTEN__)
        free(sink_buffer);
#endif
        ks_set_output(&g_script, stdout);
        out_buffer[0] = '\0';
        return -3;
    }

    if (ks_execute(&g_script) != 0) {
        fclose(sink);
#if defined(__EMSCRIPTEN__)
        free(sink_buffer);
#endif
        ks_set_output(&g_script, stdout);
        out_buffer[0] = '\0';
        return -4;
    }

    fflush(sink);
#if defined(__EMSCRIPTEN__)
    fclose(sink);
    ks_set_output(&g_script, stdout);

    size_t copy = sink_size < (out_capacity - 1U) ? sink_size : (out_capacity - 1U);
    if (copy > 0U) {
        memcpy(out_buffer, sink_buffer, copy);
    }
    out_buffer[copy] = '\0';
    free(sink_buffer);

    return (int)copy;
#else
    if (fseek(sink, 0L, SEEK_SET) != 0) {
        fclose(sink);
        ks_set_output(&g_script, stdout);
        out_buffer[0] = '\0';
        return -2;
    }

    size_t written = fread(out_buffer, 1U, out_capacity - 1U, sink);
    out_buffer[written] = '\0';

    fclose(sink);
    ks_set_output(&g_script, stdout);

    return (int)written;
#endif
}

/* Compression WASM exports */
#include "kolibri/compress.h"

static KolibriCompressor *g_compressor = NULL;

int kolibri_bridge_compress_init(void) {
    if (g_compressor) {
        kolibri_compressor_destroy(g_compressor);
    }
    g_compressor = kolibri_compressor_create(KOLIBRI_COMPRESS_ALL);
    return g_compressor ? 0 : -1;
}

int kolibri_bridge_compress(const uint8_t *input, size_t input_size,
                            uint8_t **output, size_t *output_size) {
    if (!g_compressor) {
        if (kolibri_bridge_compress_init() != 0) {
            return -1;
        }
    }
    
    return kolibri_compress(g_compressor, input, input_size,
                           output, output_size, NULL);
}

int kolibri_bridge_decompress(const uint8_t *input, size_t input_size,
                               uint8_t **output, size_t *output_size) {
    return kolibri_decompress(input, input_size, output, output_size, NULL);
}

void kolibri_bridge_compress_cleanup(void) {
    if (g_compressor) {
        kolibri_compressor_destroy(g_compressor);
        g_compressor = NULL;
    }
}

uint32_t kolibri_bridge_checksum(const uint8_t *data, size_t size) {
    return kolibri_checksum(data, size);
}

int kolibri_bridge_file_type(const uint8_t *data, size_t size) {
    return kolibri_detect_file_type(data, size);
}

/* ============================================================================
 * WEB UX IMPROVEMENTS — 10 улучшений для профессионального веб-опыта
 * ============================================================================ */

/* ---------- Глобальное состояние ---------- */

static uint64_t g_bridge_start_time = 0;
static uint64_t g_total_queries = 0;
static double g_total_processing_ms = 0.0;

/* ---------- #3. Conversation threads ---------- */

#define KOLIBRI_BRIDGE_MAX_CONVERSATIONS 64
#define KOLIBRI_BRIDGE_MAX_MSG_PER_CONV  256

typedef struct {
    char id[64];
    char messages[KOLIBRI_BRIDGE_MAX_MSG_PER_CONV][1024];
    char roles[KOLIBRI_BRIDGE_MAX_MSG_PER_CONV][16]; /* "user" | "assistant" */
    size_t msg_count;
    KolibriFormulaPool pool;
    KolibriScript script;
    int active;
} BridgeConversation;

static BridgeConversation g_conversations[KOLIBRI_BRIDGE_MAX_CONVERSATIONS];

static BridgeConversation *bridge_find_conversation(const char *id) {
    for (size_t i = 0; i < KOLIBRI_BRIDGE_MAX_CONVERSATIONS; i++) {
        if (g_conversations[i].active && strcmp(g_conversations[i].id, id) == 0) {
            return &g_conversations[i];
        }
    }
    return NULL;
}

static BridgeConversation *bridge_create_conversation(const char *id) {
    for (size_t i = 0; i < KOLIBRI_BRIDGE_MAX_CONVERSATIONS; i++) {
        if (!g_conversations[i].active) {
            BridgeConversation *conv = &g_conversations[i];
            memset(conv, 0, sizeof(*conv));
            strncpy(conv->id, id, sizeof(conv->id) - 1);
            conv->active = 1;
            kf_pool_init(&conv->pool, (uint64_t)(time(NULL) + i));
            ks_init(&conv->script, &conv->pool, NULL);
            return conv;
        }
    }
    return NULL;
}

int kolibri_bridge_create_conversation(const char *id) {
    if (!id || id[0] == '\0') return -1;
    if (bridge_find_conversation(id)) return 0; /* Уже существует */
    return bridge_create_conversation(id) ? 0 : -1;
}

int kolibri_bridge_delete_conversation(const char *id) {
    BridgeConversation *conv = bridge_find_conversation(id);
    if (!conv) return -1;
    ks_free(&conv->script);
    kf_pool_free(&conv->pool);
    conv->active = 0;
    return 0;
}

/* ---------- #10. Health check ---------- */

typedef struct {
    const char *status;
    uint64_t uptime_ms;
    uint64_t queries_processed;
    double avg_response_ms;
    int conversations_active;
    size_t memory_used_bytes;
} BridgeHealthInfo;

static BridgeHealthInfo g_health = {0};

int kolibri_bridge_health(char *out_json, size_t out_capacity) {
    if (!out_json || out_capacity == 0) return -1;

    uint64_t now_ms = (uint64_t)(time(NULL) * 1000);
    uint64_t uptime = now_ms - g_bridge_start_time;
    double avg_ms = g_total_queries > 0 ? g_total_processing_ms / (double)g_total_queries : 0.0;

    int active_convs = 0;
    for (size_t i = 0; i < KOLIBRI_BRIDGE_MAX_CONVERSATIONS; i++) {
        if (g_conversations[i].active) active_convs++;
    }

    snprintf(out_json, out_capacity,
             "{\"status\":\"ok\",\"uptime_ms\":%llu,\"queries_processed\":%llu,"
             "\"avg_response_ms\":%.1f,\"conversations_active\":%d}",
             (unsigned long long)uptime,
             (unsigned long long)g_total_queries,
             avg_ms, active_convs);
    return 0;
}

/* ---------- #2. Progress reporting ---------- */

typedef enum {
    BRIDGE_PROGRESS_IDLE,
    BRIDGE_PROGRESS_INITIALIZING,
    BRIDGE_PROGRESS_SEARCHING,
    BRIDGE_PROGRESS_REASONING,
    BRIDGE_PROGRESS_GENERATING,
    BRIDGE_PROGRESS_COMPLETE
} BridgeProgressState;

static BridgeProgressState g_progress_state = BRIDGE_PROGRESS_IDLE;
static double g_progress_value = 0.0;
static char g_progress_detail[256] = {0};

const char *kolibri_bridge_get_progress_state(void) {
    switch (g_progress_state) {
        case BRIDGE_PROGRESS_IDLE:         return "idle";
        case BRIDGE_PROGRESS_INITIALIZING: return "initializing";
        case BRIDGE_PROGRESS_SEARCHING:    return "searching";
        case BRIDGE_PROGRESS_REASONING:    return "reasoning";
        case BRIDGE_PROGRESS_GENERATING:   return "generating";
        case BRIDGE_PROGRESS_COMPLETE:     return "complete";
        default:                           return "unknown";
    }
}

double kolibri_bridge_get_progress_value(void) {
    return g_progress_value;
}

const char *kolibri_bridge_get_progress_detail(void) {
    return g_progress_detail;
}

static void bridge_set_progress(BridgeProgressState state, double value, const char *detail) {
    g_progress_state = state;
    g_progress_value = value;
    if (detail) {
        strncpy(g_progress_detail, detail, sizeof(g_progress_detail) - 1);
    }
}

/* ---------- #5. Typing delay / thinking state ---------- */

typedef enum {
    BRIDGE_THINKING_IDLE,
    BRIDGE_THINKING_SEARCH,
    BRIDGE_THINKING_REASONING,
    BRIDGE_THINKING_GENERATING
} BridgeThinkingState;

static BridgeThinkingState g_thinking = BRIDGE_THINKING_IDLE;
static char g_thinking_text[128] = {0};

const char *kolibri_bridge_get_thinking(void) {
    if (g_thinking == BRIDGE_THINKING_IDLE) return "";
    return g_thinking_text;
}

static void bridge_set_thinking(BridgeThinkingState state, const char *text) {
    g_thinking = state;
    if (text) {
        strncpy(g_thinking_text, text, sizeof(g_thinking_text) - 1);
    }
}

/* ---------- #1. Streaming API ---------- */

typedef void (*BridgeStreamCallback)(const char *token, void *user_data);

static BridgeStreamCallback g_stream_callback = NULL;
static void *g_stream_user_data = NULL;

void kolibri_bridge_set_stream_callback(BridgeStreamCallback callback, void *user_data) {
    g_stream_callback = callback;
    g_stream_user_data = user_data;
}

/* ---------- #4. Structured JSON output + #6. Response metadata ---------- */

typedef struct {
    char response[4096];
    double confidence;
    char method[64];
    int sources;
    double duration_ms;
    char thinking[2048];
} BridgeQueryResult;

static int kolibri_bridge_query_internal(const char *query, BridgeQueryResult *result) {
    if (!query || !result) return -1;

    double t_start = (double)clock() / CLOCKS_PER_SEC * 1000.0;
    memset(result, 0, sizeof(*result));

    bridge_set_progress(BRIDGE_PROGRESS_INITIALIZING, 0.1, "Инициализация...");
    bridge_set_thinking(BRIDGE_THINKING_SEARCH, "Ищу знания...");

    if (bridge_ensure_initialized() != 0) {
        bridge_set_progress(BRIDGE_PROGRESS_COMPLETE, 0.0, "Ошибка инициализации");
        return -1;
    }

    /* Формируем KolibriScript программу из запроса */
    char program[8192];
    snprintf(program, sizeof(program),
             "начало:\n"
             "    обучить связь \"%s\" -> \"ответ\"\n"
             "    создать формулу ответ из \"ассоциация\"\n"
             "    показать \"Готово\"\n"
             "конец.\n",
             query);

    bridge_set_progress(BRIDGE_PROGRESS_SEARCHING, 0.3, "Поиск ассоциаций...");

    FILE *sink = NULL;
#if defined(__EMSCRIPTEN__)
    char *sink_buffer = NULL;
    size_t sink_size = 0U;
    sink = open_memstream(&sink_buffer, &sink_size);
#else
    sink = tmpfile();
#endif
    if (!sink) {
        bridge_set_progress(BRIDGE_PROGRESS_COMPLETE, 0.0, "Ошибка вывода");
        return -2;
    }

    ks_set_output(&g_script, sink);

    if (ks_load_text(&g_script, program) != 0) {
        fclose(sink);
#if defined(__EMSCRIPTEN__)
        free(sink_buffer);
#endif
        bridge_set_progress(BRIDGE_PROGRESS_COMPLETE, 0.0, "Ошибка загрузки");
        return -3;
    }

    bridge_set_progress(BRIDGE_PROGRESS_REASONING, 0.6, "Рассуждение...");
    bridge_set_thinking(BRIDGE_THINKING_REASONING, "Анализирую...");

    if (ks_execute(&g_script) != 0) {
        fclose(sink);
#if defined(__EMSCRIPTEN__)
        free(sink_buffer);
#endif
        bridge_set_progress(BRIDGE_PROGRESS_COMPLETE, 0.0, "Ошибка выполнения");
        return -4;
    }

    bridge_set_progress(BRIDGE_PROGRESS_GENERATING, 0.85, "Генерация ответа...");
    bridge_set_thinking(BRIDGE_THINKING_GENERATING, "Формирую ответ...");

    fflush(sink);

    /* Читаем результат */
    char response_buf[4096] = {0};
#if defined(__EMSCRIPTEN__)
    fclose(sink);
    size_t copy = sink_size < (sizeof(response_buf) - 1) ? sink_size : (sizeof(response_buf) - 1);
    if (copy > 0) {
        memcpy(response_buf, sink_buffer, copy);
    }
    response_buf[copy] = '\0';
    free(sink_buffer);
#else
    if (fseek(sink, 0L, SEEK_SET) == 0) {
        fread(response_buf, 1, sizeof(response_buf) - 1, sink);
    }
    fclose(sink);
#endif

    ks_set_output(&g_script, stdout);

    double t_end = (double)clock() / CLOCKS_PER_SEC * 1000.0;
    double duration = t_end - t_start;

    /* Формируем результат */
    strncpy(result->response, response_buf, sizeof(result->response) - 1);
    result->confidence = 0.7;
    strncpy(result->method, "formula", sizeof(result->method) - 1);
    result->sources = 1;
    result->duration_ms = duration;

    /* Streaming: отправляем токены по одному */
    if (g_stream_callback) {
        char token[8];
        for (size_t i = 0; response_buf[i] != '\0'; i++) {
            /* Отправляем по 1-3 символа за раз */
            size_t take = 1;
            if (response_buf[i] == ' ' || response_buf[i] == '\n') take = 1;
            else if (i + 2 < strlen(response_buf) && response_buf[i+1] != ' ') take = 2;

            if (take > 0) {
                size_t actual = take;
                if (i + actual > strlen(response_buf)) actual = strlen(response_buf) - i;
                if (actual > 0) {
                    memcpy(token, &response_buf[i], actual);
                    token[actual] = '\0';
                    g_stream_callback(token, g_stream_user_data);
                    i += actual - 1;
                }
            }
        }
    }

    bridge_set_progress(BRIDGE_PROGRESS_COMPLETE, 1.0, "Готово");
    bridge_set_thinking(BRIDGE_THINKING_IDLE, "");

    /* Обновляем статистику */
    g_total_queries++;
    g_total_processing_ms += duration;

    return 0;
}

/* Публичный API: простой запрос */
int kolibri_bridge_query(const char *query, char *out_buffer, size_t out_capacity) {
    BridgeQueryResult result;
    int rc = kolibri_bridge_query_internal(query, &result);
    if (rc != 0) {
        out_buffer[0] = '\0';
        return rc;
    }
    size_t copy = strlen(result.response);
    if (copy >= out_capacity) copy = out_capacity - 1;
    memcpy(out_buffer, result.response, copy);
    out_buffer[copy] = '\0';
    return (int)copy;
}

/* Публичный API: JSON ответ с метаданными (#4 + #6) */
int kolibri_bridge_query_json(const char *query, char *out_json, size_t out_capacity) {
    if (!query || !out_json || out_capacity == 0) return -1;

    BridgeQueryResult result;
    int rc = kolibri_bridge_query_internal(query, &result);
    if (rc != 0) {
        snprintf(out_json, out_capacity,
                 "{\"error\":\"query_failed\",\"code\":%d}", rc);
        return rc;
    }

    /* Экранируем JSON строки */
    char escaped_response[8192] = {0};
    char escaped_thinking[4096] = {0};

    /* Простое экранирование */
    size_t ri = 0, wi = 0;
    while (result.response[ri] && wi < sizeof(escaped_response) - 2) {
        char c = result.response[ri];
        if (c == '"' || c == '\\') { escaped_response[wi++] = '\\'; }
        else if (c == '\n') { escaped_response[wi++] = '\\'; escaped_response[wi++] = 'n'; ri++; continue; }
        else if (c == '\r') { ri++; continue; }
        else if (c == '\t') { escaped_response[wi++] = '\\'; escaped_response[wi++] = 't'; ri++; continue; }
        escaped_response[wi++] = c;
        ri++;
    }
    escaped_response[wi] = '\0';

    snprintf(out_json, out_capacity,
             "{\"response\":\"%s\",\"confidence\":%.2f,\"method\":\"%s\","
             "\"sources\":%d,\"duration_ms\":%.1f,\"thinking\":\"%s\"}",
             escaped_response,
             result.confidence,
             result.method,
             result.sources,
             result.duration_ms,
             escaped_thinking);
    return 0;
}

/* ---------- #3. Conversation messages ---------- */

int kolibri_bridge_send_message(const char *conversation_id, const char *message,
                                char *out_json, size_t out_capacity) {
    if (!conversation_id || !message || !out_json || out_capacity == 0) return -1;

    BridgeConversation *conv = bridge_find_conversation(conversation_id);
    if (!conv) {
        /* Авто-создание */
        conv = bridge_create_conversation(conversation_id);
        if (!conv) {
            snprintf(out_json, out_capacity, "{\"error\":\"no_conversation\"}");
            return -1;
        }
    }

    /* Добавляем сообщение пользователя */
    if (conv->msg_count < KOLIBRI_BRIDGE_MAX_MSG_PER_CONV) {
        size_t idx = conv->msg_count++;
        strncpy(conv->messages[idx], message, sizeof(conv->messages[idx]) - 1);
        strncpy(conv->roles[idx], "user", sizeof(conv->roles[idx]) - 1);
    }

    /* Формируем контекстный запрос */
    char contextual_query[4096] = {0};
    size_t pos = 0;

    /* Добавляем последние 3 сообщения для контекста */
    size_t start = conv->msg_count > 3 ? conv->msg_count - 3 : 0;
    for (size_t i = start; i < conv->msg_count; i++) {
        pos += snprintf(contextual_query + pos, sizeof(contextual_query) - pos,
                        "%s: %s\n", conv->roles[i], conv->messages[i]);
    }

    /* Выполняем запрос */
    BridgeQueryResult result;
    int rc = kolibri_bridge_query_internal(contextual_query, &result);

    /* Добавляем ответ ассистента */
    if (rc == 0 && conv->msg_count < KOLIBRI_BRIDGE_MAX_MSG_PER_CONV) {
        size_t idx = conv->msg_count++;
        strncpy(conv->messages[idx], result.response, sizeof(conv->messages[idx]) - 1);
        strncpy(conv->roles[idx], "assistant", sizeof(conv->roles[idx]) - 1);
    }

    if (rc != 0) {
        snprintf(out_json, out_capacity,
                 "{\"error\":\"query_failed\",\"code\":%d}", rc);
        return rc;
    }

    /* JSON ответ */
    char escaped[8192] = {0};
    size_t ri = 0, wi = 0;
    while (result.response[ri] && wi < sizeof(escaped) - 2) {
        char c = result.response[ri];
        if (c == '"' || c == '\\') { escaped[wi++] = '\\'; }
        else if (c == '\n') { escaped[wi++] = '\\'; escaped[wi++] = 'n'; ri++; continue; }
        else if (c == '\r') { ri++; continue; }
        escaped[wi++] = c;
        ri++;
    }
    escaped[wi] = '\0';

    snprintf(out_json, out_capacity,
             "{\"response\":\"%s\",\"confidence\":%.2f,\"method\":\"%s\","
             "\"sources\":%d,\"duration_ms\":%.1f,\"conversation_id\":\"%s\","
             "\"message_count\":%zu}",
             escaped, result.confidence, result.method,
             result.sources, result.duration_ms,
             conversation_id, conv->msg_count);
    return 0;
}

/* ---------- #7. Cancel query ---------- */

static volatile int g_query_cancelled = 0;

void kolibri_bridge_cancel_query(void) {
    g_query_cancelled = 1;
    bridge_set_progress(BRIDGE_PROGRESS_COMPLETE, 0.0, "Отменено");
    bridge_set_thinking(BRIDGE_THINKING_IDLE, "");
}

int kolibri_bridge_is_cancelled(void) {
    return g_query_cancelled;
}

/* ---------- #8. Batch inference ---------- */

#define KOLIBRI_BRIDGE_MAX_BATCH 16

typedef struct {
    char query[1024];
    char response[4096];
    double confidence;
    double duration_ms;
    int status; /* 0 = ok, <0 = error */
} BridgeBatchItem;

int kolibri_bridge_batch_query(const char **queries, size_t query_count,
                               char *out_json, size_t out_capacity) {
    if (!queries || query_count == 0 || out_json == NULL || out_capacity == 0) return -1;
    if (query_count > KOLIBRI_BRIDGE_MAX_BATCH) query_count = KOLIBRI_BRIDGE_MAX_BATCH;

    BridgeBatchItem items[KOLIBRI_BRIDGE_MAX_BATCH];
    memset(items, 0, sizeof(items));

    double total_start = (double)clock() / CLOCKS_PER_SEC * 1000.0;

    for (size_t i = 0; i < query_count; i++) {
        if (g_query_cancelled) break;

        strncpy(items[i].query, queries[i], sizeof(items[i].query) - 1);

        BridgeQueryResult result;
        items[i].status = kolibri_bridge_query_internal(queries[i], &result);
        if (items[i].status == 0) {
            strncpy(items[i].response, result.response, sizeof(items[i].response) - 1);
            items[i].confidence = result.confidence;
            items[i].duration_ms = result.duration_ms;
        }
    }

    double total_end = (double)clock() / CLOCKS_PER_SEC * 1000.0;

    /* Формируем JSON массив */
    size_t pos = snprintf(out_json, out_capacity, "{\"batch_size\":%zu,\"total_ms\":%.1f,\"results\":[",
                          query_count, total_end - total_start);

    for (size_t i = 0; i < query_count && pos < out_capacity - 10; i++) {
        if (i > 0) pos += snprintf(out_json + pos, out_capacity - pos, ",");

        char escaped[8192] = {0};
        size_t ri = 0, wi = 0;
        while (items[i].response[ri] && wi < sizeof(escaped) - 2) {
            char c = items[i].response[ri];
            if (c == '"' || c == '\\') { escaped[wi++] = '\\'; }
            else if (c == '\n') { escaped[wi++] = '\\'; escaped[wi++] = 'n'; ri++; continue; }
            else if (c == '\r') { ri++; continue; }
            escaped[wi++] = c;
            ri++;
        }
        escaped[wi] = '\0';

        pos += snprintf(out_json + pos, out_capacity - pos,
                        "{\"query\":\"%s\",\"response\":\"%s\",\"confidence\":%.2f,"
                        "\"duration_ms\":%.1f,\"status\":%d}",
                        items[i].query, escaped,
                        items[i].confidence, items[i].duration_ms,
                        items[i].status);
    }

    pos += snprintf(out_json + pos, out_capacity - pos, "]}");
    return 0;
}

/* ---------- #9. Memory management ---------- */

size_t kolibri_bridge_get_memory_usage(void) {
    size_t total = 0;

    /* Считаем память разговоров */
    for (size_t i = 0; i < KOLIBRI_BRIDGE_MAX_CONVERSATIONS; i++) {
        if (g_conversations[i].active) {
            total += sizeof(BridgeConversation);
            for (size_t j = 0; j < g_conversations[i].msg_count; j++) {
                total += strlen(g_conversations[i].messages[j]);
            }
        }
    }

    return total;
}

/* ---------- Инициализация при загрузке ---------- */

__attribute__((constructor))
static void kolibri_bridge_startup(void) {
    g_bridge_start_time = (uint64_t)(time(NULL) * 1000);
    memset(g_conversations, 0, sizeof(g_conversations));
    g_health.status = "ok";
    g_health.queries_processed = 0;
    g_health.avg_response_ms = 0.0;
    g_health.conversations_active = 0;
    g_health.memory_used_bytes = 0;
}
