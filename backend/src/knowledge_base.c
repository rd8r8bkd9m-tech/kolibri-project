/*
 * knowledge_base.c — Интерфейс базы знаний Kolibri
 *
 * Управляет семантическим поиском (через knowledge_index.c) и
 * интеграцией с движком рассуждений (reasoning_engine.c).
 * Возвращает JSON-результаты (Top-K docs, confidence).
 *
 * Copyright (c) 2025 Kolibri OS
 */

#include "kolibri/knowledge_base.h"
#include "kolibri/knowledge_index.h"
#include "kolibri/reasoning.h"
#include "kolibri/reasoning_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static KolibriKnowledgeIndex *g_index = NULL;
static int g_kb_initialized = 0;

/* Вспомогательная функция эскейпинга JSON */
static void escape_json_string(const char* src, char* dst, size_t dst_len) {
    if (!src || !dst || dst_len == 0) return;
    size_t i = 0, j = 0;
    while (src[i] && j < dst_len - 2) {
        switch (src[i]) {
            case '"': case '\\':
                if (j < dst_len - 3) { dst[j++] = '\\'; dst[j++] = src[i]; }
                break;
            case '\n': if (j < dst_len - 3) { dst[j++] = '\\'; dst[j++] = 'n'; } break;
            case '\r': if (j < dst_len - 3) { dst[j++] = '\\'; dst[j++] = 'r'; } break;
            case '\t': if (j < dst_len - 3) { dst[j++] = '\\'; dst[j++] = 't'; } break;
            default: dst[j++] = src[i]; break;
        }
        i++;
    }
    dst[j] = '\0';
}

/**
 * Инициализация семантической базы знаний
 */
int kolibri_kb_init(const char *data_path) {
    if (g_kb_initialized) return 0;

    /* Инициализируем reasoning engine если еще нет */
    kolibri_reasoning_init();

    if (data_path) {
        const char *roots[] = { data_path };
        /* Используем семантический индекс из knowledge_index.h */
        int rc = kolibri_knowledge_index_create(roots, 1, 1000000, &g_index);
        if (rc != 0) {
            fprintf(stderr, "[KB] Error creating knowledge index from '%s'\n", data_path);
            return -1;
        }
    } else {
        /* Создаем пустой индекс */
        int rc = kolibri_knowledge_index_create(NULL, 0, 1000000, &g_index);
        if (rc != 0) return -1;
    }

    g_kb_initialized = 1;
    return 0;
}

/**
 * Очистка ресурсов
 */
void kolibri_kb_destroy(void) {
    if (g_kb_initialized) {
        if (g_index) {
            kolibri_knowledge_index_destroy(g_index);
            g_index = NULL;
        }
        g_kb_initialized = 0;
    }
}

/**
 * Семантический поиск (возвращает Top-K результатов в JSON)
 */
int kolibri_kb_search(const char *query, char *out_json, size_t out_capacity) {
    if (!g_kb_initialized || !query || !out_json || out_capacity == 0) return -1;

    size_t indices[5];
    float scores[5];
    size_t result_count = 0;

    /* Ищем до 5 релевантных документов */
    int rc = kolibri_knowledge_search(g_index, query, 5, indices, scores, &result_count);
    if (rc != 0) {
        snprintf(out_json, out_capacity, "{\"status\": -1, \"error\": \"Search failed\", \"results\": []}");
        return -1;
    }

    size_t pos = 0;
    pos += snprintf(out_json + pos, out_capacity - pos, "{\"status\": 0, \"count\": %zu, \"results\": [", result_count);

    for (size_t i = 0; i < result_count && pos < out_capacity; i++) {
        const KolibriKnowledgeDoc *doc = kolibri_knowledge_index_document(g_index, indices[i]);
        if (!doc) continue;

        char esc_title[256] = {0};
        char esc_content[2048] = {0}; /* Ограничиваем контент для JSON */

        /* Обрезаем контент для предпросмотра (до 500 символов) */
        char preview[512] = {0};
        strncpy(preview, doc->content ? doc->content : "", 500);

        escape_json_string(doc->title ? doc->title : "Unknown", esc_title, sizeof(esc_title));
        escape_json_string(preview, esc_content, sizeof(esc_content));

        int written = snprintf(out_json + pos, out_capacity - pos,
                 "%s{\"score\": %.4f, \"title\": \"%s\", \"preview\": \"%s...\"}",
                 i > 0 ? ", " : "",
                 scores[i], esc_title, esc_content);

        if (written > 0 && (size_t)written < out_capacity - pos) {
            pos += written;
        } else {
            break;
        }
    }

    if (pos < out_capacity) {
        snprintf(out_json + pos, out_capacity - pos, "]}");
    }

    return 0;
}

/**
 * Динамическое добавление факта (инъекция)
 */
int kolibri_kb_add_fact(const char *fact, double confidence, const char *source) {
    if (!fact) return -1;
    if (!g_kb_initialized) kolibri_kb_init(NULL);

    /* Добавляем факт в глобальный движок рассуждений */
    KolibriREConfig *config = kolibri_reasoning_get_config();
    int rc = kolibri_re_add_fact(config, fact, confidence, source ? source : "Runtime Injection");

    /* В идеале также нужно обновить in-memory семантический индекс g_index,
       но текущий API kolibri_knowledge_index не поддерживает динамическое добавление.
       В будущем здесь будет: kolibri_knowledge_index_add_document(g_index, ...);
    */

    return rc;
}

/**
 * Динамическое добавление правила (инъекция)
 */
int kolibri_kb_add_rule(const char *premise, const char *conclusion, int op, double strength, const char *domain) {
    if (!premise || !conclusion) return -1;
    if (!g_kb_initialized) kolibri_kb_init(NULL);

    KolibriREConfig *config = kolibri_reasoning_get_config();
    KolibriLogicalOp logic_op = (KolibriLogicalOp)op;

    /* Ограничиваем op валидным диапазоном */
    if (logic_op < 0 || logic_op >= KRE_OP_COUNT) {
        logic_op = KRE_OP_IMPLIES; /* По умолчанию импликация */
    }

    return kolibri_re_add_rule(config, premise, conclusion, logic_op, strength, domain ? domain : "general");
}
