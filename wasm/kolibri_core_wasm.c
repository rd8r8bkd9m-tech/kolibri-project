#include <emscripten/emscripten.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kolibri/inference.h"

static KolibriInferenceContext *g_inf_ctx = NULL;

extern void kolibri_load_initial_knowledge();

/* Функция для безопасного экранирования строк в JSON */
void safe_json_string(char *dest, const char *src, size_t max_len) {
    size_t d = 0;
    for (size_t s = 0; src[s] != '\0' && d < max_len - 4; s++) {
        unsigned char c = src[s];
        if (c == '"') { dest[d++] = '\\'; dest[d++] = '"'; }
        else if (c == '\\') { dest[d++] = '\\'; dest[d++] = '\\'; }
        else if (c == '\n') { dest[d++] = '\\'; dest[d++] = 'n'; }
        else if (c == '\r') { dest[d++] = '\\'; dest[d++] = 'r'; }
        else if (c == '\t') { dest[d++] = '\\'; dest[d++] = 't'; }
        else if (c < 32) { dest[d++] = ' '; } // Убираем управляющие символы
        else { dest[d++] = c; }
    }
    dest[d] = '\0';
}

EMSCRIPTEN_KEEPALIVE
int kolibri_bridge_init(void) {
    if (g_inf_ctx) return 0;
    g_inf_ctx = kolibri_inference_create();
    kolibri_load_initial_knowledge();
    return (g_inf_ctx != NULL) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int kolibri_bridge_query_json(const char *query, char *result_json, size_t capacity) {
    if (!g_inf_ctx || !query || !result_json || capacity < 2048) return -1;

    KolibriCognitionResult res = {0};
    int rc = kolibri_inference_think(g_inf_ctx, query, &res);

    char escaped_resp[8192];
    char escaped_thinking[4096] = "";

    if (rc != 0 || !res.response) {
        snprintf(result_json, capacity, "{\"response\":\"Ошибка инференса\",\"confidence\":0.0}");
        return -1;
    }

    safe_json_string(escaped_resp, res.response, sizeof(escaped_resp));

    // Превращаем поток цифр в строку для визуализации
    if (res.digit_stream && res.digit_count > 0) {
        size_t d = 0;
        for (size_t i = 0; i < res.digit_count && d < sizeof(escaped_thinking) - 4; i++) {
            d += snprintf(escaped_thinking + d, 4, "%d ", res.digit_stream[i]);
        }
    }

    snprintf(result_json, capacity,
        "{\"response\":\"%s\",\"confidence\":%.2f,\"method\":\"C-Core\",\"sources\":1,\"duration_ms\":%.2f,\"thinking\":\"%s\"}",
        escaped_resp, res.confidence, res.duration_ms, escaped_thinking);

    free(res.response);
    free(res.digit_stream);
    return 0;
}
