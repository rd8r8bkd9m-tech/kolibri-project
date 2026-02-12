/*
 * world_model.c
 *
 * Реализация мировой модели (World Model) для Kolibri AGI
 *
 * Предсказательная модель: сжатие ≡ предсказание ≡ понимание
 *   - Каждый наблюдённый байт вычисляет P(byte|context) через Transformer
 *   - «Удивление» = -log2(P) измеряет количество новой информации
 *   - Онлайн-обучение через градиентную оценку (SPSA)
 *   - Извлечение концептов: кластеры в пространстве эмбеддингов
 *
 * Алгоритм SPSA (Simultaneous Perturbation Stochastic Approximation):
 *   Эффективнее поэлементной оценки градиента:
 *   - Одна пертурбация для всех параметров одновременно
 *   - O(2) forward pass вместо O(2*p) для p параметров
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/world_model.h"
#include "kolibri/attention.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

/* ========== ВНУТРЕННИЙ PRNG ========== */

static uint64_t kwm_rng(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* ========== ЖИЗНЕННЫЙ ЦИКЛ ========== */

KwmContext* kwm_create(uint64_t seed) {
    KwmContext *ctx = calloc(1, sizeof(KwmContext));
    if (!ctx) return NULL;

    ctx->seed = seed ? seed : 42;
    ctx->tick = 0;

    /* Создаём нейронный бэкбон */
    ctx->backbone = (struct KatModel*)kat_model_create(ctx->seed);
    if (!ctx->backbone) {
        free(ctx);
        return NULL;
    }

    ctx->workspace = (struct KatWorkspace*)kat_workspace_create();
    if (!ctx->workspace) {
        kat_model_destroy((KatModel*)ctx->backbone);
        free(ctx);
        return NULL;
    }

    /* Параметры по умолчанию */
    ctx->learning_rate = 0.001f;
    ctx->surprise_threshold = 3.0f;  /* bits */
    ctx->auto_learn = 1;

    /* Начальная статистика */
    ctx->stats.min_loss = FLT_MAX;

    return ctx;
}

void kwm_destroy(KwmContext *ctx) {
    if (!ctx) return;
    if (ctx->workspace) kat_workspace_destroy((KatWorkspace*)ctx->workspace);
    if (ctx->backbone) kat_model_destroy((KatModel*)ctx->backbone);
    free(ctx);
}

void kwm_reset(KwmContext *ctx) {
    if (!ctx) return;
    ctx->context_len = 0;
    ctx->context_pos = 0;
    ctx->history_len = 0;
    ctx->history_pos = 0;
    ctx->tick = 0;
    memset(&ctx->stats, 0, sizeof(KwmStats));
    ctx->stats.min_loss = FLT_MAX;
}

/* ========== НАБЛЮДЕНИЕ ========== */

/*
 * Ядро мировой модели — наблюдение одного байта
 *
 * Последовательность:
 *   1. Forward pass через Transformer на текущем контексте
 *   2. Получение P(token | context)
 *   3. Вычисление удивления: -log2(P(actual))
 *   4. Если auto_learn: обучение на наблюдённом токене
 *   5. Добавление байта в контекст
 *   6. Если удивление > порога: создание нового концепта
 */
float kwm_observe(KwmContext *ctx, uint8_t byte, KwmPrediction *pred) {
    if (!ctx) return -1.0f;

    float surprise = 0.0f;

    /* --- Предсказание --- */
    if (ctx->context_len > 0) {
        /* Forward pass на текущем контексте */
        size_t seq_len = ctx->context_len;
        if (seq_len > KWM_CONTEXT_SIZE) seq_len = KWM_CONTEXT_SIZE;

        int rc = kat_forward(
            (KatModel*)ctx->backbone,
            (KatWorkspace*)ctx->workspace,
            ctx->context, seq_len
        );

        if (rc == 0) {
            KatWorkspace *ws = (KatWorkspace*)ctx->workspace;

            /* Вычисляем удивление */
            float p_actual = ws->probs[byte];
            if (p_actual < 1e-10f) p_actual = 1e-10f;
            surprise = -log2f(p_actual);

            /* Заполняем предсказание */
            if (pred) {
                memcpy(pred->probs, ws->probs, 256 * sizeof(float));
                /* Наиболее вероятный токен */
                pred->predicted_token = 0;
                float best_p = ws->probs[0];
                for (int i = 1; i < 256; i++) {
                    if (ws->probs[i] > best_p) {
                        best_p = ws->probs[i];
                        pred->predicted_token = (uint8_t)i;
                    }
                }
                pred->confidence = best_p;
                pred->surprise = surprise;
            }

            /* --- Онлайн-обучение (каждый 4-й байт) --- */
            if (ctx->auto_learn && (ctx->tick & 3) == 0) {
                float loss = kat_train_step_fast(
                    (KatModel*)ctx->backbone,
                    (KatWorkspace*)ctx->workspace,
                    ctx->context, seq_len,
                    byte, ctx->learning_rate
                );
                ctx->stats.learning_steps++;
                (void)loss;
            }
        }
    } else {
        /* Первый байт — нет контекста, максимальное удивление */
        surprise = 8.0f;  /* log2(256) = 8 bits */
        if (pred) {
            /* Равномерное распределение */
            for (int i = 0; i < 256; i++) pred->probs[i] = 1.0f / 256.0f;
            pred->predicted_token = byte;
            pred->confidence = 1.0f / 256.0f;
            pred->surprise = surprise;
        }
    }

    /* --- Обновление контекста --- */
    if (ctx->context_len < KWM_CONTEXT_SIZE) {
        ctx->context[ctx->context_len] = byte;
        ctx->context_len++;
    } else {
        /* Кольцевой сдвиг: удаляем первый, добавляем в конец */
        memmove(ctx->context, ctx->context + 1, KWM_CONTEXT_SIZE - 1);
        ctx->context[KWM_CONTEXT_SIZE - 1] = byte;
    }

    /* --- Обновление истории --- */
    if (ctx->history_len < KWM_HISTORY_SIZE) {
        ctx->history[ctx->history_len] = byte;
        ctx->history_len++;
    } else {
        ctx->history[ctx->history_pos] = byte;
        ctx->history_pos = (ctx->history_pos + 1) % KWM_HISTORY_SIZE;
    }

    /* --- Обновление статистики --- */
    ctx->stats.total_tokens++;
    ctx->stats.total_loss += (double)surprise;
    ctx->stats.avg_loss = ctx->stats.total_loss / (double)ctx->stats.total_tokens;
    if ((double)surprise < ctx->stats.min_loss) {
        ctx->stats.min_loss = (double)surprise;
    }
    ctx->stats.perplexity = pow(2.0, ctx->stats.avg_loss);
    ctx->stats.surprise_integral += (double)surprise;

    /* Коэффициент сжатия: 8 bits/byte ÷ avg_bits/byte */
    if (ctx->stats.avg_loss > 0.01) {
        ctx->stats.compression_ratio = 8.0 / ctx->stats.avg_loss;
    }

    ctx->tick++;

    /* --- Извлечение концептов при высоком удивлении --- */
    if (surprise > ctx->surprise_threshold &&
        ctx->concept_count < KWM_MAX_CONCEPTS &&
        ctx->context_len >= 4) {
        /* Создаём новый концепт из текущего контекста */
        KwmConcept *concept = &ctx->concepts[ctx->concept_count];
        memset(concept, 0, sizeof(KwmConcept));

        /* Извлекаем эмбеддинг */
        kat_extract_embedding(
            (KatWorkspace*)ctx->workspace,
            concept->embedding
        );

        /* Метка: последние 16 байт контекста */
        size_t label_start = ctx->context_len > 16 ? ctx->context_len - 16 : 0;
        size_t label_len = ctx->context_len - label_start;
        if (label_len > 127) label_len = 127;
        memcpy(concept->label, ctx->context + label_start, label_len);
        concept->label[label_len] = '\0';

        concept->salience = 1.0f;
        concept->surprise = surprise;
        concept->first_seen = ctx->tick;
        concept->last_seen = ctx->tick;
        concept->frequency = 1;

        ctx->concept_count++;
        ctx->stats.num_concepts = ctx->concept_count;
    }

    return surprise;
}

float kwm_observe_block(KwmContext *ctx, const uint8_t *data, size_t len) {
    if (!ctx || !data || len == 0) return -1.0f;

    double total_surprise = 0.0;
    for (size_t i = 0; i < len; i++) {
        float s = kwm_observe(ctx, data[i], NULL);
        if (s >= 0.0f) total_surprise += (double)s;
    }

    return (float)(total_surprise / (double)len);
}

/* ========== ПРЕДСКАЗАНИЕ ========== */

int kwm_predict(KwmContext *ctx, KwmPrediction *pred) {
    if (!ctx || !pred) return -1;

    memset(pred, 0, sizeof(KwmPrediction));

    if (ctx->context_len == 0) {
        /* Нет контекста — равномерное распределение */
        for (int i = 0; i < 256; i++) pred->probs[i] = 1.0f / 256.0f;
        pred->confidence = 1.0f / 256.0f;
        return 0;
    }

    /* Forward pass */
    int rc = kat_forward(
        (KatModel*)ctx->backbone,
        (KatWorkspace*)ctx->workspace,
        ctx->context, ctx->context_len
    );

    if (rc != 0) return -1;

    KatWorkspace *ws = (KatWorkspace*)ctx->workspace;
    memcpy(pred->probs, ws->probs, 256 * sizeof(float));

    /* Наиболее вероятный */
    pred->predicted_token = 0;
    float best_p = ws->probs[0];
    for (int i = 1; i < 256; i++) {
        if (ws->probs[i] > best_p) {
            best_p = ws->probs[i];
            pred->predicted_token = (uint8_t)i;
        }
    }
    pred->confidence = best_p;
    pred->surprise = 0.0f;  /* Ещё нет наблюдения */

    return 0;
}

/* ========== ГЕНЕРАЦИЯ ========== */

size_t kwm_generate(KwmContext *ctx, uint8_t *output, size_t max_len,
                    float temperature) {
    if (!ctx || !output || max_len == 0) return 0;

    size_t generated = 0;

    for (size_t i = 0; i < max_len; i++) {
        if (ctx->context_len == 0) break;

        /* Forward pass */
        int rc = kat_forward(
            (KatModel*)ctx->backbone,
            (KatWorkspace*)ctx->workspace,
            ctx->context, ctx->context_len
        );
        if (rc != 0) break;

        /* Сэмплируем следующий токен */
        uint8_t next = kat_sample(
            (KatModel*)ctx->backbone,
            (KatWorkspace*)ctx->workspace,
            temperature
        );

        output[i] = next;
        generated++;

        /* Добавляем в контекст */
        if (ctx->context_len < KWM_CONTEXT_SIZE) {
            ctx->context[ctx->context_len] = next;
            ctx->context_len++;
        } else {
            memmove(ctx->context, ctx->context + 1, KWM_CONTEXT_SIZE - 1);
            ctx->context[KWM_CONTEXT_SIZE - 1] = next;
        }
    }

    return generated;
}

/* ========== КОНЦЕПТЫ И ЭМБЕДДИНГИ ========== */

int kwm_embed_text(KwmContext *ctx, const char *text, size_t len,
                   float *out) {
    if (!ctx || !text || len == 0 || !out) return -1;

    /* Ограничиваем длину контекстным окном */
    if (len > KWM_CONTEXT_SIZE) len = KWM_CONTEXT_SIZE;

    /* Forward pass */
    int rc = kat_forward(
        (KatModel*)ctx->backbone,
        (KatWorkspace*)ctx->workspace,
        (const uint8_t*)text, len
    );

    if (rc != 0) return -1;

    /* Извлекаем средний эмбеддинг */
    kat_extract_embedding((KatWorkspace*)ctx->workspace, out);

    return 0;
}

float kwm_similarity(KwmContext *ctx, const char *a, const char *b) {
    if (!ctx || !a || !b) return 0.0f;

    float emb_a[KWM_CONCEPT_DIM];
    float emb_b[KWM_CONCEPT_DIM];

    if (kwm_embed_text(ctx, a, strlen(a), emb_a) != 0) return 0.0f;
    if (kwm_embed_text(ctx, b, strlen(b), emb_b) != 0) return 0.0f;

    return kat_cosine_similarity(emb_a, emb_b, KWM_CONCEPT_DIM);
}

size_t kwm_extract_concepts(KwmContext *ctx, const char *text, size_t len,
                            KwmConcept *concepts, size_t max_concepts) {
    if (!ctx || !text || len == 0 || !concepts || max_concepts == 0) return 0;

    /*
     * Стратегия извлечения концептов:
     * 1. Скользящее окно (16 байт) по тексту
     * 2. Для каждого окна вычисляем эмбеддинг
     * 3. Кластеризация: если эмбеддинг далёк от всех существующих → новый концепт
     */
    size_t found = 0;
    size_t window = 16;
    float similarity_threshold = 0.8f;

    for (size_t pos = 0; pos + window <= len && found < max_concepts;
         pos += window / 2) {
        float emb[KWM_CONCEPT_DIM];

        /* Эмбеддинг окна */
        if (kwm_embed_text(ctx, text + pos,
                           (pos + window > len) ? len - pos : window,
                           emb) != 0) continue;

        /* Проверяем, не повторяет ли существующий концепт */
        int is_new = 1;
        for (size_t i = 0; i < found; i++) {
            float sim = kat_cosine_similarity(
                emb, concepts[i].embedding, KWM_CONCEPT_DIM
            );
            if (sim > similarity_threshold) {
                /* Обновляем существующий */
                concepts[i].frequency++;
                concepts[i].last_seen = ctx->tick;
                is_new = 0;
                break;
            }
        }

        if (is_new) {
            KwmConcept *c = &concepts[found];
            memcpy(c->embedding, emb, KWM_CONCEPT_DIM * sizeof(float));

            /* Метка */
            size_t label_len = (pos + window > len) ? len - pos : window;
            if (label_len > 127) label_len = 127;
            memcpy(c->label, text + pos, label_len);
            c->label[label_len] = '\0';

            c->salience = 1.0f;
            c->surprise = 0.0f;
            c->first_seen = ctx->tick;
            c->last_seen = ctx->tick;
            c->frequency = 1;

            found++;
        }
    }

    return found;
}

/* ========== ОБУЧЕНИЕ ========== */

void kwm_set_auto_learn(KwmContext *ctx, int enabled) {
    if (ctx) ctx->auto_learn = enabled;
}

void kwm_set_learning_rate(KwmContext *ctx, float lr) {
    if (ctx) ctx->learning_rate = lr > 0.0f ? lr : 0.001f;
}

float kwm_learn_step(KwmContext *ctx) {
    if (!ctx || ctx->context_len < 2) return 0.0f;

    /* Обучаем на предсказание последнего байта контекста */
    uint8_t target = ctx->context[ctx->context_len - 1];
    size_t input_len = ctx->context_len - 1;

    return kat_train_step_fast(
        (KatModel*)ctx->backbone,
        (KatWorkspace*)ctx->workspace,
        ctx->context, input_len,
        target, ctx->learning_rate
    );
}

/* ========== СТАТИСТИКА ========== */

void kwm_get_stats(const KwmContext *ctx, KwmStats *stats) {
    if (ctx && stats) *stats = ctx->stats;
}

void kwm_reset_stats(KwmContext *ctx) {
    if (!ctx) return;
    memset(&ctx->stats, 0, sizeof(KwmStats));
    ctx->stats.min_loss = FLT_MAX;
}

/* ========== СЕРИАЛИЗАЦИЯ ========== */

size_t kwm_serialize(const KwmContext *ctx, uint8_t *buf, size_t buf_size) {
    if (!ctx) return 0;

    /* Вычисляем общий размер */
    size_t backbone_size = kat_serialize(
        (const KatModel*)ctx->backbone, NULL, 0);
    size_t total = sizeof(uint64_t)        /* tick */
                 + sizeof(size_t) * 4      /* lengths */
                 + KWM_CONTEXT_SIZE        /* context */
                 + KWM_HISTORY_SIZE        /* history */
                 + sizeof(KwmStats)        /* stats */
                 + sizeof(float) * 2       /* lr, surprise_threshold */
                 + sizeof(int)             /* auto_learn */
                 + sizeof(size_t)          /* concept_count */
                 + ctx->concept_count * sizeof(KwmConcept)
                 + backbone_size;

    if (!buf || buf_size < total) return total;

    uint8_t *p = buf;

    /* Состояние */
    memcpy(p, &ctx->tick, sizeof(uint64_t)); p += sizeof(uint64_t);
    memcpy(p, &ctx->context_len, sizeof(size_t)); p += sizeof(size_t);
    memcpy(p, &ctx->context_pos, sizeof(size_t)); p += sizeof(size_t);
    memcpy(p, &ctx->history_len, sizeof(size_t)); p += sizeof(size_t);
    memcpy(p, &ctx->history_pos, sizeof(size_t)); p += sizeof(size_t);
    memcpy(p, ctx->context, KWM_CONTEXT_SIZE); p += KWM_CONTEXT_SIZE;
    memcpy(p, ctx->history, KWM_HISTORY_SIZE); p += KWM_HISTORY_SIZE;
    memcpy(p, &ctx->stats, sizeof(KwmStats)); p += sizeof(KwmStats);
    memcpy(p, &ctx->learning_rate, sizeof(float)); p += sizeof(float);
    memcpy(p, &ctx->surprise_threshold, sizeof(float)); p += sizeof(float);
    memcpy(p, &ctx->auto_learn, sizeof(int)); p += sizeof(int);
    memcpy(p, &ctx->concept_count, sizeof(size_t)); p += sizeof(size_t);
    memcpy(p, ctx->concepts, ctx->concept_count * sizeof(KwmConcept));
    p += ctx->concept_count * sizeof(KwmConcept);

    /* Бэкбон */
    kat_serialize((const KatModel*)ctx->backbone, p,
                  buf_size - (size_t)(p - buf));

    return total;
}

int kwm_deserialize(KwmContext *ctx, const uint8_t *buf, size_t buf_size) {
    if (!ctx || !buf || buf_size < sizeof(uint64_t)) return -1;

    const uint8_t *p = buf;

    /* Состояние */
    memcpy(&ctx->tick, p, sizeof(uint64_t)); p += sizeof(uint64_t);
    memcpy(&ctx->context_len, p, sizeof(size_t)); p += sizeof(size_t);
    memcpy(&ctx->context_pos, p, sizeof(size_t)); p += sizeof(size_t);
    memcpy(&ctx->history_len, p, sizeof(size_t)); p += sizeof(size_t);
    memcpy(&ctx->history_pos, p, sizeof(size_t)); p += sizeof(size_t);
    memcpy(ctx->context, p, KWM_CONTEXT_SIZE); p += KWM_CONTEXT_SIZE;
    memcpy(ctx->history, p, KWM_HISTORY_SIZE); p += KWM_HISTORY_SIZE;
    memcpy(&ctx->stats, p, sizeof(KwmStats)); p += sizeof(KwmStats);
    memcpy(&ctx->learning_rate, p, sizeof(float)); p += sizeof(float);
    memcpy(&ctx->surprise_threshold, p, sizeof(float)); p += sizeof(float);
    memcpy(&ctx->auto_learn, p, sizeof(int)); p += sizeof(int);

    size_t concept_count;
    memcpy(&concept_count, p, sizeof(size_t)); p += sizeof(size_t);
    if (concept_count > KWM_MAX_CONCEPTS) concept_count = KWM_MAX_CONCEPTS;
    ctx->concept_count = concept_count;
    memcpy(ctx->concepts, p, concept_count * sizeof(KwmConcept));
    p += concept_count * sizeof(KwmConcept);

    /* Бэкбон */
    int rc = kat_deserialize(
        (KatModel*)ctx->backbone, p,
        buf_size - (size_t)(p - buf)
    );

    return rc;
}
