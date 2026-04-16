/*
 * auto_learn.c
 *
 * Реализация автономного цикла обучения (Autonomous Learning Loop)
 *
 * Три стратегии обучения:
 *
 * 1. НАБЛЮДЕНИЕ (Observation):
 *    Чтение внешних данных → подача в мировую модель → обучение
 *
 * 2. ЛЮБОПЫТСТВО (Curiosity-driven):
 *    Модель генерирует текст → анализирует удивление →
 *    → фокусируется на областях с высоким удивлением
 *
 * 3. ЭВОЛЮЦИЯ (Evolution):
 *    SPSA-мутация параметров → eval → сохранение если улучшение
 *    → откат если деградация
 *
 * Замкнутый цикл не требует человеческого вмешательства.
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/auto_learn.h"
#include "kolibri/world_model.h"
#include "kolibri/attention.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

/* ========== ВНУТРЕННИЙ PRNG ========== */

static uint64_t kal_rng(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* ========== ЖИЗНЕННЫЙ ЦИКЛ ========== */

KalContext* kal_create(uint64_t seed) {
    KalContext *ctx = calloc(1, sizeof(KalContext));
    if (!ctx) return NULL;

    ctx->seed = seed ? seed : 42;

    /* Создаём мировую модель */
    ctx->world_model = (struct KwmContext*)kwm_create(ctx->seed);
    if (!ctx->world_model) {
        free(ctx);
        return NULL;
    }

    /* Параметры по умолчанию */
    ctx->mode = KAL_MODE_MIXED;
    ctx->learning_rate = 0.001f;
    ctx->lr_decay = 0.9999f;
    ctx->mutation_strength = 0.01f;
    ctx->curiosity_temperature = 0.8f;
    ctx->checkpoint_interval = 1000;
    ctx->eval_interval = 100;

    /* Метрики */
    ctx->metrics.best_loss = DBL_MAX;

    return ctx;
}

void kal_destroy(KalContext *ctx) {
    if (!ctx) return;

    /* Освобождаем чекпоинты */
    for (size_t i = 0; i < ctx->checkpoint_count; i++) {
        free(ctx->checkpoints[i].state_data);
    }

    /* Освобождаем eval данные */
    free(ctx->eval_data);

    /* Уничтожаем мировую модель */
    if (ctx->world_model) {
        kwm_destroy((KwmContext*)ctx->world_model);
    }

    free(ctx);
}

/* ========== ИСТОЧНИКИ ДАННЫХ ========== */

int kal_add_file_source(KalContext *ctx, const char *path, float weight) {
    if (!ctx || !path || ctx->source_count >= KAL_MAX_SOURCES) return -1;

    KalDataSource *src = &ctx->sources[ctx->source_count];
    memset(src, 0, sizeof(KalDataSource));
    src->type = KAL_SOURCE_FILE;
    snprintf(src->path, sizeof(src->path), "%s", path);
    src->weight = weight > 0.0f ? weight : 1.0f;

    /* Загружаем файл в память */
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {  /* Максимум 10MB */
        fclose(f);
        return -1;
    }

    uint8_t *buf = malloc((size_t)fsize);
    if (!buf) {
        fclose(f);
        return -1;
    }

    size_t read = fread(buf, 1, (size_t)fsize, f);
    fclose(f);

    src->data = buf;
    src->data_len = read;
    src->cursor = 0;

    ctx->source_count++;
    return 0;
}

int kal_add_memory_source(KalContext *ctx, const uint8_t *data,
                          size_t len, float weight) {
    if (!ctx || !data || len == 0 || ctx->source_count >= KAL_MAX_SOURCES)
        return -1;

    KalDataSource *src = &ctx->sources[ctx->source_count];
    memset(src, 0, sizeof(KalDataSource));
    src->type = KAL_SOURCE_MEMORY;
    src->data = data;
    src->data_len = len;
    src->cursor = 0;
    src->weight = weight > 0.0f ? weight : 1.0f;

    ctx->source_count++;
    return 0;
}

int kal_set_eval_data(KalContext *ctx, const uint8_t *data, size_t len) {
    if (!ctx || !data || len == 0) return -1;

    free(ctx->eval_data);
    ctx->eval_data = malloc(len);
    if (!ctx->eval_data) return -1;
    memcpy(ctx->eval_data, data, len);
    ctx->eval_len = len;

    return 0;
}

/* ========== ВНУТРЕННИЕ СТРАТЕГИИ ========== */

/*
 * Стратегия наблюдения:
 * Берём батч из случайного источника и подаём в мировую модель
 */
static float tick_observation(KalContext *ctx) {
    if (ctx->source_count == 0) return 0.0f;

    /* Выбираем случайный источник (взвешенно) */
    float total_weight = 0.0f;
    for (size_t i = 0; i < ctx->source_count; i++) {
        total_weight += ctx->sources[i].weight;
    }

    float r = (float)(kal_rng(&ctx->seed) & 0xFFFF) / (float)0xFFFF * total_weight;
    float cumsum = 0.0f;
    size_t src_idx = 0;
    for (size_t i = 0; i < ctx->source_count; i++) {
        cumsum += ctx->sources[i].weight;
        if (cumsum >= r) { src_idx = i; break; }
    }

    KalDataSource *src = &ctx->sources[src_idx];
    if (!src->data || src->data_len == 0) return 0.0f;

    /* Берём батч */
    size_t batch_size = KAL_BATCH_SIZE;
    if (src->cursor + batch_size > src->data_len) {
        /* Перемотка */
        src->cursor = 0;
    }

    float avg_loss = kwm_observe_block(
        (KwmContext*)ctx->world_model,
        src->data + src->cursor,
        batch_size < (src->data_len - src->cursor) ?
            batch_size : (src->data_len - src->cursor)
    );

    src->cursor += batch_size;
    if (src->cursor >= src->data_len) src->cursor = 0;

    ctx->metrics.observation_ticks++;
    return avg_loss;
}

/*
 * Стратегия любопытства:
 * Модель генерирует текст → анализирует удивление →
 * → обучается на областях высокого удивления
 */
static float tick_curiosity(KalContext *ctx) {
    KwmContext *wm = (KwmContext*)ctx->world_model;

    /* Генерируем текст через мировую модель */
    uint8_t generated[KAL_BATCH_SIZE];
    size_t gen_len = kwm_generate(
        wm, generated, KAL_BATCH_SIZE / 2,
        ctx->curiosity_temperature
    );

    if (gen_len == 0) return 0.0f;

    /* Пропускаем через модель как наблюдение (с обучением) */
    float surprise = kwm_observe_block(wm, generated, gen_len);

    ctx->metrics.curiosity_ticks++;
    ctx->metrics.curiosity_surprise =
        ctx->metrics.curiosity_surprise * 0.99 + (double)surprise * 0.01;

    return surprise;
}

/*
 * Стратегия эволюции:
 * SPSA-подобная мутация параметров LM head
 * Если eval улучшился — сохраняем, иначе откатываем
 */
static float tick_evolution(KalContext *ctx) {
    KwmContext *wm = (KwmContext*)ctx->world_model;
    KatModel *model = (KatModel*)wm->backbone;

    if (!model) return 0.0f;

    /* Текущий eval loss */
    double base_eval = kal_eval(ctx);
    if (base_eval < 0.0) base_eval = 100.0;

    /* Мутация: SPSA на случайных параметрах LM head */
    float *params = model->lm_head;
    size_t total = (size_t)model->cfg.embed_dim * model->cfg.vocab_size;

    /* Сохраняем оригинальные значения */
    #define SPSA_BATCH 32
    size_t indices[SPSA_BATCH];
    float originals[SPSA_BATCH];

    for (int i = 0; i < SPSA_BATCH; i++) {
        indices[i] = kal_rng(&ctx->seed) % total;
        originals[i] = params[indices[i]];

        /* Случайная пертурбация ±mutation_strength */
        float sign = (kal_rng(&ctx->seed) & 1) ? 1.0f : -1.0f;
        params[indices[i]] += sign * ctx->mutation_strength;
    }

    ctx->metrics.evolution_mutations++;

    /* Оцениваем после мутации */
    double new_eval = kal_eval(ctx);

    if (new_eval < base_eval) {
        /* Улучшение! Сохраняем мутацию */
        ctx->metrics.evolution_improvements++;
    } else {
        /* Откатываем */
        for (int i = 0; i < SPSA_BATCH; i++) {
            params[indices[i]] = originals[i];
        }
    }

    #undef SPSA_BATCH

    ctx->metrics.evolution_ticks++;
    return (float)base_eval;
}

/* ========== ОБУЧЕНИЕ ========== */

float kal_train_tick(KalContext *ctx) {
    if (!ctx) return 0.0f;

    float loss = 0.0f;

    switch (ctx->mode) {
    case KAL_MODE_OBSERVATION:
        loss = tick_observation(ctx);
        break;

    case KAL_MODE_CURIOSITY:
        loss = tick_curiosity(ctx);
        break;

    case KAL_MODE_EVOLUTION:
        loss = tick_evolution(ctx);
        break;

    case KAL_MODE_MIXED: {
        /* Чередуем режимы: 60% наблюдение, 25% любопытство, 15% эволюция */
        uint64_t r = kal_rng(&ctx->seed) % 100;
        if (r < 60 && ctx->source_count > 0) {
            loss = tick_observation(ctx);
        } else if (r < 85) {
            loss = tick_curiosity(ctx);
        } else {
            loss = tick_evolution(ctx);
        }
        break;
    }
    }

    /* Обновляем метрики */
    ctx->metrics.total_ticks++;
    ctx->metrics.current_loss = (double)loss;
    if ((double)loss < ctx->metrics.best_loss && loss > 0.0f) {
        ctx->metrics.best_loss = (double)loss;
    }

    /* Скорость обучения: экспоненциальное затухание */
    ctx->learning_rate *= ctx->lr_decay;
    kwm_set_learning_rate(
        (KwmContext*)ctx->world_model,
        ctx->learning_rate
    );

    /* Вычисляем скорость обучения (learning velocity) */
    if (ctx->metrics.total_ticks > 10) {
        double prev = ctx->metrics.current_loss;
        ctx->metrics.learning_velocity =
            ctx->metrics.learning_velocity * 0.99 + (prev - loss) * 0.01;
    }

    /* Периодический eval */
    if (ctx->eval_data && ctx->eval_len > 0 &&
        ctx->metrics.total_ticks % ctx->eval_interval == 0) {
        ctx->metrics.eval_loss = kal_eval(ctx);
    }

    /* Периодический чекпоинт */
    if (ctx->metrics.total_ticks % ctx->checkpoint_interval == 0) {
        kal_checkpoint(ctx);
    }

    /* Подсчёт концептов */
    KwmStats wm_stats;
    kwm_get_stats((KwmContext*)ctx->world_model, &wm_stats);
    ctx->metrics.concepts_learned = wm_stats.num_concepts;

    return loss;
}

int kal_train(KalContext *ctx, uint64_t ticks) {
    if (!ctx) return -1;

    for (uint64_t t = 0; t < ticks; t++) {
        kal_train_tick(ctx);
    }

    return 0;
}

/* ========== НАСТРОЙКИ ========== */

void kal_set_mode(KalContext *ctx, KalLearningMode mode) {
    if (ctx) ctx->mode = mode;
}

void kal_set_learning_rate(KalContext *ctx, float lr) {
    if (ctx) ctx->learning_rate = lr > 0.0f ? lr : 0.001f;
}

void kal_set_checkpoint_interval(KalContext *ctx, uint64_t interval) {
    if (ctx) ctx->checkpoint_interval = interval > 0 ? interval : 1000;
}

/* ========== EVAL ========== */

double kal_eval(KalContext *ctx) {
    if (!ctx || !ctx->eval_data || ctx->eval_len == 0) return -1.0;

    /*
     * Eval: прогоняем held-out данные через мировую модель
     * БЕЗ обучения (auto_learn = 0)
     */
    KwmContext *wm = (KwmContext*)ctx->world_model;

    /* Сохраняем и отключаем auto_learn */
    kwm_set_auto_learn(wm, 0);

    /* Прогоняем eval данные через свежий контекст */
    /* Используем подмножество для быстрого eval */
    size_t eval_size = ctx->eval_len;
    if (eval_size > KAL_EVAL_WINDOW) eval_size = KAL_EVAL_WINDOW;

    double total_surprise = 0.0;
    for (size_t i = 0; i < eval_size; i++) {
        KwmPrediction pred;
        float s = kwm_observe(wm, ctx->eval_data[i], &pred);
        if (s >= 0.0f) total_surprise += (double)s;
    }

    /* Восстанавливаем auto_learn */
    kwm_set_auto_learn(wm, 1);

    return total_surprise / (double)eval_size;
}

/* ========== ЧЕКПОИНТЫ ========== */

int kal_checkpoint(KalContext *ctx) {
    if (!ctx) return -1;

    /* Если буфер чекпоинтов полон, удаляем худший */
    if (ctx->checkpoint_count >= KAL_MAX_CHECKPOINTS) {
        /* Находим чекпоинт с наибольшим eval_loss */
        size_t worst = 0;
        double worst_loss = ctx->checkpoints[0].eval_loss;
        for (size_t i = 1; i < ctx->checkpoint_count; i++) {
            if (ctx->checkpoints[i].eval_loss > worst_loss) {
                worst_loss = ctx->checkpoints[i].eval_loss;
                worst = i;
            }
        }
        free(ctx->checkpoints[worst].state_data);
        /* Сдвигаем */
        if (worst < ctx->checkpoint_count - 1) {
            memmove(&ctx->checkpoints[worst],
                    &ctx->checkpoints[worst + 1],
                    (ctx->checkpoint_count - worst - 1) * sizeof(KalCheckpoint));
        }
        ctx->checkpoint_count--;
    }

    /* Сериализуем мировую модель */
    size_t state_size = kwm_serialize(
        (const KwmContext*)ctx->world_model, NULL, 0);
    if (state_size == 0) return -1;

    uint8_t *state_data = malloc(state_size);
    if (!state_data) return -1;

    kwm_serialize((const KwmContext*)ctx->world_model, state_data, state_size);

    /* Создаём чекпоинт */
    KalCheckpoint *cp = &ctx->checkpoints[ctx->checkpoint_count];
    cp->tick = ctx->metrics.total_ticks;
    cp->eval_loss = ctx->metrics.eval_loss;
    cp->compression = ctx->metrics.current_loss > 0.0 ?
                      8.0 / ctx->metrics.current_loss : 0.0;
    cp->state_size = state_size;
    cp->state_data = state_data;

    ctx->checkpoint_count++;
    ctx->metrics.checkpoints_created++;

    return 0;
}

int kal_rollback_to_best(KalContext *ctx) {
    if (!ctx || ctx->checkpoint_count == 0) return -1;

    /* Находим чекпоинт с наименьшим eval_loss */
    size_t best = 0;
    double best_loss = ctx->checkpoints[0].eval_loss;
    for (size_t i = 1; i < ctx->checkpoint_count; i++) {
        if (ctx->checkpoints[i].eval_loss < best_loss) {
            best_loss = ctx->checkpoints[i].eval_loss;
            best = i;
        }
    }

    KalCheckpoint *cp = &ctx->checkpoints[best];
    if (!cp->state_data || cp->state_size == 0) return -1;

    /* Восстанавливаем состояние */
    return kwm_deserialize(
        (KwmContext*)ctx->world_model,
        cp->state_data, cp->state_size
    );
}

/* ========== МЕТРИКИ ========== */

void kal_get_metrics(const KalContext *ctx, KalMetrics *metrics) {
    if (ctx && metrics) *metrics = ctx->metrics;
}

void kal_reset_metrics(KalContext *ctx) {
    if (!ctx) return;
    memset(&ctx->metrics, 0, sizeof(KalMetrics));
    ctx->metrics.best_loss = DBL_MAX;
}
