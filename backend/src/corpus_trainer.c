/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 *
 * Corpus Trainer — Реализация масштабного обучения
 *
 * Ключевые оптимизации относительно corpus_learning.c:
 *   1. Хеш-таблица вместо линейного поиска → O(1) вместо O(n)
 *   2. Мульти-поколенческая эволюция (10+ gen вместо 1)
 *   3. Периодическая дистилляция (вытеснение + слияние)
 *   4. Полная сериализация модели
 *   5. Фиксированный размер (128K паттернов + 256K рёбер)
 */

#include "kolibri/corpus_trainer.h"
#include "kolibri/corpus.h"
#include "kolibri/semantic.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ============================================================
 * Внутренние хелперы
 * ============================================================ */

/* DJB2 хеш (детерминированный, быстрый) */
static uint32_t klm_hash(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        hash = ((hash << 5) + hash) + (uint32_t)c;
    return hash;
}

/* Быстрая генерация паттерна без эволюции (O(1), детерминированная) */
static void klm_quick_pattern(const char *word, uint8_t out[KLM_PATTERN_SIZE]) {
    uint32_t h = klm_hash(word);
    for (int i = 0; i < KLM_PATTERN_SIZE; i++) {
        out[i] = (uint8_t)(h % 10);
        h = h * 1103515245u + 12345u; /* LCG каскад */
    }
}

/* Поиск слота в хеш-таблице паттернов (open addressing, linear probing) */
static size_t klm_find_pattern_slot(const KlmPatternEntry *t, size_t cap, const char *word) {
    uint32_t h = klm_hash(word);
    size_t idx = h & (cap - 1);
    size_t probes = 0;
    while (t[idx].occupied && strcmp(t[idx].word, word) != 0) {
        idx = (idx + 1) & (cap - 1);
        if (++probes >= cap)
            return SIZE_MAX;
    }
    return idx;
}

/* Поиск слота для ребра графа знаний */
static size_t klm_find_edge_slot(const KlmEdge *t, size_t cap, uint32_t src, uint32_t tgt) {
    uint32_t h = src * 2654435761u ^ tgt;
    size_t idx = h & (cap - 1);
    size_t probes = 0;
    while (t[idx].occupied && !(t[idx].source_hash == src && t[idx].target_hash == tgt)) {
        idx = (idx + 1) & (cap - 1);
        if (++probes >= cap)
            return SIZE_MAX;
    }
    return idx;
}

/* Сходство двух паттернов по цифрам [0.0–1.0] */
static double klm_pattern_sim(const uint8_t a[KLM_PATTERN_SIZE], const uint8_t b[KLM_PATTERN_SIZE]) {
    int score = 0;
    for (int i = 0; i < KLM_PATTERN_SIZE; i++) {
        int d = abs((int)a[i] - (int)b[i]);
        if (d == 0)
            score += 2;
        else if (d == 1)
            score += 1;
    }
    return (double)score / (2.0 * KLM_PATTERN_SIZE);
}

/* Поиск слова по его хешу — O(1) через хеш-таблицу вместо линейного скана */
static const char *klm_hash_to_word(const KlmModel *m, uint32_t hash) {
    /* Пробируем от позиции hash & (cap-1). Паттерны расположены по djb2 хешу,
     * поэтому слово с данным hash скорее всего рядом со slot = hash & (cap-1).
     * Ограничиваем пробинг 64 попытками — если не нашли, значит нет. */
    size_t cap = m->pattern_capacity;
    size_t idx = hash & (cap - 1);
    for (size_t probe = 0; probe < 64 && probe < cap; probe++) {
        size_t pos = (idx + probe) & (cap - 1);
        if (!m->patterns[pos].occupied)
            return NULL;
        if (m->patterns[pos].hash == hash)
            return m->patterns[pos].word;
    }
    return NULL;
}

/* ============================================================
 * Индекс смежности — O(1) поиск рёбер по хешу слова
 * ============================================================ */

static int klm_adj_init(KlmAdjIndex *idx, size_t edge_capacity) {
    memset(idx, 0, sizeof(*idx));
    /* Бакетов = edge_capacity (степень двойки), записей = 2×edge_capacity */
    idx->bucket_count = edge_capacity;
    idx->entry_capacity = edge_capacity * 2;
    idx->entry_count = 0;

    idx->buckets = malloc(idx->bucket_count * sizeof(size_t));
    if (!idx->buckets)
        return -1;
    for (size_t i = 0; i < idx->bucket_count; i++)
        idx->buckets[i] = SIZE_MAX;

    idx->entries = malloc(idx->entry_capacity * sizeof(KlmAdjEntry));
    if (!idx->entries) {
        free(idx->buckets);
        return -1;
    }

    return 0;
}

void klm_adj_free(KlmAdjIndex *idx) {
    if (!idx)
        return;
    free(idx->buckets);
    free(idx->entries);
    memset(idx, 0, sizeof(*idx));
}

/* Добавить ребро в индекс смежности для конкретного word_hash */
static int klm_adj_insert(KlmAdjIndex *idx, uint32_t word_hash, size_t edge_slot) {
    if (idx->entry_count >= idx->entry_capacity)
        return -1;

    size_t bucket = word_hash & (idx->bucket_count - 1);
    size_t eidx = idx->entry_count++;
    idx->entries[eidx].edge_slot = edge_slot;
    idx->entries[eidx].next = idx->buckets[bucket];
    idx->buckets[bucket] = eidx;
    return 0;
}

/* Полная перестройка индекса смежности из текущих рёбер */
int klm_adj_rebuild(KlmTrainerContext *ctx) {
    if (!ctx)
        return -1;

    KlmModel *m = &ctx->model;

    /* Освобождаем старый индекс */
    klm_adj_free(&m->adj_index);

    /* Инициализируем новый */
    if (klm_adj_init(&m->adj_index, m->edge_capacity) != 0)
        return -1;

    /* Проходим по всем занятым рёбрам, добавляем 2 записи на ребро */
    for (size_t i = 0; i < m->edge_capacity; i++) {
        if (!m->edges[i].occupied)
            continue;
        klm_adj_insert(&m->adj_index, m->edges[i].source_hash, i);
        klm_adj_insert(&m->adj_index, m->edges[i].target_hash, i);
    }

    return 0;
}

/* Добавление / усиление ребра в графе знаний */
static int klm_add_edge(KlmTrainerContext *ctx, const char *w1, const char *w2) {
    if (strlen(w1) < 3 || strlen(w2) < 3)
        return 0;

    uint32_t h1 = klm_hash(w1);
    uint32_t h2 = klm_hash(w2);
    if (h1 == h2)
        return 0;

    /* Каноническое направление: меньший хеш → больший */
    uint32_t src = h1 < h2 ? h1 : h2;
    uint32_t tgt = h1 < h2 ? h2 : h1;

    KlmEdge *edges = ctx->model.edges;
    size_t cap = ctx->model.edge_capacity;

    if (ctx->model.edge_count >= (size_t)(cap * KLM_LOAD_FACTOR)) {
        /* Сначала пробуем расширить */
        if (ctx->model.edge_capacity < KLM_MAX_CAPACITY) {
            klm_rehash_edges(ctx, ctx->model.edge_capacity * KLM_GROWTH_FACTOR);
            edges = ctx->model.edges;
            cap = ctx->model.edge_capacity;
        } else {
            return -1; /* нужна дистилляция */
        }
    }

    size_t slot = klm_find_edge_slot(edges, cap, src, tgt);
    if (slot == SIZE_MAX)
        return -1;

    if (edges[slot].occupied) {
        edges[slot].cooccurrence++;
        /* Сигмоидальный рост веса: быстро вначале, плато на 1.0 */
        edges[slot].weight = 1.0f - 1.0f / (1.0f + (float)edges[slot].cooccurrence);
        edges[slot].version = ++ctx->model.global_version;
        edges[slot].dirty = 1;
    } else {
        edges[slot].source_hash = src;
        edges[slot].target_hash = tgt;
        edges[slot].weight = 0.5f;
        edges[slot].cooccurrence = 1;
        edges[slot].version = ++ctx->model.global_version;
        edges[slot].dirty = 1;
        edges[slot].occupied = 1;
        ctx->model.edge_count++;
        ctx->stats.edges_created++;

        /* Добавляем в индекс смежности (2 записи: src и tgt) */
        if (ctx->model.adj_index.buckets) {
            klm_adj_insert(&ctx->model.adj_index, src, slot);
            klm_adj_insert(&ctx->model.adj_index, tgt, slot);
        }
    }
    return 0;
}

/* Компаратор для qsort по score (по убыванию) */
typedef struct {
    uint32_t hash;
    float score;
} KlmScoreEntry;

static int klm_score_cmp_desc(const void *a, const void *b) {
    float sa = ((const KlmScoreEntry *)a)->score;
    float sb = ((const KlmScoreEntry *)b)->score;
    if (sb > sa)
        return 1;
    if (sb < sa)
        return -1;
    return 0;
}

/* ============================================================
 * API — Жизненный цикл
 * ============================================================ */

KlmTrainerConfig klm_default_config(void) {
    return (KlmTrainerConfig){.evolution_generations = 10,
                              .distill_interval = 1000,
                              .context_window = 32,
                              .min_fitness = 0.05,
                              .eviction_ratio = 0.1,
                              .merge_threshold = 0.95,
                              .verbose = false};
}

KlmTrainerContext *klm_trainer_create(const KlmTrainerConfig *config) {
    KlmTrainerContext *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;

    ctx->config = config ? *config : klm_default_config();

    /* Аллокация хеш-таблицы паттернов (динамическая, начальный размер) */
    ctx->model.pattern_capacity = KLM_INITIAL_PATTERNS;
    ctx->model.patterns = calloc(KLM_INITIAL_PATTERNS, sizeof(KlmPatternEntry));
    if (!ctx->model.patterns) {
        free(ctx);
        return NULL;
    }

    /* Аллокация хеш-таблицы рёбер (динамическая) */
    ctx->model.edge_capacity = KLM_INITIAL_EDGES;
    ctx->model.edges = calloc(KLM_INITIAL_EDGES, sizeof(KlmEdge));
    if (!ctx->model.edges) {
        free(ctx->model.patterns);
        free(ctx);
        return NULL;
    }

    ctx->model.global_version = 0;
    ctx->model.last_sync_version = 0;
    ctx->model.rehash_count = 0;

    /* Инициализация индекса смежности */
    if (klm_adj_init(&ctx->model.adj_index, KLM_INITIAL_EDGES) != 0) {
        free(ctx->model.edges);
        free(ctx->model.patterns);
        free(ctx);
        return NULL;
    }

    return ctx;
}

void klm_trainer_free(KlmTrainerContext *ctx) {
    if (!ctx)
        return;
    klm_adj_free(&ctx->model.adj_index);
    free(ctx->model.patterns);
    free(ctx->model.edges);
    free(ctx);
}

/* ============================================================
 * API — Обучение
 * ============================================================ */

/* ============================================================
 * Fast inline tokenizer — no malloc per token
 * ============================================================ */
static int klm_train_text_fast(KlmTrainerContext *ctx, const char *text, size_t len)
{
    if (!ctx || !text || len == 0) return -1;
    char token_buf[KLM_WORD_MAX];
    KlmPatternEntry *patterns = ctx->model.patterns;
    size_t cap = ctx->model.pattern_capacity;
    size_t i = 0;
    while (i < len) {
        while (i < len && (text[i] <= ' ' || (text[i] >= '!' && text[i] <= '/'))) i++;
        if (i >= len) break;
        size_t start = i;
        while (i < len && text[i] > ' ' && !(text[i] >= '!' && text[i] <= '/')) i++;
        size_t wlen = i - start;
        if (wlen < 2 || wlen >= KLM_WORD_MAX) continue;
        memcpy(token_buf, text + start, wlen);
        token_buf[wlen] = '\0';
        if (ctx->model.pattern_count >= (size_t)(cap * KLM_LOAD_FACTOR)) {
            if (ctx->model.pattern_capacity < KLM_MAX_CAPACITY) {
                klm_rehash_patterns(ctx, ctx->model.pattern_capacity * KLM_GROWTH_FACTOR);
                cap = ctx->model.pattern_capacity;
                patterns = ctx->model.patterns;
            } else { klm_distill(ctx); }
        }
        size_t slot = klm_find_pattern_slot(patterns, cap, token_buf);
        if (slot == SIZE_MAX) continue;
        if (patterns[slot].occupied) {
            patterns[slot].frequency++;
            patterns[slot].version = ++ctx->model.global_version;
            patterns[slot].dirty = 1;
        } else {
            patterns[slot].hash = klm_hash(token_buf);
            strncpy(patterns[slot].word, token_buf, KLM_WORD_MAX - 1);
            patterns[slot].word[KLM_WORD_MAX - 1] = '\0';
            klm_quick_pattern(token_buf, patterns[slot].pattern);
            patterns[slot].fitness = 0.1f;
            ctx->model.pattern_count++;
        }
    }
    return 0;
}

int klm_train_text(KlmTrainerContext *ctx, const char *text, size_t len) {
    if (!ctx || !text || len == 0)
        return -1;

    /* Токенизация через существующий модуль corpus */
    char **tokens = NULL;
    size_t token_count = 0;
    if (k_corpus_tokenize(text, len, &tokens, &token_count) != 0)
        return -1;
    if (token_count == 0) {
        k_corpus_free_tokens(tokens, 0);
        return 0;
    }

    KlmPatternEntry *patterns = ctx->model.patterns;
    size_t cap = ctx->model.pattern_capacity;

    /* Проверка заполнения — автоматическое расширение или дистилляция */
    if (ctx->model.pattern_count >= (size_t)(cap * KLM_LOAD_FACTOR)) {
        /* Сначала пытаемся расширить таблицу */
        if (ctx->model.pattern_capacity < KLM_MAX_CAPACITY) {
            if (ctx->config.verbose)
                fprintf(stderr, "[Train] Авто-расширение паттернов: %zu → %zu\n", ctx->model.pattern_capacity,
                        ctx->model.pattern_capacity * KLM_GROWTH_FACTOR);
            klm_rehash_patterns(ctx, ctx->model.pattern_capacity * KLM_GROWTH_FACTOR);
            cap = ctx->model.pattern_capacity;
            patterns = ctx->model.patterns;
        } else {
            /* Абсолютный предел — дистилляция */
            if (ctx->config.verbose)
                fprintf(stderr, "[Train] Авто-дистилляция при %zu/%zu паттернов\n", ctx->model.pattern_count, cap);
            klm_distill(ctx);
        }
    }

    /* --- Фаза 1: Обучение паттернов --- */
    for (size_t i = 0; i < token_count; i++) {
        if (strlen(tokens[i]) < 2)
            continue;

        size_t slot = klm_find_pattern_slot(patterns, cap, tokens[i]);
        if (slot == SIZE_MAX)
            continue;

        if (patterns[slot].occupied) {
            /* Существующее слово: обновляем частоту */
            patterns[slot].frequency++;
            patterns[slot].version = ++ctx->model.global_version;
            patterns[slot].dirty = 1;
        } else {
            /* Новое слово: генерируем паттерн */
            patterns[slot].hash = klm_hash(tokens[i]);
            strncpy(patterns[slot].word, tokens[i], KLM_WORD_MAX - 1);
            patterns[slot].word[KLM_WORD_MAX - 1] = '\0';

            /* Быстрый паттерн по умолчанию */
            klm_quick_pattern(tokens[i], patterns[slot].pattern);
            patterns[slot].fitness = 0.1f;

            /* Эволюционное обучение с контекстом (если включено) */
            if (ctx->config.evolution_generations > 0) {
                KolibriSemanticContext sctx;
                if (k_semantic_context_init(&sctx) == 0) {
                    size_t win = ctx->config.context_window;
                    size_t start = (i >= win) ? i - win : 0;
                    size_t end = (i + win < token_count) ? i + win : token_count;
                    for (size_t j = start; j < end; j++) {
                        if (j == i)
                            continue;
                        double rel = 1.0 / (1.0 + fabs((double)j - (double)i));
                        k_semantic_context_add_word(&sctx, tokens[j], rel);
                    }
                    KolibriSemanticPattern spat;
                    k_semantic_pattern_init(&spat);
                    if (k_semantic_learn(tokens[i], &sctx, ctx->config.evolution_generations, &spat) == 0) {
                        memcpy(patterns[slot].pattern, spat.pattern, KLM_PATTERN_SIZE);
                        patterns[slot].fitness = (float)spat.context_weight;
                    }
                    k_semantic_pattern_free(&spat);
                    k_semantic_context_free(&sctx);
                }
            }

            patterns[slot].frequency = 1;
            patterns[slot].last_epoch = ctx->model.current_epoch;
            patterns[slot].version = ++ctx->model.global_version;
            patterns[slot].dirty = 1;
            patterns[slot].occupied = 1;
            ctx->model.pattern_count++;
            ctx->stats.patterns_learned++;
        }
    }

    /* --- Фаза 2: Граф знаний (связи между словами) --- */
    size_t edge_window = 5;
    int edge_overflow = 0; /* флаг: рёбра переполнены и дистилляция не помогла */
    for (size_t i = 0; i < token_count && !edge_overflow; i++) {
        if (strlen(tokens[i]) < 3)
            continue;
        size_t end = (i + edge_window < token_count) ? i + edge_window : token_count;
        for (size_t j = i + 1; j < end; j++) {
            if (strlen(tokens[j]) < 3)
                continue;
            if (klm_add_edge(ctx, tokens[i], tokens[j]) == -1) {
                /* Переполнение рёбер — одна попытка дистилляции */
                size_t evicted = klm_distill(ctx);
                if (evicted > 0) {
                    klm_add_edge(ctx, tokens[i], tokens[j]);
                } else {
                    /* Дистилляция не помогла — таблица полна сильных рёбер */
                    edge_overflow = 1;
                    break;
                }
            }
        }
    }

    /* Обновление статистики */
    ctx->stats.documents_processed++;
    ctx->stats.tokens_total += token_count;
    ctx->model.documents_trained++;
    ctx->model.tokens_processed += token_count;

    /* Периодическая дистилляция */
    if (ctx->config.distill_interval > 0 && ctx->stats.documents_processed % ctx->config.distill_interval == 0) {
        klm_distill(ctx);
    }

    k_corpus_free_tokens(tokens, token_count);
    return 0;
}

int klm_train_document(KlmTrainerContext *ctx, const char *title, const char *text) {
    if (!ctx || !text)
        return -1;

    /* Обучаем на заголовке (если есть) */
    if (title && strlen(title) > 0)
        klm_train_text(ctx, title, strlen(title));

    /* Обучаем на теле документа */
    return klm_train_text(ctx, text, strlen(text));
}

int klm_train_file(KlmTrainerContext *ctx, const char *filepath) {
    if (!ctx || !filepath)
        return -1;

    FILE *f = fopen(filepath, "r");
    if (!f)
        return -1;

    /* Fast path: read line-by-line instead of loading entire file */
    char line[4096];
    int line_count = 0;
    int trained = 0;

    while (fgets(line, sizeof(line), f)) {
        line_count++;
        /* Skip short lines, headers, separators */
        if (strlen(line) < 4)
            continue;
        if (line[0] == '#' && line[1] == '#')
            continue; /* markdown headers */
        if (line[0] == '-' && line[1] == '-' && line[2] == '-')
            continue; /* separator */

        int result = klm_train_text_fast(ctx, line, strlen(line));
        if (result == 0)
            trained++;
    }
    fclose(f);

    if (ctx->config.verbose)
        fprintf(stderr, "[Train] File: %s (%d lines, %d trained)\n", filepath, line_count, trained);

    return 0;
}

size_t klm_train_directory(KlmTrainerContext *ctx, const char *dirpath) {
    if (!ctx || !dirpath)
        return 0;

    DIR *dir = opendir(dirpath);
    if (!dir)
        return 0;

    size_t count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dirpath, entry->d_name);

        struct stat st;
        if (stat(path, &st) != 0)
            continue;

        if (S_ISREG(st.st_mode)) {
            size_t len = strlen(entry->d_name);
            bool is_text = false;
            if (len > 4 && strcmp(entry->d_name + len - 4, ".txt") == 0)
                is_text = true;
            if (len > 3 && strcmp(entry->d_name + len - 3, ".md") == 0)
                is_text = true;
            if (len > 5 && strcmp(entry->d_name + len - 5, ".html") == 0)
                is_text = true;

            if (is_text && klm_train_file(ctx, path) == 0)
                count++;
        } else if (S_ISDIR(st.st_mode)) {
            count += klm_train_directory(ctx, path);
        }
    }

    closedir(dir);

    if (ctx->config.verbose)
        fprintf(stderr, "[Train] Директория %s: %zu файлов\n", dirpath, count);

    return count;
}

/* ============================================================
 * API — Дистилляция знаний
 * ============================================================ */

size_t klm_distill(KlmTrainerContext *ctx) {
    if (!ctx)
        return 0;

    KlmModel *m = &ctx->model;
    size_t evicted_patterns = 0;
    size_t evicted_edges = 0;

    /* --- Фаза 1: Вытеснение слабых паттернов --- */
    {
        /* Вычисляем score = fitness × log(frequency + 1) */
        double sum_score = 0;
        size_t active = 0;
        for (size_t i = 0; i < m->pattern_capacity; i++) {
            if (!m->patterns[i].occupied)
                continue;
            double s = (double)m->patterns[i].fitness * log((double)m->patterns[i].frequency + 1.0);
            sum_score += s;
            active++;
        }

        if (active > 0) {
            double mean_score = sum_score / active;
            /* Порог: вытесняем паттерны с score < mean × eviction_ratio */
            double threshold = mean_score * ctx->config.eviction_ratio;

            for (size_t i = 0; i < m->pattern_capacity; i++) {
                if (!m->patterns[i].occupied)
                    continue;
                double s = (double)m->patterns[i].fitness * log((double)m->patterns[i].frequency + 1.0);
                if (s < threshold) {
                    m->patterns[i].occupied = 0;
                    evicted_patterns++;
                }
            }
        }
    }

    /* Перестройка хеш-таблицы паттернов (устранение "дыр") */
    if (evicted_patterns > 0) {
        KlmPatternEntry *old = m->patterns;
        m->patterns = calloc(m->pattern_capacity, sizeof(KlmPatternEntry));
        if (!m->patterns) {
            m->patterns = old;
            return 0;
        }
        m->pattern_count = 0;

        for (size_t i = 0; i < m->pattern_capacity; i++) {
            if (!old[i].occupied)
                continue;
            size_t slot = klm_find_pattern_slot(m->patterns, m->pattern_capacity, old[i].word);
            if (slot != SIZE_MAX) {
                m->patterns[slot] = old[i];
                m->pattern_count++;
            }
        }
        free(old);
    }

    /* --- Фаза 2: Вытеснение слабых рёбер --- */
    {
        /* Процентильная стратегия: удаляем нижние 30% по score = weight × log(cooc+1) */
        size_t active_edges = 0;
        double edge_score_sum = 0;
        for (size_t i = 0; i < m->edge_capacity; i++) {
            if (!m->edges[i].occupied)
                continue;
            double s = (double)m->edges[i].weight * log((double)m->edges[i].cooccurrence + 1.0);
            edge_score_sum += s;
            active_edges++;
        }
        if (active_edges > 0) {
            double mean_edge_score = edge_score_sum / active_edges;
            double edge_threshold = mean_edge_score * ctx->config.eviction_ratio;
            for (size_t i = 0; i < m->edge_capacity; i++) {
                if (!m->edges[i].occupied)
                    continue;
                double s = (double)m->edges[i].weight * log((double)m->edges[i].cooccurrence + 1.0);
                if (s < edge_threshold) {
                    m->edges[i].occupied = 0;
                    evicted_edges++;
                }
            }
        }
    }

    /* Перестройка хеш-таблицы рёбер */
    if (evicted_edges > 0) {
        KlmEdge *old = m->edges;
        m->edges = calloc(m->edge_capacity, sizeof(KlmEdge));
        if (!m->edges) {
            m->edges = old;
            return evicted_patterns;
        }
        m->edge_count = 0;

        for (size_t i = 0; i < m->edge_capacity; i++) {
            if (!old[i].occupied)
                continue;
            size_t slot = klm_find_edge_slot(m->edges, m->edge_capacity, old[i].source_hash, old[i].target_hash);
            if (slot != SIZE_MAX) {
                m->edges[slot] = old[i];
                m->edge_count++;
            }
        }
        free(old);
    }

    /* --- Фаза 3: Обновление статистики --- */
    m->current_epoch++;
    ctx->stats.patterns_evicted += evicted_patterns;
    ctx->stats.edges_evicted += evicted_edges;
    ctx->stats.distillation_runs++;

    /* Пересчёт средних */
    double fitness_sum = 0;
    size_t fitness_count = 0;
    for (size_t i = 0; i < m->pattern_capacity; i++) {
        if (m->patterns[i].occupied) {
            fitness_sum += m->patterns[i].fitness;
            fitness_count++;
        }
    }
    m->avg_pattern_fitness = fitness_count > 0 ? fitness_sum / fitness_count : 0;

    double weight_sum = 0;
    size_t weight_count = 0;
    for (size_t i = 0; i < m->edge_capacity; i++) {
        if (m->edges[i].occupied) {
            weight_sum += m->edges[i].weight;
            weight_count++;
        }
    }
    m->avg_edge_weight = weight_count > 0 ? weight_sum / weight_count : 0;

    if (ctx->config.verbose && (evicted_patterns > 0 || evicted_edges > 0))
        fprintf(stderr,
                "[Distill] эпоха=%u паттерны: -%zu→%zu  "
                "рёбра: -%zu→%zu  avg_fitness=%.3f\n",
                m->current_epoch, evicted_patterns, m->pattern_count, evicted_edges, m->edge_count,
                m->avg_pattern_fitness);

    /* Перестройка индекса смежности после дистилляции */
    if (evicted_edges > 0)
        klm_adj_rebuild(ctx);

    return evicted_patterns + evicted_edges;
}

/* ============================================================
 * API — Сериализация (.klm формат)
 * ============================================================ */

int klm_save(const KlmTrainerContext *ctx, const char *filepath) {
    if (!ctx || !filepath)
        return -1;

    FILE *f = fopen(filepath, "wb");
    if (!f)
        return -1;

    /* --- Заголовок --- */
    uint32_t magic = KLM_MAGIC;
    uint32_t version = KLM_VERSION;
    uint64_t timestamp = (uint64_t)time(NULL);
    fwrite(&magic, 4, 1, f);
    fwrite(&version, 4, 1, f);
    fwrite(&timestamp, 8, 1, f);

    /* --- Метаданные --- */
    uint32_t pc = (uint32_t)ctx->model.pattern_count;
    uint32_t ec = (uint32_t)ctx->model.edge_count;
    uint32_t epoch = ctx->model.current_epoch;
    uint64_t docs = ctx->model.documents_trained;
    uint64_t toks = ctx->model.tokens_processed;
    fwrite(&pc, 4, 1, f);
    fwrite(&ec, 4, 1, f);
    fwrite(&epoch, 4, 1, f);
    fwrite(&docs, 8, 1, f);
    fwrite(&toks, 8, 1, f);

    /* --- Паттерны (только занятые слоты) --- */
    for (size_t i = 0; i < ctx->model.pattern_capacity; i++) {
        if (ctx->model.patterns[i].occupied)
            fwrite(&ctx->model.patterns[i], sizeof(KlmPatternEntry), 1, f);
    }

    /* --- Рёбра (только занятые слоты) --- */
    for (size_t i = 0; i < ctx->model.edge_capacity; i++) {
        if (ctx->model.edges[i].occupied)
            fwrite(&ctx->model.edges[i], sizeof(KlmEdge), 1, f);
    }

    fclose(f);

    if (ctx->config.verbose)
        fprintf(stderr, "[Save] %s: %u паттернов, %u рёбер\n", filepath, pc, ec);

    return 0;
}

int klm_load(KlmTrainerContext *ctx, const char *filepath) {
    if (!ctx || !filepath)
        return -1;

    FILE *f = fopen(filepath, "rb");
    if (!f)
        return -1;

    /* --- Заголовок --- */
    uint32_t magic, version;
    uint64_t timestamp;
    if (fread(&magic, 4, 1, f) != 1 || fread(&version, 4, 1, f) != 1 || fread(&timestamp, 8, 1, f) != 1) {
        fclose(f);
        return -1;
    }

    if (magic != KLM_MAGIC || version != KLM_VERSION) {
        fclose(f);
        fprintf(stderr, "[Load] Неверный формат: magic=0x%08X version=%u\n", magic, version);
        return -1;
    }

    /* --- Метаданные --- */
    uint32_t pc, ec, epoch;
    uint64_t docs, toks;
    if (fread(&pc, 4, 1, f) != 1 || fread(&ec, 4, 1, f) != 1 || fread(&epoch, 4, 1, f) != 1 ||
        fread(&docs, 8, 1, f) != 1 || fread(&toks, 8, 1, f) != 1) {
        fclose(f);
        return -1;
    }

    /* Очистка текущих данных */
    memset(ctx->model.patterns, 0, ctx->model.pattern_capacity * sizeof(KlmPatternEntry));
    memset(ctx->model.edges, 0, ctx->model.edge_capacity * sizeof(KlmEdge));
    ctx->model.pattern_count = 0;
    ctx->model.edge_count = 0;

    /* --- Загрузка паттернов (с перехешированием) --- */
    for (uint32_t i = 0; i < pc; i++) {
        KlmPatternEntry entry;
        if (fread(&entry, sizeof(KlmPatternEntry), 1, f) != 1)
            break;
        size_t slot = klm_find_pattern_slot(ctx->model.patterns, ctx->model.pattern_capacity, entry.word);
        if (slot != SIZE_MAX) {
            ctx->model.patterns[slot] = entry;
            ctx->model.patterns[slot].occupied = 1;
            ctx->model.pattern_count++;
        }
    }

    /* --- Загрузка рёбер (с перехешированием) --- */
    for (uint32_t i = 0; i < ec; i++) {
        KlmEdge edge;
        if (fread(&edge, sizeof(KlmEdge), 1, f) != 1)
            break;
        size_t slot =
            klm_find_edge_slot(ctx->model.edges, ctx->model.edge_capacity, edge.source_hash, edge.target_hash);
        if (slot != SIZE_MAX) {
            ctx->model.edges[slot] = edge;
            ctx->model.edges[slot].occupied = 1;
            ctx->model.edge_count++;
        }
    }

    ctx->model.current_epoch = epoch;
    ctx->model.documents_trained = docs;
    ctx->model.tokens_processed = toks;

    fclose(f);

    /* Перестройка индекса смежности после загрузки */
    klm_adj_rebuild(ctx);

    if (ctx->config.verbose)
        fprintf(stderr, "[Load] %s: %zu паттернов, %zu рёбер, эпоха %u\n", filepath, ctx->model.pattern_count,
                ctx->model.edge_count, epoch);

    return 0;
}

/* ============================================================
 * API — Запросы к модели
 * ============================================================ */

int klm_query_similar(const KlmTrainerContext *ctx, const char *word, char results[][KLM_WORD_MAX], float *scores,
                      size_t max_results) {
    if (!ctx || !word || !results || !scores || max_results == 0)
        return 0;

    /* Находим паттерн искомого слова */
    size_t slot = klm_find_pattern_slot(ctx->model.patterns, ctx->model.pattern_capacity, word);
    if (slot == SIZE_MAX || !ctx->model.patterns[slot].occupied)
        return 0;

    const uint8_t *target = ctx->model.patterns[slot].pattern;

    /* Сбор кандидатов с подсчётом сходства */
    KlmScoreEntry *cands = malloc(ctx->model.pattern_count * sizeof(KlmScoreEntry));
    if (!cands)
        return 0;
    size_t nc = 0;

    for (size_t i = 0; i < ctx->model.pattern_capacity; i++) {
        if (!ctx->model.patterns[i].occupied)
            continue;
        if (i == slot)
            continue;
        double sim = klm_pattern_sim(target, ctx->model.patterns[i].pattern);
        if (sim > 0.3) { /* минимальный порог */
            cands[nc].hash = (uint32_t)i;
            cands[nc].score = (float)sim;
            nc++;
        }
    }

    qsort(cands, nc, sizeof(KlmScoreEntry), klm_score_cmp_desc);

    int found = 0;
    for (size_t i = 0; i < nc && (size_t)found < max_results; i++) {
        size_t idx = cands[i].hash;
        strncpy(results[found], ctx->model.patterns[idx].word, KLM_WORD_MAX - 1);
        results[found][KLM_WORD_MAX - 1] = '\0';
        scores[found] = cands[i].score;
        found++;
    }

    free(cands);
    return found;
}

double klm_word_similarity(const KlmTrainerContext *ctx, const char *w1, const char *w2) {
    if (!ctx || !w1 || !w2)
        return 0.0;

    size_t s1 = klm_find_pattern_slot(ctx->model.patterns, ctx->model.pattern_capacity, w1);
    size_t s2 = klm_find_pattern_slot(ctx->model.patterns, ctx->model.pattern_capacity, w2);

    if (s1 == SIZE_MAX || s2 == SIZE_MAX)
        return 0.0;
    if (!ctx->model.patterns[s1].occupied || !ctx->model.patterns[s2].occupied)
        return 0.0;

    /* Комбинация: сходство паттернов + наличие ребра */
    double pat_sim = klm_pattern_sim(ctx->model.patterns[s1].pattern, ctx->model.patterns[s2].pattern);

    /* Проверяем связь в графе */
    uint32_t h1 = ctx->model.patterns[s1].hash;
    uint32_t h2 = ctx->model.patterns[s2].hash;
    uint32_t src = h1 < h2 ? h1 : h2;
    uint32_t tgt = h1 < h2 ? h2 : h1;

    size_t eslot = klm_find_edge_slot(ctx->model.edges, ctx->model.edge_capacity, src, tgt);
    double graph_sim = 0.0;
    if (eslot != SIZE_MAX && ctx->model.edges[eslot].occupied)
        graph_sim = ctx->model.edges[eslot].weight;

    /* Итоговая оценка: 40% паттерн + 60% граф */
    return 0.4 * pat_sim + 0.6 * graph_sim;
}

size_t klm_get_associations(const KlmTrainerContext *ctx, const char *word, char results[][KLM_WORD_MAX],
                            float *weights, size_t max_results) {
    if (!ctx || !word || !results || !weights || max_results == 0)
        return 0;

    uint32_t h = klm_hash(word);

    /* Сбор рёбер через индекс смежности — O(degree) вместо O(edge_capacity) */
    KlmScoreEntry *cands = malloc(512 * sizeof(KlmScoreEntry));
    if (!cands)
        return 0;
    size_t nc = 0;

    const KlmAdjIndex *adj = &ctx->model.adj_index;
    if (adj->buckets && adj->bucket_count > 0) {
        /* Быстрый путь: используем индекс смежности */
        size_t bucket = h & (adj->bucket_count - 1);
        size_t eidx = adj->buckets[bucket];
        while (eidx != SIZE_MAX && nc < 512) {
            size_t slot = adj->entries[eidx].edge_slot;
            if (slot < ctx->model.edge_capacity && ctx->model.edges[slot].occupied) {
                uint32_t other = 0;
                if (ctx->model.edges[slot].source_hash == h)
                    other = ctx->model.edges[slot].target_hash;
                else if (ctx->model.edges[slot].target_hash == h)
                    other = ctx->model.edges[slot].source_hash;

                if (other != 0) {
                    cands[nc].hash = other;
                    cands[nc].score = ctx->model.edges[slot].weight;
                    nc++;
                }
            }
            eidx = adj->entries[eidx].next;
        }
    } else {
        /* Фоллбэк: линейный скан (если индекс не построен) */
        for (size_t i = 0; i < ctx->model.edge_capacity && nc < 512; i++) {
            if (!ctx->model.edges[i].occupied)
                continue;

            uint32_t other = 0;
            if (ctx->model.edges[i].source_hash == h)
                other = ctx->model.edges[i].target_hash;
            else if (ctx->model.edges[i].target_hash == h)
                other = ctx->model.edges[i].source_hash;
            else
                continue;

            cands[nc].hash = other;
            cands[nc].score = ctx->model.edges[i].weight;
            nc++;
        }
    }

    qsort(cands, nc, sizeof(KlmScoreEntry), klm_score_cmp_desc);

    size_t found = 0;
    for (size_t i = 0; i < nc && found < max_results; i++) {
        const char *w = klm_hash_to_word(&ctx->model, cands[i].hash);
        if (w) {
            strncpy(results[found], w, KLM_WORD_MAX - 1);
            results[found][KLM_WORD_MAX - 1] = '\0';
            weights[found] = cands[i].score;
            found++;
        }
    }

    free(cands);
    return found;
}

int klm_answer(const KlmTrainerContext *ctx, const char *question, char *answer, size_t answer_max) {
    if (!ctx || !question || !answer || answer_max < 2)
        return -1;

    /* Токенизируем вопрос */
    char **tokens = NULL;
    size_t tc = 0;
    if (k_corpus_tokenize(question, strlen(question), &tokens, &tc) != 0)
        return -1;
    if (tc == 0) {
        k_corpus_free_tokens(tokens, 0);
        return -1;
    }

    /* Хеши слов вопроса (для фильтрации) */
    uint32_t *q_hashes = malloc(tc * sizeof(uint32_t));
    if (!q_hashes) {
        k_corpus_free_tokens(tokens, tc);
        return -1;
    }
    for (size_t i = 0; i < tc; i++)
        q_hashes[i] = klm_hash(tokens[i]);

    /* Агрегация ассоциаций от всех слов вопроса */
    size_t max_cands = 2048;
    KlmScoreEntry *cands = calloc(max_cands, sizeof(KlmScoreEntry));
    if (!cands) {
        free(q_hashes);
        k_corpus_free_tokens(tokens, tc);
        return -1;
    }
    size_t nc = 0;

    for (size_t t = 0; t < tc; t++) {
        if (strlen(tokens[t]) < 3)
            continue;
        uint32_t h = q_hashes[t];

        const KlmAdjIndex *adj = &ctx->model.adj_index;
        if (adj->buckets && adj->bucket_count > 0) {
            /* Быстрый путь: O(degree) через индекс смежности */
            size_t bucket = h & (adj->bucket_count - 1);
            size_t eidx = adj->buckets[bucket];
            while (eidx != SIZE_MAX) {
                size_t slot = adj->entries[eidx].edge_slot;
                if (slot < ctx->model.edge_capacity && ctx->model.edges[slot].occupied) {
                    uint32_t other = 0;
                    if (ctx->model.edges[slot].source_hash == h)
                        other = ctx->model.edges[slot].target_hash;
                    else if (ctx->model.edges[slot].target_hash == h)
                        other = ctx->model.edges[slot].source_hash;

                    if (other != 0) {
                        /* Пропускаем слова вопроса */
                        bool is_q = false;
                        for (size_t q = 0; q < tc; q++) {
                            if (q_hashes[q] == other) {
                                is_q = true;
                                break;
                            }
                        }
                        if (!is_q) {
                            /* Добавляем или усиливаем кандидата */
                            bool found = false;
                            for (size_t c = 0; c < nc; c++) {
                                if (cands[c].hash == other) {
                                    cands[c].score += ctx->model.edges[slot].weight;
                                    found = true;
                                    break;
                                }
                            }
                            if (!found && nc < max_cands) {
                                cands[nc].hash = other;
                                cands[nc].score = ctx->model.edges[slot].weight;
                                nc++;
                            }
                        }
                    }
                }
                eidx = adj->entries[eidx].next;
            }
        } else {
            /* Фоллбэк: линейный скан */
            for (size_t i = 0; i < ctx->model.edge_capacity; i++) {
                if (!ctx->model.edges[i].occupied)
                    continue;

                uint32_t other = 0;
                if (ctx->model.edges[i].source_hash == h)
                    other = ctx->model.edges[i].target_hash;
                else if (ctx->model.edges[i].target_hash == h)
                    other = ctx->model.edges[i].source_hash;
                else
                    continue;

                bool is_q = false;
                for (size_t q = 0; q < tc; q++) {
                    if (q_hashes[q] == other) {
                        is_q = true;
                        break;
                    }
                }
                if (is_q)
                    continue;

                bool found = false;
                for (size_t c = 0; c < nc; c++) {
                    if (cands[c].hash == other) {
                        cands[c].score += ctx->model.edges[i].weight;
                        found = true;
                        break;
                    }
                }
                if (!found && nc < max_cands) {
                    cands[nc].hash = other;
                    cands[nc].score = ctx->model.edges[i].weight;
                    nc++;
                }
            }
        }
    }

    /* Сортировка по агрегированному score */
    qsort(cands, nc, sizeof(KlmScoreEntry), klm_score_cmp_desc);

    /* Собираем ответ из top-10 слов */
    answer[0] = '\0';
    size_t pos = 0;
    size_t max_words = 10;
    for (size_t i = 0; i < nc && i < max_words; i++) {
        const char *w = klm_hash_to_word(&ctx->model, cands[i].hash);
        if (!w)
            continue;
        size_t wlen = strlen(w);
        if (pos + wlen + 2 >= answer_max)
            break;
        if (pos > 0)
            answer[pos++] = ' ';
        memcpy(answer + pos, w, wlen);
        pos += wlen;
    }
    answer[pos] = '\0';

    free(cands);
    free(q_hashes);
    k_corpus_free_tokens(tokens, tc);

    return (pos > 0) ? 0 : -1;
}

/* --- Числовой формат ответа: текст → цифры (3 цифры на байт UTF-8) --- */
int klm_answer_digits(const KlmTrainerContext *ctx, const char *question, uint8_t *digits, size_t digits_max,
                      size_t *digits_out) {
    char answer[4096];
    if (klm_answer(ctx, question, answer, sizeof(answer)) != 0) {
        if (digits_out)
            *digits_out = 0;
        return -1;
    }
    size_t text_len = strlen(answer);
    size_t need = text_len * 3;
    if (need > digits_max)
        need = (digits_max / 3) * 3;
    size_t pos = 0;
    for (size_t i = 0; i < text_len && pos + 3 <= digits_max; ++i) {
        uint8_t b = (uint8_t)answer[i];
        digits[pos++] = b / 100;
        digits[pos++] = (b / 10) % 10;
        digits[pos++] = b % 10;
    }
    if (digits_out)
        *digits_out = pos;
    return 0;
}

/* ============================================================
 * API — Статистика
 * ============================================================ */

KlmTrainerStats klm_get_stats(const KlmTrainerContext *ctx) {
    if (!ctx)
        return (KlmTrainerStats){0};

    KlmTrainerStats s = ctx->stats;
    s.avg_fitness = ctx->model.avg_pattern_fitness;
    s.model_size_mb = klm_model_size_mb(ctx);
    return s;
}

double klm_model_size_mb(const KlmTrainerContext *ctx) {
    if (!ctx)
        return 0.0;
    double patterns_mb = (double)(ctx->model.pattern_count * sizeof(KlmPatternEntry)) / (1024.0 * 1024.0);
    double edges_mb = (double)(ctx->model.edge_count * sizeof(KlmEdge)) / (1024.0 * 1024.0);
    return patterns_mb + edges_mb;
}

void klm_print_stats(const KlmTrainerContext *ctx) {
    if (!ctx)
        return;

    const KlmModel *m = &ctx->model;
    const KlmTrainerStats *s = &ctx->stats;

    fprintf(stderr,
            "\n╔══════════════════════════════════════════╗\n"
            "║    Kolibri Learning Model — Статистика   ║\n"
            "╠══════════════════════════════════════════╣\n"
            "║ Документов обучено:  %10zu          ║\n"
            "║ Токенов обработано:  %10zu          ║\n"
            "║ Эпоха дистилляции:  %10u          ║\n"
            "╠══════════════════════════════════════════╣\n"
            "║ Паттернов в модели:  %6zu / %-6zu     ║\n"
            "║ Рёбер в графе:       %6zu / %-6zu     ║\n"
            "║ Паттернов вытеснено: %10zu          ║\n"
            "║ Рёбер вытеснено:     %10zu          ║\n"
            "╠══════════════════════════════════════════╣\n"
            "║ Средний fitness:     %10.4f          ║\n"
            "║ Средний вес ребра:   %10.4f          ║\n"
            "║ Размер модели:       %7.2f МБ        ║\n"
            "║ Рехеширований:       %10zu          ║\n"
            "║ Глоб. версия:        %10llu          ║\n"
            "╠══════════════════════════════════════════╣\n"
            "║ Сжатие: %.0f документов → %.1f МБ      ║\n"
            "╚══════════════════════════════════════════╝\n",
            s->documents_processed, s->tokens_total, m->current_epoch, m->pattern_count, m->pattern_capacity,
            m->edge_count, m->edge_capacity, s->patterns_evicted, s->edges_evicted, m->avg_pattern_fitness,
            m->avg_edge_weight, klm_model_size_mb(ctx), m->rehash_count, (unsigned long long)m->global_version,
            (double)m->documents_trained, klm_model_size_mb(ctx));
}

/* ============================================================
 * API — Динамическое рехеширование (снятие лимитов)
 * ============================================================ */

static size_t klm_next_pow2(size_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
}

int klm_rehash_patterns(KlmTrainerContext *ctx, size_t new_capacity) {
    if (!ctx)
        return -1;

    new_capacity = klm_next_pow2(new_capacity);
    if (new_capacity <= ctx->model.pattern_capacity)
        return 0;
    if (new_capacity > KLM_MAX_CAPACITY)
        new_capacity = KLM_MAX_CAPACITY;

    KlmPatternEntry *old = ctx->model.patterns;
    size_t old_cap = ctx->model.pattern_capacity;

    KlmPatternEntry *new_table = calloc(new_capacity, sizeof(KlmPatternEntry));
    if (!new_table)
        return -1;

    /* Перенос всех занятых слотов */
    size_t moved = 0;
    for (size_t i = 0; i < old_cap; i++) {
        if (!old[i].occupied)
            continue;
        size_t slot = klm_find_pattern_slot(new_table, new_capacity, old[i].word);
        if (slot == SIZE_MAX) {
            /* Практически невозможно при увеличении capacity */
            free(new_table);
            return -1;
        }
        new_table[slot] = old[i];
        moved++;
    }

    free(old);
    ctx->model.patterns = new_table;
    ctx->model.pattern_capacity = new_capacity;
    ctx->model.pattern_count = moved;
    ctx->model.rehash_count++;

    return 0;
}

int klm_rehash_edges(KlmTrainerContext *ctx, size_t new_capacity) {
    if (!ctx)
        return -1;

    new_capacity = klm_next_pow2(new_capacity);
    if (new_capacity <= ctx->model.edge_capacity)
        return 0;
    if (new_capacity > KLM_MAX_CAPACITY)
        new_capacity = KLM_MAX_CAPACITY;

    KlmEdge *old = ctx->model.edges;
    size_t old_cap = ctx->model.edge_capacity;

    KlmEdge *new_table = calloc(new_capacity, sizeof(KlmEdge));
    if (!new_table)
        return -1;

    size_t moved = 0;
    for (size_t i = 0; i < old_cap; i++) {
        if (!old[i].occupied)
            continue;
        size_t slot = klm_find_edge_slot(new_table, new_capacity, old[i].source_hash, old[i].target_hash);
        if (slot == SIZE_MAX) {
            free(new_table);
            return -1;
        }
        new_table[slot] = old[i];
        moved++;
    }

    free(old);
    ctx->model.edges = new_table;
    ctx->model.edge_capacity = new_capacity;
    ctx->model.edge_count = moved;
    ctx->model.rehash_count++;

    /* Перестройка индекса смежности после рехеширования */
    klm_adj_rebuild(ctx);

    return 0;
}

int klm_auto_grow(KlmTrainerContext *ctx) {
    if (!ctx)
        return 0;
    int grown = 0;

    if (ctx->model.pattern_count >= (size_t)(ctx->model.pattern_capacity * KLM_LOAD_FACTOR)) {
        if (ctx->model.pattern_capacity < KLM_MAX_CAPACITY) {
            if (klm_rehash_patterns(ctx, ctx->model.pattern_capacity * KLM_GROWTH_FACTOR) == 0)
                grown++;
        }
    }

    if (ctx->model.edge_count >= (size_t)(ctx->model.edge_capacity * KLM_LOAD_FACTOR)) {
        if (ctx->model.edge_capacity < KLM_MAX_CAPACITY) {
            if (klm_rehash_edges(ctx, ctx->model.edge_capacity * KLM_GROWTH_FACTOR) == 0)
                grown++;
        }
    }

    return grown;
}

/* ============================================================
 * API — Дельта-синхронизация
 * ============================================================ */

KlmDeltaPacket *klm_delta_extract(const KlmTrainerContext *ctx, uint64_t since_version) {
    if (!ctx)
        return NULL;

    KlmDeltaPacket *delta = calloc(1, sizeof(KlmDeltaPacket));
    if (!delta)
        return NULL;

    delta->from_version = since_version;
    delta->to_version = ctx->model.global_version;

    /* Подсчёт dirty паттернов */
    size_t pc = 0;
    for (size_t i = 0; i < ctx->model.pattern_capacity; i++) {
        if (ctx->model.patterns[i].occupied && ctx->model.patterns[i].version > since_version)
            pc++;
    }

    /* Подсчёт dirty рёбер */
    size_t ec = 0;
    for (size_t i = 0; i < ctx->model.edge_capacity; i++) {
        if (ctx->model.edges[i].occupied && ctx->model.edges[i].version > since_version)
            ec++;
    }

    /* Аллокация */
    if (pc > 0) {
        delta->patterns = malloc(pc * sizeof(KlmPatternEntry));
        if (!delta->patterns) {
            free(delta);
            return NULL;
        }
    }
    if (ec > 0) {
        delta->edges = malloc(ec * sizeof(KlmEdge));
        if (!delta->edges) {
            free(delta->patterns);
            free(delta);
            return NULL;
        }
    }

    /* Копирование */
    size_t pi = 0, ei = 0;
    for (size_t i = 0; i < ctx->model.pattern_capacity && pi < pc; i++) {
        if (ctx->model.patterns[i].occupied && ctx->model.patterns[i].version > since_version) {
            delta->patterns[pi++] = ctx->model.patterns[i];
        }
    }
    for (size_t i = 0; i < ctx->model.edge_capacity && ei < ec; i++) {
        if (ctx->model.edges[i].occupied && ctx->model.edges[i].version > since_version) {
            delta->edges[ei++] = ctx->model.edges[i];
        }
    }

    delta->pattern_count = pi;
    delta->edge_count = ei;
    return delta;
}

size_t klm_delta_apply(KlmTrainerContext *ctx, const KlmDeltaPacket *delta) {
    if (!ctx || !delta)
        return 0;
    size_t applied = 0;

    /* Применяем паттерны: побеждает запись с большей version */
    for (size_t i = 0; i < delta->pattern_count; i++) {
        const KlmPatternEntry *incoming = &delta->patterns[i];

        /* Авто-расширение при необходимости */
        if (ctx->model.pattern_count >= (size_t)(ctx->model.pattern_capacity * KLM_LOAD_FACTOR)) {
            klm_rehash_patterns(ctx, ctx->model.pattern_capacity * KLM_GROWTH_FACTOR);
        }

        size_t slot = klm_find_pattern_slot(ctx->model.patterns, ctx->model.pattern_capacity, incoming->word);
        if (slot == SIZE_MAX)
            continue;

        if (ctx->model.patterns[slot].occupied) {
            /* Конфликт: побеждает бо́льшая version */
            if (incoming->version > ctx->model.patterns[slot].version) {
                ctx->model.patterns[slot] = *incoming;
                ctx->model.patterns[slot].dirty = 0; /* уже синхронизировано */
                applied++;
            }
        } else {
            ctx->model.patterns[slot] = *incoming;
            ctx->model.patterns[slot].dirty = 0;
            ctx->model.pattern_count++;
            applied++;
        }
    }

    /* Применяем рёбра */
    for (size_t i = 0; i < delta->edge_count; i++) {
        const KlmEdge *incoming = &delta->edges[i];

        if (ctx->model.edge_count >= (size_t)(ctx->model.edge_capacity * KLM_LOAD_FACTOR)) {
            klm_rehash_edges(ctx, ctx->model.edge_capacity * KLM_GROWTH_FACTOR);
        }

        size_t slot = klm_find_edge_slot(ctx->model.edges, ctx->model.edge_capacity, incoming->source_hash,
                                         incoming->target_hash);
        if (slot == SIZE_MAX)
            continue;

        if (ctx->model.edges[slot].occupied) {
            if (incoming->version > ctx->model.edges[slot].version) {
                ctx->model.edges[slot] = *incoming;
                ctx->model.edges[slot].dirty = 0;
                applied++;
            }
        } else {
            ctx->model.edges[slot] = *incoming;
            ctx->model.edges[slot].dirty = 0;
            ctx->model.edge_count++;
            applied++;
        }
    }

    /* Обновляем last_sync_version */
    if (delta->to_version > ctx->model.last_sync_version) {
        ctx->model.last_sync_version = delta->to_version;
    }

    return applied;
}

void klm_delta_mark_synced(KlmTrainerContext *ctx) {
    if (!ctx)
        return;
    for (size_t i = 0; i < ctx->model.pattern_capacity; i++) {
        ctx->model.patterns[i].dirty = 0;
    }
    for (size_t i = 0; i < ctx->model.edge_capacity; i++) {
        ctx->model.edges[i].dirty = 0;
    }
    ctx->model.last_sync_version = ctx->model.global_version;
}

uint8_t *klm_delta_serialize(const KlmDeltaPacket *delta, size_t *out_size) {
    if (!delta || !out_size)
        return NULL;

    /* Формат: [from_version:8][to_version:8][pattern_count:4][edge_count:4]
     *         [patterns...][edges...] */
    size_t header = 8 + 8 + 4 + 4;
    size_t pat_size = delta->pattern_count * sizeof(KlmPatternEntry);
    size_t edge_size = delta->edge_count * sizeof(KlmEdge);
    size_t total = header + pat_size + edge_size;

    uint8_t *buf = malloc(total);
    if (!buf)
        return NULL;

    size_t off = 0;
    memcpy(buf + off, &delta->from_version, 8);
    off += 8;
    memcpy(buf + off, &delta->to_version, 8);
    off += 8;
    uint32_t pc = (uint32_t)delta->pattern_count;
    uint32_t ec = (uint32_t)delta->edge_count;
    memcpy(buf + off, &pc, 4);
    off += 4;
    memcpy(buf + off, &ec, 4);
    off += 4;
    if (pat_size > 0) {
        memcpy(buf + off, delta->patterns, pat_size);
        off += pat_size;
    }
    if (edge_size > 0) {
        memcpy(buf + off, delta->edges, edge_size);
        off += edge_size;
    }

    *out_size = total;
    return buf;
}

KlmDeltaPacket *klm_delta_deserialize(const uint8_t *data, size_t data_size) {
    if (!data || data_size < 24)
        return NULL;

    KlmDeltaPacket *delta = calloc(1, sizeof(KlmDeltaPacket));
    if (!delta)
        return NULL;

    size_t off = 0;
    memcpy(&delta->from_version, data + off, 8);
    off += 8;
    memcpy(&delta->to_version, data + off, 8);
    off += 8;
    uint32_t pc, ec;
    memcpy(&pc, data + off, 4);
    off += 4;
    memcpy(&ec, data + off, 4);
    off += 4;

    size_t pat_size = (size_t)pc * sizeof(KlmPatternEntry);
    size_t edge_size = (size_t)ec * sizeof(KlmEdge);

    if (off + pat_size + edge_size > data_size) {
        free(delta);
        return NULL;
    }

    if (pc > 0) {
        delta->patterns = malloc(pat_size);
        if (!delta->patterns) {
            free(delta);
            return NULL;
        }
        memcpy(delta->patterns, data + off, pat_size);
        off += pat_size;
    }
    if (ec > 0) {
        delta->edges = malloc(edge_size);
        if (!delta->edges) {
            free(delta->patterns);
            free(delta);
            return NULL;
        }
        memcpy(delta->edges, data + off, edge_size);
    }

    delta->pattern_count = pc;
    delta->edge_count = ec;
    return delta;
}

void klm_delta_free(KlmDeltaPacket *delta) {
    if (!delta)
        return;
    free(delta->patterns);
    free(delta->edges);
    free(delta);
}

/* ============================================================
 * API — Шардирование графа (consistent hashing)
 * ============================================================ */

static int klm_ring_cmp(const void *a, const void *b) {
    uint32_t va = *(const uint32_t *)a;
    uint32_t vb = *(const uint32_t *)b;
    if (va < vb)
        return -1;
    if (va > vb)
        return 1;
    return 0;
}

static uint32_t klm_vnode_hash(uint32_t node_id, size_t vnode_idx) {
    /* Генерируем хеш для виртуальной ноды */
    char buf[32];
    snprintf(buf, sizeof(buf), "%u:%zu", node_id, vnode_idx);
    return klm_hash(buf);
}

int klm_shard_init(KlmShardConfig *cfg, uint32_t node_id, const uint32_t *node_ids, size_t node_count,
                   size_t vnodes_per_node) {
    if (!cfg || !node_ids || node_count == 0)
        return -1;

    memset(cfg, 0, sizeof(*cfg));
    cfg->node_id = node_id;
    cfg->vnodes_per_node = vnodes_per_node > 0 ? vnodes_per_node : 128;
    cfg->node_count = node_count;

    /* Копируем список нод */
    cfg->node_ids = malloc(node_count * sizeof(uint32_t));
    if (!cfg->node_ids)
        return -1;
    memcpy(cfg->node_ids, node_ids, node_count * sizeof(uint32_t));

    /* Строим хеш-кольцо */
    cfg->ring_size = node_count * cfg->vnodes_per_node;
    cfg->ring = malloc(cfg->ring_size * sizeof(uint32_t));
    cfg->ring_node_ids = malloc(cfg->ring_size * sizeof(uint32_t));
    if (!cfg->ring || !cfg->ring_node_ids) {
        klm_shard_free(cfg);
        return -1;
    }

    size_t idx = 0;
    for (size_t n = 0; n < node_count; n++) {
        for (size_t v = 0; v < cfg->vnodes_per_node; v++) {
            cfg->ring[idx] = klm_vnode_hash(node_ids[n], v);
            cfg->ring_node_ids[idx] = node_ids[n];
            idx++;
        }
    }

    /* Сортируем кольцо + соответствие node_ids */
    /* Используем parallel sort: создаём пары, сортируем, разделяем */
    typedef struct {
        uint32_t hash;
        uint32_t nid;
    } RingEntry;
    RingEntry *entries = malloc(cfg->ring_size * sizeof(RingEntry));
    if (!entries) {
        klm_shard_free(cfg);
        return -1;
    }
    for (size_t i = 0; i < cfg->ring_size; i++) {
        entries[i].hash = cfg->ring[i];
        entries[i].nid = cfg->ring_node_ids[i];
    }
    qsort(entries, cfg->ring_size, sizeof(RingEntry), (int (*)(const void *, const void *))klm_ring_cmp);
    for (size_t i = 0; i < cfg->ring_size; i++) {
        cfg->ring[i] = entries[i].hash;
        cfg->ring_node_ids[i] = entries[i].nid;
    }
    free(entries);

    return 0;
}

uint32_t klm_shard_owner(const KlmShardConfig *cfg, uint32_t hash) {
    if (!cfg || cfg->ring_size == 0)
        return 0;

    /* Бинарный поиск: первая позиция в кольце >= hash */
    size_t lo = 0, hi = cfg->ring_size;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cfg->ring[mid] < hash)
            lo = mid + 1;
        else
            hi = mid;
    }

    /* Wrap-around */
    if (lo >= cfg->ring_size)
        lo = 0;

    return cfg->ring_node_ids[lo];
}

bool klm_shard_is_local(const KlmShardConfig *cfg, uint32_t hash) { return klm_shard_owner(cfg, hash) == cfg->node_id; }

int klm_shard_rebuild(KlmShardConfig *cfg, const uint32_t *node_ids, size_t node_count) {
    if (!cfg)
        return -1;
    uint32_t saved_id = cfg->node_id;
    size_t saved_vnodes = cfg->vnodes_per_node;
    klm_shard_free(cfg);
    return klm_shard_init(cfg, saved_id, node_ids, node_count, saved_vnodes);
}

void klm_shard_free(KlmShardConfig *cfg) {
    if (!cfg)
        return;
    free(cfg->ring);
    free(cfg->ring_node_ids);
    free(cfg->node_ids);
    memset(cfg, 0, sizeof(*cfg));
}
