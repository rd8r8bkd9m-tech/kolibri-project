/*
 * attention.c
 *
 * Реализация модуля Self-Attention для Kolibri AGI
 *
 * Масштабируемая архитектура: Pre-LN Transformer
 *   - Динамическая аллокация всех весов и буферов
 *   - Runtime-конфигурация: small (~165K), medium (~6.5M), large (~100M)
 *   - Xavier инициализация весов
 *   - Multi-Head Scaled Dot-Product Attention
 *   - Каузальная маска (авторегрессия)
 *   - GELU активация в Feed-Forward
 *   - Residual connections + LayerNorm
 *   - Обучение через приближённый backward pass (прямая оценка градиентов)
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/attention.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

/* Короткий алиас для 2D-индексации плоского массива */
#define IDX2(r, c, stride) KAT_IDX2((r), (c), (stride))

/* ============================================================================
 * Внутренний PRNG (xorshift64)
 * ============================================================================ */

static uint64_t kat_rng_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* Нормальное распределение (Box-Muller) */
static float kat_randn(uint64_t *state) {
    double u1 = (double)(kat_rng_next(state) & 0x7FFFFFFFULL) / (double)0x7FFFFFFFULL;
    double u2 = (double)(kat_rng_next(state) & 0x7FFFFFFFULL) / (double)0x7FFFFFFFULL;
    if (u1 < 1e-10) u1 = 1e-10;
    return (float)(sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2));
}

/* ============================================================================
 * LayerNorm
 * ============================================================================ */

/*
 * out[i] = gamma[i] * (x[i] - mean) / sqrt(var + eps) + beta[i]
 */
static void layer_norm(
    const float *x, float *out,
    const float *gamma, const float *beta,
    size_t dim
) {
    float mean = 0.0f;
    for (size_t i = 0; i < dim; i++) mean += x[i];
    mean /= (float)dim;

    float var = 0.0f;
    for (size_t i = 0; i < dim; i++) {
        float d = x[i] - mean;
        var += d * d;
    }
    var /= (float)dim;

    float inv_std = 1.0f / sqrtf(var + KAT_EPSILON);
    for (size_t i = 0; i < dim; i++) {
        out[i] = gamma[i] * (x[i] - mean) * inv_std + beta[i];
    }
}

/* ============================================================================
 * Активации
 * ============================================================================ */

/*
 * GELU: Gaussian Error Linear Unit
 * Быстрое приближение: x * 0.5 * (1 + tanh(sqrt(2/pi) * (x + 0.044715 * x^3)))
 */
static float gelu(float x) {
    float c = 0.7978845608f;  /* sqrt(2/pi) */
    float inner = c * (x + 0.044715f * x * x * x);
    return 0.5f * x * (1.0f + tanhf(inner));
}

/* ============================================================================
 * Softmax
 * ============================================================================ */

static void softmax(float *x, size_t n) {
    float max_val = -FLT_MAX;
    for (size_t i = 0; i < n; i++) {
        if (x[i] > max_val) max_val = x[i];
    }
    float sum = 0.0f;
    for (size_t i = 0; i < n; i++) {
        x[i] = expf(x[i] - max_val);
        sum += x[i];
    }
    float inv_sum = 1.0f / (sum + 1e-10f);
    for (size_t i = 0; i < n; i++) {
        x[i] *= inv_sum;
    }
}

/* ============================================================================
 * Инициализация Xavier
 * ============================================================================ */

/*
 * Xavier Uniform: U(-sqrt(6/(fan_in+fan_out)), sqrt(6/(fan_in+fan_out)))
 */
static void xavier_init(float *weights, size_t fan_in, size_t fan_out,
                         uint64_t *state) {
    float limit = sqrtf(6.0f / (float)(fan_in + fan_out));
    size_t total = fan_in * fan_out;
    for (size_t i = 0; i < total; i++) {
        float u = (float)(kat_rng_next(state) & 0xFFFFFF) / (float)0xFFFFFF;
        weights[i] = (2.0f * u - 1.0f) * limit;
    }
}

/* ============================================================================
 * Предустановленные конфигурации
 * ============================================================================ */

KatConfig kat_config_small(void) {
    return (KatConfig){
        .vocab_size = 256,
        .embed_dim  = 64,
        .num_heads  = 4,
        .head_dim   = 16,
        .ff_dim     = 256,
        .num_layers = 2,
        .max_seq    = 512
    };
}

KatConfig kat_config_medium(void) {
    return (KatConfig){
        .vocab_size = 256,
        .embed_dim  = 256,
        .num_heads  = 8,
        .head_dim   = 32,
        .ff_dim     = 1024,
        .num_layers = 8,
        .max_seq    = 512
    };
}

KatConfig kat_config_large(void) {
    return (KatConfig){
        .vocab_size = 256,
        .embed_dim  = 768,
        .num_heads  = 12,
        .head_dim   = 64,
        .ff_dim     = 3072,
        .num_layers = 14,
        .max_seq    = 1024
    };
}

size_t kat_config_count_params(const KatConfig *cfg) {
    if (!cfg) return 0;
    size_t total = 0;

    /* Эмбеддинги */
    total += (size_t)cfg->vocab_size * cfg->embed_dim;   /* token  */
    total += (size_t)cfg->max_seq * cfg->embed_dim;      /* pos    */

    /* Трансформерные блоки */
    for (int l = 0; l < cfg->num_layers; l++) {
        /* Attention: heads × (Wq + Wk + Wv) */
        total += (size_t)cfg->num_heads * 3 * cfg->embed_dim * cfg->head_dim;
        /* Wo */
        total += (size_t)cfg->embed_dim * cfg->embed_dim;
        /* LayerNorm (attention) */
        total += 2 * (size_t)cfg->embed_dim;

        /* FFN: W1 + b1 + W2 + b2 */
        total += (size_t)cfg->embed_dim * cfg->ff_dim;
        total += (size_t)cfg->ff_dim;
        total += (size_t)cfg->ff_dim * cfg->embed_dim;
        total += (size_t)cfg->embed_dim;
        /* LayerNorm (FFN) */
        total += 2 * (size_t)cfg->embed_dim;
    }

    /* Финальный LayerNorm */
    total += 2 * (size_t)cfg->embed_dim;

    /* LM head */
    total += (size_t)cfg->embed_dim * cfg->vocab_size;

    return total;
}

/* ============================================================================
 * Жизненный цикл модели (динамическая аллокация)
 * ============================================================================ */

/* Вспомогательная: аллоцировать float-массив с проверкой */
static float* alloc_floats(size_t count) {
    if (count == 0) return NULL;
    return (float*)calloc(count, sizeof(float));
}

/* Освобождение данных одного трансформерного слоя */
static void destroy_layer(KatTransformerBlock *block, int num_heads) {
    if (!block) return;

    /* Attention heads */
    if (block->attn.heads) {
        for (int h = 0; h < num_heads; h++) {
            free(block->attn.heads[h].wq);
            free(block->attn.heads[h].wk);
            free(block->attn.heads[h].wv);
        }
        free(block->attn.heads);
    }
    free(block->attn.wo);
    free(block->attn.ln_gamma);
    free(block->attn.ln_beta);

    /* FFN */
    free(block->ffn.w1);
    free(block->ffn.b1);
    free(block->ffn.w2);
    free(block->ffn.b2);
    free(block->ffn.ln_gamma);
    free(block->ffn.ln_beta);
}

KatModel* kat_model_create_ex(const KatConfig *cfg, uint64_t seed) {
    if (!cfg || cfg->embed_dim <= 0 || cfg->num_heads <= 0) return NULL;

    KatModel *model = (KatModel*)calloc(1, sizeof(KatModel));
    if (!model) return NULL;

    model->cfg = *cfg;
    model->seed = seed ? seed : 42;
    uint64_t *s = &model->seed;

    int V = cfg->vocab_size;
    int E = cfg->embed_dim;
    int H = cfg->num_heads;
    int D = cfg->head_dim;
    int F = cfg->ff_dim;
    int L = cfg->num_layers;
    int S = cfg->max_seq;

    /* --- Эмбеддинги --- */
    model->embed.token_embed = alloc_floats((size_t)V * E);
    model->embed.pos_embed   = alloc_floats((size_t)S * E);
    if (!model->embed.token_embed || !model->embed.pos_embed) goto fail;

    /* Токеновые: Xavier */
    xavier_init(model->embed.token_embed, V, E, s);

    /* Позиционные: sinusoidal (фиксированные) */
    for (int pos = 0; pos < S; pos++) {
        for (int i = 0; i < E; i++) {
            float angle = (float)pos / powf(10000.0f,
                          2.0f * (float)(i / 2) / (float)E);
            model->embed.pos_embed[IDX2(pos, i, E)] =
                (i % 2 == 0) ? sinf(angle) : cosf(angle);
        }
    }

    /* --- Трансформерные блоки --- */
    model->layers = (KatTransformerBlock*)calloc(L, sizeof(KatTransformerBlock));
    if (!model->layers) goto fail;

    for (int layer = 0; layer < L; layer++) {
        KatTransformerBlock *block = &model->layers[layer];

        /* Attention heads */
        block->attn.heads = (KatAttentionHead*)calloc(H, sizeof(KatAttentionHead));
        if (!block->attn.heads) goto fail;

        for (int h = 0; h < H; h++) {
            KatAttentionHead *head = &block->attn.heads[h];
            head->wq = alloc_floats((size_t)E * D);
            head->wk = alloc_floats((size_t)E * D);
            head->wv = alloc_floats((size_t)E * D);
            if (!head->wq || !head->wk || !head->wv) goto fail;

            xavier_init(head->wq, E, D, s);
            xavier_init(head->wk, E, D, s);
            xavier_init(head->wv, E, D, s);
        }

        block->attn.wo       = alloc_floats((size_t)E * E);
        block->attn.ln_gamma = alloc_floats(E);
        block->attn.ln_beta  = alloc_floats(E);
        if (!block->attn.wo || !block->attn.ln_gamma || !block->attn.ln_beta)
            goto fail;

        xavier_init(block->attn.wo, E, E, s);
        for (int i = 0; i < E; i++) {
            block->attn.ln_gamma[i] = 1.0f;
            block->attn.ln_beta[i]  = 0.0f;
        }

        /* Feed-Forward Network */
        block->ffn.w1       = alloc_floats((size_t)E * F);
        block->ffn.b1       = alloc_floats(F);
        block->ffn.w2       = alloc_floats((size_t)F * E);
        block->ffn.b2       = alloc_floats(E);
        block->ffn.ln_gamma = alloc_floats(E);
        block->ffn.ln_beta  = alloc_floats(E);
        if (!block->ffn.w1 || !block->ffn.b1 || !block->ffn.w2 ||
            !block->ffn.b2 || !block->ffn.ln_gamma || !block->ffn.ln_beta)
            goto fail;

        xavier_init(block->ffn.w1, E, F, s);
        /* b1 — нули (calloc) */
        xavier_init(block->ffn.w2, F, E, s);
        /* b2 — нули (calloc) */
        for (int i = 0; i < E; i++) {
            block->ffn.ln_gamma[i] = 1.0f;
            block->ffn.ln_beta[i]  = 0.0f;
        }
    }

    /* --- Финальный LayerNorm --- */
    model->final_ln_gamma = alloc_floats(E);
    model->final_ln_beta  = alloc_floats(E);
    if (!model->final_ln_gamma || !model->final_ln_beta) goto fail;

    for (int i = 0; i < E; i++) {
        model->final_ln_gamma[i] = 1.0f;
        model->final_ln_beta[i]  = 0.0f;
    }

    /* --- Language Model Head --- */
    model->lm_head = alloc_floats((size_t)E * V);
    if (!model->lm_head) goto fail;
    xavier_init(model->lm_head, E, V, s);

    /* Подсчёт параметров */
    model->param_count = kat_config_count_params(cfg);

    return model;

fail:
    kat_model_destroy(model);
    return NULL;
}

/** Обратная совместимость: создать small (~165K) модель */
KatModel* kat_model_create(uint64_t seed) {
    KatConfig cfg = kat_config_small();
    return kat_model_create_ex(&cfg, seed);
}

void kat_model_destroy(KatModel *model) {
    if (!model) return;

    /* Эмбеддинги */
    free(model->embed.token_embed);
    free(model->embed.pos_embed);

    /* Трансформерные блоки */
    if (model->layers) {
        for (int l = 0; l < model->cfg.num_layers; l++) {
            destroy_layer(&model->layers[l], model->cfg.num_heads);
        }
        free(model->layers);
    }

    free(model->final_ln_gamma);
    free(model->final_ln_beta);
    free(model->lm_head);
    free(model);
}

/* ============================================================================
 * Рабочий буфер (workspace)
 * ============================================================================ */

KatWorkspace* kat_workspace_create_ex(const KatConfig *cfg) {
    if (!cfg) return NULL;

    KatWorkspace *ws = (KatWorkspace*)calloc(1, sizeof(KatWorkspace));
    if (!ws) return NULL;

    ws->cfg = *cfg;

    int E = cfg->embed_dim;
    int D = cfg->head_dim;
    int F = cfg->ff_dim;
    int S = cfg->max_seq;
    int V = cfg->vocab_size;

    ws->hidden         = alloc_floats((size_t)S * E);
    ws->residual       = alloc_floats((size_t)S * E);
    ws->q              = alloc_floats((size_t)S * D);
    ws->k              = alloc_floats((size_t)S * D);
    ws->v              = alloc_floats((size_t)S * D);
    ws->attn_scores    = alloc_floats((size_t)S * S);
    ws->attn_out       = alloc_floats((size_t)S * E);
    ws->ff_hidden      = alloc_floats((size_t)S * F);
    ws->logits         = alloc_floats(V);
    ws->probs          = alloc_floats(V);
    ws->mha_normed     = alloc_floats((size_t)S * E);
    ws->ffn_normed     = alloc_floats((size_t)S * E);
    ws->layer_attn_out = alloc_floats((size_t)S * E);
    ws->layer_ffn_out  = alloc_floats((size_t)S * E);
    ws->head_out       = alloc_floats((size_t)S * D);

    /* Проверка всех аллокаций */
    if (!ws->hidden || !ws->residual || !ws->q || !ws->k || !ws->v ||
        !ws->attn_scores || !ws->attn_out || !ws->ff_hidden ||
        !ws->logits || !ws->probs || !ws->mha_normed || !ws->ffn_normed ||
        !ws->layer_attn_out || !ws->layer_ffn_out || !ws->head_out) {
        kat_workspace_destroy(ws);
        return NULL;
    }

    return ws;
}

/** Обратная совместимость: создать workspace под small конфигурацию */
KatWorkspace* kat_workspace_create(void) {
    KatConfig cfg = kat_config_small();
    return kat_workspace_create_ex(&cfg);
}

void kat_workspace_destroy(KatWorkspace *ws) {
    if (!ws) return;
    free(ws->hidden);
    free(ws->residual);
    free(ws->q);
    free(ws->k);
    free(ws->v);
    free(ws->attn_scores);
    free(ws->attn_out);
    free(ws->ff_hidden);
    free(ws->logits);
    free(ws->probs);
    free(ws->mha_normed);
    free(ws->ffn_normed);
    free(ws->layer_attn_out);
    free(ws->layer_ffn_out);
    free(ws->head_out);
    free(ws);
}

/* ============================================================================
 * Forward Pass (динамические размеры)
 * ============================================================================ */

/*
 * Одна голова Self-Attention (каузальная):
 *   Q = X @ Wq,  K = X @ Wk,  V = X @ Wv
 *   scores = Q @ K^T / sqrt(head_dim) + mask
 *   attn = softmax(scores) @ V
 */
static void attention_head_forward(
    const KatAttentionHead *head,
    const float *hidden,       /* [seq_len × embed_dim] flat */
    size_t seq_len,
    const KatConfig *cfg,
    float *q_out,              /* [seq_len × head_dim]  flat */
    float *k_out,
    float *v_out,
    float *scores,             /* [seq_len × max_seq]   flat */
    float *out                 /* [seq_len × head_dim]  flat */
) {
    int E = cfg->embed_dim;
    int D = cfg->head_dim;
    int S = cfg->max_seq;

    /* Проекции Q, K, V */
    for (size_t t = 0; t < seq_len; t++) {
        for (int d = 0; d < D; d++) {
            float sq = 0.0f, sk = 0.0f, sv = 0.0f;
            for (int j = 0; j < E; j++) {
                float h = hidden[IDX2(t, j, E)];
                sq += h * head->wq[IDX2(j, d, D)];
                sk += h * head->wk[IDX2(j, d, D)];
                sv += h * head->wv[IDX2(j, d, D)];
            }
            q_out[IDX2(t, d, D)] = sq;
            k_out[IDX2(t, d, D)] = sk;
            v_out[IDX2(t, d, D)] = sv;
        }
    }

    /* Масштаб: 1/sqrt(head_dim) */
    float scale = 1.0f / sqrtf((float)D);

    /* Матрица внимания: scores[i][j] = dot(Q[i], K[j]) * scale */
    for (size_t i = 0; i < seq_len; i++) {
        for (size_t j = 0; j < seq_len; j++) {
            if (j > i) {
                /* Каузальная маска: будущие позиции = -inf */
                scores[IDX2(i, j, S)] = -1e9f;
            } else {
                float dot = 0.0f;
                for (int d = 0; d < D; d++) {
                    dot += q_out[IDX2(i, d, D)] * k_out[IDX2(j, d, D)];
                }
                scores[IDX2(i, j, S)] = dot * scale;
            }
        }
        /* Softmax по строке */
        softmax(&scores[i * (size_t)S], seq_len);
    }

    /* Взвешенная сумма values: out[i] = sum_j(scores[i][j] * V[j]) */
    for (size_t i = 0; i < seq_len; i++) {
        for (int d = 0; d < D; d++) {
            float sum = 0.0f;
            for (size_t j = 0; j <= i; j++) {
                sum += scores[IDX2(i, j, S)] * v_out[IDX2(j, d, D)];
            }
            out[IDX2(i, d, D)] = sum;
        }
    }
}

/*
 * Multi-Head Attention: конкатенация голов + выходная проекция
 */
static void multi_head_attention_forward(
    const KatMultiHeadAttention *mha,
    const float *input,        /* [seq_len × embed_dim] flat */
    size_t seq_len,
    const KatConfig *cfg,
    KatWorkspace *ws,
    float *output              /* [seq_len × embed_dim] flat */
) {
    int E = cfg->embed_dim;
    int D = cfg->head_dim;
    int H = cfg->num_heads;

    /* LayerNorm перед attention (Pre-LN) */
    for (size_t t = 0; t < seq_len; t++) {
        layer_norm(&input[t * E], &ws->mha_normed[t * E],
                   mha->ln_gamma, mha->ln_beta, E);
    }

    /* Обнуляем выход */
    memset(output, 0, seq_len * E * sizeof(float));

    /* Каждая голова обрабатывает своё подпространство */
    for (int h = 0; h < H; h++) {
        attention_head_forward(
            &mha->heads[h], ws->mha_normed, seq_len, cfg,
            ws->q, ws->k, ws->v, ws->attn_scores,
            ws->head_out
        );

        /* Конкатенация: записать head_out в соответствующие позиции */
        size_t offset = (size_t)h * D;
        for (size_t t = 0; t < seq_len; t++) {
            for (int d = 0; d < D; d++) {
                ws->attn_out[IDX2(t, offset + d, E)] =
                    ws->head_out[IDX2(t, d, D)];
            }
        }
    }

    /* Выходная проекция: Wo */
    for (size_t t = 0; t < seq_len; t++) {
        for (int i = 0; i < E; i++) {
            float sum = 0.0f;
            for (int j = 0; j < E; j++) {
                sum += ws->attn_out[IDX2(t, j, E)] * mha->wo[IDX2(j, i, E)];
            }
            output[IDX2(t, i, E)] = sum;
        }
    }
}

/*
 * Feed-Forward Network: GELU(x @ W1 + b1) @ W2 + b2
 */
static void feed_forward_forward(
    const KatFeedForward *ffn,
    const float *input,        /* [seq_len × embed_dim] flat */
    size_t seq_len,
    const KatConfig *cfg,
    KatWorkspace *ws,
    float *output              /* [seq_len × embed_dim] flat */
) {
    int E = cfg->embed_dim;
    int F = cfg->ff_dim;

    /* LayerNorm перед FFN (Pre-LN) */
    for (size_t t = 0; t < seq_len; t++) {
        layer_norm(&input[t * E], &ws->ffn_normed[t * E],
                   ffn->ln_gamma, ffn->ln_beta, E);
    }

    /* Первый слой: hidden = GELU(x @ W1 + b1) */
    for (size_t t = 0; t < seq_len; t++) {
        for (int j = 0; j < F; j++) {
            float sum = ffn->b1[j];
            for (int i = 0; i < E; i++) {
                sum += ws->ffn_normed[IDX2(t, i, E)] * ffn->w1[IDX2(i, j, F)];
            }
            ws->ff_hidden[IDX2(t, j, F)] = gelu(sum);
        }
    }

    /* Второй слой: output = hidden @ W2 + b2 */
    for (size_t t = 0; t < seq_len; t++) {
        for (int i = 0; i < E; i++) {
            float sum = ffn->b2[i];
            for (int j = 0; j < F; j++) {
                sum += ws->ff_hidden[IDX2(t, j, F)] * ffn->w2[IDX2(j, i, E)];
            }
            output[IDX2(t, i, E)] = sum;
        }
    }
}

/* ============================================================================
 * Основной forward pass
 * ============================================================================ */

int kat_forward(const KatModel *model, KatWorkspace *ws,
                const uint8_t *tokens, size_t seq_len) {
    if (!model || !ws || !tokens || seq_len == 0) return -1;

    const KatConfig *cfg = &model->cfg;
    int E = cfg->embed_dim;
    int V = cfg->vocab_size;
    int L = cfg->num_layers;

    if (seq_len > (size_t)cfg->max_seq) seq_len = (size_t)cfg->max_seq;
    ws->seq_len = seq_len;

    /* --- Шаг 1: Эмбеддинги (токен + позиция) --- */
    for (size_t t = 0; t < seq_len; t++) {
        int tok = tokens[t];
        if (tok >= V) tok = 0;  /* безопасная граница */
        for (int d = 0; d < E; d++) {
            ws->hidden[IDX2(t, d, E)] =
                model->embed.token_embed[IDX2(tok, d, E)] +
                model->embed.pos_embed[IDX2(t, d, E)];
        }
    }

    /* --- Шаг 2: Стек трансформерных блоков --- */
    for (int layer = 0; layer < L; layer++) {
        const KatTransformerBlock *block = &model->layers[layer];

        /* Сохраняем residual */
        memcpy(ws->residual, ws->hidden,
               seq_len * E * sizeof(float));

        /* Multi-Head Attention */
        multi_head_attention_forward(
            &block->attn, ws->hidden, seq_len, cfg, ws,
            ws->layer_attn_out
        );

        /* Residual connection после attention */
        for (size_t t = 0; t < seq_len; t++) {
            for (int d = 0; d < E; d++) {
                size_t idx = IDX2(t, d, E);
                ws->hidden[idx] = ws->residual[idx] + ws->layer_attn_out[idx];
            }
        }

        /* Сохраняем residual перед FFN */
        memcpy(ws->residual, ws->hidden,
               seq_len * E * sizeof(float));

        /* Feed-Forward Network */
        feed_forward_forward(
            &block->ffn, ws->hidden, seq_len, cfg, ws,
            ws->layer_ffn_out
        );

        /* Residual connection после FFN */
        for (size_t t = 0; t < seq_len; t++) {
            for (int d = 0; d < E; d++) {
                size_t idx = IDX2(t, d, E);
                ws->hidden[idx] = ws->residual[idx] + ws->layer_ffn_out[idx];
            }
        }
    }

    /* --- Шаг 3: Финальный LayerNorm --- */
    /* Используем начало layer_attn_out как временный буфер */
    for (size_t t = 0; t < seq_len; t++) {
        float *tmp = ws->layer_attn_out;   /* переиспользуем буфер */
        layer_norm(&ws->hidden[t * E], tmp,
                   model->final_ln_gamma, model->final_ln_beta, E);
        memcpy(&ws->hidden[t * E], tmp, E * sizeof(float));
    }

    /* --- Шаг 4: LM Head — логиты для следующего токена --- */
    size_t last = seq_len - 1;
    for (int vi = 0; vi < V; vi++) {
        float sum = 0.0f;
        for (int d = 0; d < E; d++) {
            sum += ws->hidden[IDX2(last, d, E)] * model->lm_head[IDX2(d, vi, V)];
        }
        ws->logits[vi] = sum;
    }

    /* Softmax для вероятностей */
    memcpy(ws->probs, ws->logits, V * sizeof(float));
    softmax(ws->probs, V);

    return 0;
}

/* ============================================================================
 * Извлечение эмбеддинга (mean pooling)
 * ============================================================================ */

void kat_extract_embedding(const KatWorkspace *ws, float *out) {
    if (!ws || !out || ws->seq_len == 0) return;

    int E = ws->cfg.embed_dim;

    /* Среднее по всем позициям */
    memset(out, 0, E * sizeof(float));
    for (size_t t = 0; t < ws->seq_len; t++) {
        for (int d = 0; d < E; d++) {
            out[d] += ws->hidden[IDX2(t, d, E)];
        }
    }
    float inv_n = 1.0f / (float)ws->seq_len;
    for (int d = 0; d < E; d++) {
        out[d] *= inv_n;
    }
}

/* ============================================================================
 * Сэмплирование
 * ============================================================================ */

uint8_t kat_sample(KatModel *model, const KatWorkspace *ws,
                   float temperature) {
    if (!model || !ws) return 0;

    int V = ws->cfg.vocab_size;

    if (temperature < 0.01f) {
        /* Greedy: argmax */
        uint8_t best = 0;
        float best_p = ws->probs[0];
        for (int i = 1; i < V; i++) {
            if (ws->probs[i] > best_p) {
                best_p = ws->probs[i];
                best = (uint8_t)i;
            }
        }
        return best;
    }

    /* Температурное сэмплирование */
    float *scaled = (float*)malloc(V * sizeof(float));
    if (!scaled) return 0;

    memcpy(scaled, ws->logits, V * sizeof(float));
    float inv_t = 1.0f / temperature;
    for (int i = 0; i < V; i++) {
        scaled[i] *= inv_t;
    }
    softmax(scaled, V);

    /* Выбор по кумулятивной вероятности */
    float r = (float)(kat_rng_next(&model->seed) & 0xFFFFFF) / (float)0xFFFFFF;
    float cumsum = 0.0f;
    uint8_t result = (uint8_t)(V - 1);
    for (int i = 0; i < V; i++) {
        cumsum += scaled[i];
        if (cumsum >= r) {
            result = (uint8_t)i;
            break;
        }
    }

    free(scaled);
    return result;
}

/* ============================================================================
 * Обучение
 * ============================================================================ */

/*
 * Приближённое обучение через «прямую оценку градиента»
 * (Evolutionary Gradient Estimation)
 *
 * Для каждого параметра:
 *   1. Пертурбация параметра на +epsilon
 *   2. Измерение изменения loss
 *   3. Обновление: param -= lr * (delta_loss / epsilon)
 */

static float compute_loss(const KatModel *model, KatWorkspace *ws,
                          const uint8_t *tokens, size_t seq_len,
                          uint8_t target) {
    kat_forward(model, ws, tokens, seq_len);
    /* Cross-entropy: -log(P(target)) */
    float p = ws->probs[target];
    if (p < 1e-10f) p = 1e-10f;
    return -logf(p);
}

float kat_train_step(KatModel *model, KatWorkspace *ws,
                     const uint8_t *tokens, size_t seq_len,
                     uint8_t target, float lr) {
    if (!model || !ws || !tokens || seq_len == 0) return 0.0f;

    const KatConfig *cfg = &model->cfg;

    /* Текущий loss */
    float base_loss = compute_loss(model, ws, tokens, seq_len, target);

    /*
     * Стохастическая оценка градиента:
     * Сэмплируем N_PROBE случайных параметров из LM head
     */
    #define N_PROBE 16
    float epsilon = 1e-3f;

    float *lm_params = model->lm_head;
    size_t lm_total = (size_t)cfg->embed_dim * cfg->vocab_size;

    for (int p = 0; p < N_PROBE; p++) {
        size_t idx = kat_rng_next(&model->seed) % lm_total;

        /* Пертурбация */
        float orig = lm_params[idx];
        lm_params[idx] = orig + epsilon;
        float loss_plus = compute_loss(model, ws, tokens, seq_len, target);
        lm_params[idx] = orig;

        /* Оценка градиента */
        float grad = (loss_plus - base_loss) / epsilon;

        /* SGD обновление */
        lm_params[idx] -= lr * grad;
    }

    /*
     * Также обновляем эмбеддинги входных токенов
     * (прямой градиент по использованным токенам — только последние 4)
     */
    size_t start_t = seq_len > 4 ? seq_len - 4 : 0;
    for (size_t t = start_t; t < seq_len; t++) {
        int tok = tokens[t];
        if (tok >= cfg->vocab_size) tok = 0;
        float *emb = &model->embed.token_embed[(size_t)tok * cfg->embed_dim];
        for (int d = 0; d < cfg->embed_dim; d += 16) {
            float orig = emb[d];
            emb[d] = orig + epsilon;
            float loss_p = compute_loss(model, ws, tokens, seq_len, target);
            emb[d] = orig;
            float grad = (loss_p - base_loss) / epsilon;
            emb[d] -= lr * grad;
        }
    }

    #undef N_PROBE

    return base_loss;
}

/* ============================================================================
 * Быстрое обучение: 1 forward + аналитический градиент
 * ============================================================================ */

/*
 * Вместо SPSA (20 forward проходов) — прямой градиент cross-entropy
 * по LM head и эмбеддингам последнего токена.
 *
 * Градиент softmax cross-entropy по logit_i:
 *   dL/dlogit_i = probs[i] - (i == target ? 1 : 0)
 *
 * Градиент по W_lm[d][v] = hidden[last][d] * dL/dlogit_v
 * Градиент по embed[tok][d] = sum_v(W_lm[d][v] * dL/dlogit_v)  (через W_lm)
 */
float kat_train_step_fast(KatModel *model, KatWorkspace *ws,
                          const uint8_t *tokens, size_t seq_len,
                          uint8_t target, float lr) {
    if (!model || !ws || !tokens || seq_len == 0) return 0.0f;

    const KatConfig *cfg = &model->cfg;
    int E = cfg->embed_dim;
    int V = cfg->vocab_size;

    /* 1 forward pass */
    kat_forward(model, ws, tokens, seq_len);

    /* Cross-entropy loss */
    float p_target = ws->probs[target];
    if (p_target < 1e-10f) p_target = 1e-10f;
    float loss = -logf(p_target);

    /* Градиент softmax CE: dL/dlogit[v] = probs[v] - one_hot[v] */
    float *dlogits = (float *)malloc((size_t)V * sizeof(float));
    if (!dlogits) return loss;

    for (int v = 0; v < V; v++) {
        dlogits[v] = ws->probs[v];
    }
    dlogits[target] -= 1.0f;

    /* Последняя позиция hidden (после forward) */
    size_t last = (ws->seq_len > 0 ? ws->seq_len : seq_len) - 1;
    float *h_last = &ws->hidden[last * (size_t)E];

    /* Обновление LM head: W[d][v] -= lr * h[d] * dlogits[v] */
    for (int d = 0; d < E; d++) {
        float hd = h_last[d];
        for (int v = 0; v < V; v++) {
            model->lm_head[d * V + v] -= lr * hd * dlogits[v];
        }
    }

    /* Обновление эмбеддинга последнего токена: сигнал через LM head */
    int tok = tokens[seq_len - 1];
    if (tok < V) {
        float *emb = &model->embed.token_embed[(size_t)tok * E];
        for (int d = 0; d < E; d++) {
            float grad_d = 0.0f;
            for (int v = 0; v < V; v++) {
                grad_d += model->lm_head[d * V + v] * dlogits[v];
            }
            emb[d] -= lr * 0.1f * grad_d;  /* Меньший LR для эмбеддингов */
        }
    }

    free(dlogits);
    return loss;
}

/* ============================================================================
 * Полный Backpropagation через все слои трансформера
 * ============================================================================ */

/*
 * Производная GELU:
 *   gelu(x) = 0.5 * x * (1 + tanh(c * (x + 0.044715 * x^3)))
 *   gelu'(x) = 0.5*(1+tanh(u)) + 0.5*x*(1-tanh(u)^2)*u'
 *   где u = c*(x + 0.044715*x^3), u' = c*(1 + 3*0.044715*x^2)
 */
static float gelu_grad(float x) {
    float c = 0.7978845608f;  /* sqrt(2/pi) */
    float x3 = x * x * x;
    float u = c * (x + 0.044715f * x3);
    float tanh_u = tanhf(u);
    float du = c * (1.0f + 3.0f * 0.044715f * x * x);
    return 0.5f * (1.0f + tanh_u) + 0.5f * x * (1.0f - tanh_u * tanh_u) * du;
}

/*
 * Обратный проход LayerNorm:
 *   y = gamma * (x - mean) / sqrt(var + eps) + beta
 *
 * Входы:  dy[dim] — градиент по выходу
 *         x[dim]  — исходный вход в LayerNorm
 *         gamma[dim] — масштаб
 * Выходы: dx[dim]     — градиент по входу (накапливается += )
 *         dgamma[dim] — градиент по gamma (накапливается += )
 *         dbeta[dim]  — градиент по beta  (накапливается += )
 */
static void layer_norm_backward(
    const float *dy,      /* [dim] градиент по выходу */
    const float *x,       /* [dim] вход в LayerNorm */
    const float *gamma,   /* [dim] масштаб */
    float *dx,            /* [dim] градиент по входу (+=) */
    float *dgamma,        /* [dim] градиент по gamma (+=) */
    float *dbeta,         /* [dim] градиент по beta (+=) */
    size_t dim
) {
    /* Пересчитаем mean и var */
    float mean = 0.0f;
    for (size_t i = 0; i < dim; i++) mean += x[i];
    mean /= (float)dim;

    float var = 0.0f;
    for (size_t i = 0; i < dim; i++) {
        float d = x[i] - mean;
        var += d * d;
    }
    var /= (float)dim;

    float inv_std = 1.0f / sqrtf(var + KAT_EPSILON);
    float inv_n = 1.0f / (float)dim;

    /* x_hat[i] = (x[i] - mean) * inv_std */
    /* dgamma[i] += dy[i] * x_hat[i] */
    /* dbeta[i]  += dy[i] */
    float sum_dy_xhat = 0.0f;
    float sum_dy_gamma = 0.0f;

    for (size_t i = 0; i < dim; i++) {
        float x_hat = (x[i] - mean) * inv_std;
        dgamma[i] += dy[i] * x_hat;
        dbeta[i]  += dy[i];
        float dy_g = dy[i] * gamma[i];
        sum_dy_xhat  += dy_g * x_hat;
        sum_dy_gamma += dy_g;
    }

    /* dx[i] += gamma[i] * inv_std * (dy[i] - inv_n*(sum_dy_gamma + x_hat_i * sum_dy_xhat)) */
    for (size_t i = 0; i < dim; i++) {
        float x_hat = (x[i] - mean) * inv_std;
        float dy_g = dy[i] * gamma[i];
        dx[i] += inv_std * (dy_g - inv_n * (sum_dy_gamma + x_hat * sum_dy_xhat));
    }
}

/*
 * Обратный проход softmax (для одной строки):
 *   dz[j] = s[j] * (ds[j] - sum_k(ds[k] * s[k]))
 *   где s = softmax(z), ds = dL/ds
 *
 * Записывает результат в dz (перезаписывает).
 */
static void softmax_backward(
    const float *s,    /* [n] softmax выход */
    const float *ds,   /* [n] градиент по softmax выходу */
    float *dz,         /* [n] выход: градиент по logits (перезапись) */
    size_t n
) {
    float dot = 0.0f;
    for (size_t i = 0; i < n; i++) {
        dot += ds[i] * s[i];
    }
    for (size_t i = 0; i < n; i++) {
        dz[i] = s[i] * (ds[i] - dot);
    }
}

/*
 * Кэш активаций для обучения.
 * Хранит все промежуточные значения forward pass, нужные для backward.
 */
typedef struct {
    int num_layers;
    int num_heads;
    size_t seq_len;
    KatConfig cfg;

    /* Per-layer [num_layers] */
    float **layer_input;      /* [seq_len * E] — вход в каждый блок */
    float **attn_normed;      /* [seq_len * E] — после LN перед attention */
    float **attn_concat;      /* [seq_len * E] — конкатенация голов до Wo */
    float **mid_hidden;       /* [seq_len * E] — после attn residual */
    float **ffn_normed;       /* [seq_len * E] — после LN перед FFN */
    float **ffn_pre_gelu;     /* [seq_len * F] — вход в GELU */
    float **ffn_post_gelu;    /* [seq_len * F] — выход GELU */

    /* Per-layer per-head [num_layers * num_heads] */
    float **head_q;           /* [seq_len * D] */
    float **head_k;           /* [seq_len * D] */
    float **head_v;           /* [seq_len * D] */
    float **head_scores;      /* [seq_len * S] (после softmax) */

    /* Final LN */
    float *final_ln_input;    /* [seq_len * E] — вход в финальный LN */
} KatTrainCache;

static KatTrainCache* train_cache_create(const KatConfig *cfg, size_t seq_len) {
    KatTrainCache *tc = (KatTrainCache*)calloc(1, sizeof(KatTrainCache));
    if (!tc) return NULL;

    int L = cfg->num_layers;
    int H = cfg->num_heads;
    int E = cfg->embed_dim;
    int D = cfg->head_dim;
    int F = cfg->ff_dim;
    int S = cfg->max_seq;

    tc->num_layers = L;
    tc->num_heads  = H;
    tc->seq_len    = seq_len;
    tc->cfg        = *cfg;

    /* Аллокация per-layer буферов */
    tc->layer_input   = (float**)calloc(L, sizeof(float*));
    tc->attn_normed   = (float**)calloc(L, sizeof(float*));
    tc->attn_concat   = (float**)calloc(L, sizeof(float*));
    tc->mid_hidden    = (float**)calloc(L, sizeof(float*));
    tc->ffn_normed    = (float**)calloc(L, sizeof(float*));
    tc->ffn_pre_gelu  = (float**)calloc(L, sizeof(float*));
    tc->ffn_post_gelu = (float**)calloc(L, sizeof(float*));

    for (int l = 0; l < L; l++) {
        tc->layer_input[l]   = alloc_floats(seq_len * E);
        tc->attn_normed[l]   = alloc_floats(seq_len * E);
        tc->attn_concat[l]   = alloc_floats(seq_len * E);
        tc->mid_hidden[l]    = alloc_floats(seq_len * E);
        tc->ffn_normed[l]    = alloc_floats(seq_len * E);
        tc->ffn_pre_gelu[l]  = alloc_floats(seq_len * F);
        tc->ffn_post_gelu[l] = alloc_floats(seq_len * F);
    }

    /* Per-layer per-head буферы */
    int LH = L * H;
    tc->head_q      = (float**)calloc(LH, sizeof(float*));
    tc->head_k      = (float**)calloc(LH, sizeof(float*));
    tc->head_v      = (float**)calloc(LH, sizeof(float*));
    tc->head_scores = (float**)calloc(LH, sizeof(float*));

    for (int i = 0; i < LH; i++) {
        tc->head_q[i]      = alloc_floats(seq_len * D);
        tc->head_k[i]      = alloc_floats(seq_len * D);
        tc->head_v[i]      = alloc_floats(seq_len * D);
        tc->head_scores[i] = alloc_floats(seq_len * S);
    }

    tc->final_ln_input = alloc_floats(seq_len * E);

    return tc;
}

static void train_cache_destroy(KatTrainCache *tc) {
    if (!tc) return;
    int L = tc->num_layers;
    int H = tc->num_heads;
    int LH = L * H;

    for (int l = 0; l < L; l++) {
        free(tc->layer_input[l]);
        free(tc->attn_normed[l]);
        free(tc->attn_concat[l]);
        free(tc->mid_hidden[l]);
        free(tc->ffn_normed[l]);
        free(tc->ffn_pre_gelu[l]);
        free(tc->ffn_post_gelu[l]);
    }
    free(tc->layer_input);
    free(tc->attn_normed);
    free(tc->attn_concat);
    free(tc->mid_hidden);
    free(tc->ffn_normed);
    free(tc->ffn_pre_gelu);
    free(tc->ffn_post_gelu);

    for (int i = 0; i < LH; i++) {
        free(tc->head_q[i]);
        free(tc->head_k[i]);
        free(tc->head_v[i]);
        free(tc->head_scores[i]);
    }
    free(tc->head_q);
    free(tc->head_k);
    free(tc->head_v);
    free(tc->head_scores);

    free(tc->final_ln_input);
    free(tc);
}

/*
 * Ограничение нормы градиента (gradient clipping by global norm)
 */
static void clip_grad(float *grad, size_t n, float max_norm) {
    float sum_sq = 0.0f;
    for (size_t i = 0; i < n; i++) {
        sum_sq += grad[i] * grad[i];
    }
    float norm = sqrtf(sum_sq + 1e-12f);
    if (norm > max_norm) {
        float scale = max_norm / norm;
        for (size_t i = 0; i < n; i++) {
            grad[i] *= scale;
        }
    }
}

/*
 * Training forward pass с кэшированием промежуточных результатов.
 * Идентичен kat_forward, но сохраняет все активации в tc.
 */
static int training_forward(
    const KatModel *model, KatWorkspace *ws, KatTrainCache *tc,
    const uint8_t *tokens, size_t seq_len
) {
    const KatConfig *cfg = &model->cfg;
    int E = cfg->embed_dim;
    int V = cfg->vocab_size;
    int L = cfg->num_layers;
    int H = cfg->num_heads;
    int D = cfg->head_dim;
    int S = cfg->max_seq;

    if (seq_len > (size_t)S) seq_len = (size_t)S;
    ws->seq_len = seq_len;
    tc->seq_len = seq_len;

    /* --- Шаг 1: Эмбеддинги --- */
    for (size_t t = 0; t < seq_len; t++) {
        int tok = tokens[t];
        if (tok >= V) tok = 0;
        for (int d = 0; d < E; d++) {
            ws->hidden[IDX2(t, d, E)] =
                model->embed.token_embed[IDX2(tok, d, E)] +
                model->embed.pos_embed[IDX2(t, d, E)];
        }
    }

    /* --- Шаг 2: Стек блоков (с кэшированием) --- */
    for (int layer = 0; layer < L; layer++) {
        const KatTransformerBlock *block = &model->layers[layer];

        /* Сохраняем вход слоя */
        memcpy(tc->layer_input[layer], ws->hidden,
               seq_len * E * sizeof(float));

        /* === Attention sub-block === */

        /* Pre-LN: attention LayerNorm */
        for (size_t t = 0; t < seq_len; t++) {
            layer_norm(&ws->hidden[t * E],
                       &tc->attn_normed[layer][t * E],
                       block->attn.ln_gamma, block->attn.ln_beta, E);
        }

        /* Каждая голова */
        memset(ws->attn_out, 0, seq_len * E * sizeof(float));

        for (int h = 0; h < H; h++) {
            int lh = layer * H + h;
            const KatAttentionHead *head = &block->attn.heads[h];
            float *qbuf = tc->head_q[lh];
            float *kbuf = tc->head_k[lh];
            float *vbuf = tc->head_v[lh];
            float *sbuf = tc->head_scores[lh];

            /* Q, K, V проекции */
            for (size_t t = 0; t < seq_len; t++) {
                for (int d = 0; d < D; d++) {
                    float sq = 0.0f, sk = 0.0f, sv = 0.0f;
                    for (int j = 0; j < E; j++) {
                        float hn = tc->attn_normed[layer][IDX2(t, j, E)];
                        sq += hn * head->wq[IDX2(j, d, D)];
                        sk += hn * head->wk[IDX2(j, d, D)];
                        sv += hn * head->wv[IDX2(j, d, D)];
                    }
                    qbuf[IDX2(t, d, D)] = sq;
                    kbuf[IDX2(t, d, D)] = sk;
                    vbuf[IDX2(t, d, D)] = sv;
                }
            }

            /* Attention scores + causal mask + softmax */
            float scale = 1.0f / sqrtf((float)D);
            for (size_t i = 0; i < seq_len; i++) {
                for (size_t j = 0; j < seq_len; j++) {
                    if (j > i) {
                        sbuf[IDX2(i, j, S)] = -1e9f;
                    } else {
                        float dot = 0.0f;
                        for (int d = 0; d < D; d++) {
                            dot += qbuf[IDX2(i, d, D)] * kbuf[IDX2(j, d, D)];
                        }
                        sbuf[IDX2(i, j, S)] = dot * scale;
                    }
                }
                softmax(&sbuf[i * (size_t)S], seq_len);
            }

            /* Weighted sum of V → head_out */
            size_t offset = (size_t)h * D;
            for (size_t i = 0; i < seq_len; i++) {
                for (int d = 0; d < D; d++) {
                    float sum = 0.0f;
                    for (size_t j = 0; j <= i; j++) {
                        sum += sbuf[IDX2(i, j, S)] * vbuf[IDX2(j, d, D)];
                    }
                    /* Сразу в attn_concat */
                    ws->attn_out[IDX2(i, offset + d, E)] = sum;
                }
            }
        }

        /* Сохраняем concat до проекции Wo */
        memcpy(tc->attn_concat[layer], ws->attn_out,
               seq_len * E * sizeof(float));

        /* Выходная проекция: attn_out @ Wo → layer_attn_out */
        for (size_t t = 0; t < seq_len; t++) {
            for (int i = 0; i < E; i++) {
                float sum = 0.0f;
                for (int j = 0; j < E; j++) {
                    sum += ws->attn_out[IDX2(t, j, E)] *
                           block->attn.wo[IDX2(j, i, E)];
                }
                ws->layer_attn_out[IDX2(t, i, E)] = sum;
            }
        }

        /* Residual connection после attention */
        for (size_t t = 0; t < seq_len; t++) {
            for (int d = 0; d < E; d++) {
                size_t idx = IDX2(t, d, E);
                ws->hidden[idx] = tc->layer_input[layer][idx]
                                  + ws->layer_attn_out[idx];
            }
        }

        /* Сохраняем mid_hidden */
        memcpy(tc->mid_hidden[layer], ws->hidden,
               seq_len * E * sizeof(float));

        /* === FFN sub-block === */

        /* Pre-LN: FFN LayerNorm */
        for (size_t t = 0; t < seq_len; t++) {
            layer_norm(&ws->hidden[t * E],
                       &tc->ffn_normed[layer][t * E],
                       block->ffn.ln_gamma, block->ffn.ln_beta, E);
        }

        /* Первый слой FFN: pre_gelu = normed @ W1 + b1 */
        for (size_t t = 0; t < seq_len; t++) {
            for (int f = 0; f < cfg->ff_dim; f++) {
                float sum = block->ffn.b1[f];
                for (int e = 0; e < E; e++) {
                    sum += tc->ffn_normed[layer][IDX2(t, e, E)] *
                           block->ffn.w1[IDX2(e, f, cfg->ff_dim)];
                }
                tc->ffn_pre_gelu[layer][IDX2(t, f, cfg->ff_dim)] = sum;
                tc->ffn_post_gelu[layer][IDX2(t, f, cfg->ff_dim)] = gelu(sum);
            }
        }

        /* Второй слой FFN: ff_out = post_gelu @ W2 + b2 */
        for (size_t t = 0; t < seq_len; t++) {
            for (int e = 0; e < E; e++) {
                float sum = block->ffn.b2[e];
                for (int f = 0; f < cfg->ff_dim; f++) {
                    sum += tc->ffn_post_gelu[layer][IDX2(t, f, cfg->ff_dim)] *
                           block->ffn.w2[IDX2(f, e, E)];
                }
                ws->layer_ffn_out[IDX2(t, e, E)] = sum;
            }
        }

        /* Residual connection после FFN */
        for (size_t t = 0; t < seq_len; t++) {
            for (int d = 0; d < E; d++) {
                size_t idx = IDX2(t, d, E);
                ws->hidden[idx] = tc->mid_hidden[layer][idx]
                                  + ws->layer_ffn_out[idx];
            }
        }
    }

    /* --- Шаг 3: Финальный LayerNorm --- */
    memcpy(tc->final_ln_input, ws->hidden,
           seq_len * E * sizeof(float));

    for (size_t t = 0; t < seq_len; t++) {
        float tmp[1024];  /* embed_dim <= 1024 */
        layer_norm(&ws->hidden[t * E], tmp,
                   model->final_ln_gamma, model->final_ln_beta, E);
        memcpy(&ws->hidden[t * E], tmp, E * sizeof(float));
    }

    /* --- Шаг 4: LM Head → logits + softmax --- */
    size_t last = seq_len - 1;
    for (int vi = 0; vi < V; vi++) {
        float sum = 0.0f;
        for (int d = 0; d < E; d++) {
            sum += ws->hidden[IDX2(last, d, E)] *
                   model->lm_head[IDX2(d, vi, V)];
        }
        ws->logits[vi] = sum;
    }
    memcpy(ws->probs, ws->logits, V * sizeof(float));
    softmax(ws->probs, V);

    return 0;
}

/*
 * Полный backpropagation: обратный проход через ВСЕ слои трансформера.
 *
 * Обновляет:
 *   - LM head (линейная проекция → логиты)
 *   - Финальный LayerNorm (gamma, beta + входной сигнал)
 *   - Для каждого блока (обратный порядок):
 *     * FFN: W2, b2, GELU, W1, b1, LayerNorm
 *     * MHA: Wo, per-head Wq/Wk/Wv (через attention scores), LayerNorm
 *   - Token embeddings, Position embeddings
 */
float kat_train_step_full(KatModel *model, KatWorkspace *ws,
                          const uint8_t *tokens, size_t seq_len,
                          uint8_t target, float lr) {
    if (!model || !ws || !tokens || seq_len == 0) return 0.0f;

    const KatConfig *cfg = &model->cfg;
    int E = cfg->embed_dim;
    int V = cfg->vocab_size;
    int L = cfg->num_layers;
    int H = cfg->num_heads;
    int D = cfg->head_dim;
    int F = cfg->ff_dim;
    int S = cfg->max_seq;

    if (seq_len > (size_t)S) seq_len = (size_t)S;

    /* Аллокация кэша */
    KatTrainCache *tc = train_cache_create(cfg, seq_len);
    if (!tc) return 0.0f;

    /* ===== Forward pass с кэшированием ===== */
    training_forward(model, ws, tc, tokens, seq_len);

    /* Cross-entropy loss */
    float p_target = ws->probs[target];
    if (p_target < 1e-10f) p_target = 1e-10f;
    float loss = -logf(p_target);

    /* ===== Backward pass ===== */

    size_t last = seq_len - 1;
    float grad_clip = 1.0f;  /* Максимальная норма градиента */

    /* --- Градиент по логитам: dL/dlogit[v] = probs[v] - one_hot(target) --- */
    float *dlogits = alloc_floats(V);
    for (int v = 0; v < V; v++) {
        dlogits[v] = ws->probs[v];
    }
    dlogits[target] -= 1.0f;

    /* --- Обновление LM head и градиент по hidden[last] --- */
    float *d_hidden = alloc_floats(seq_len * E);
    memset(d_hidden, 0, seq_len * E * sizeof(float));

    /* dL/d_h_last[d] = sum_v(lm_head[d,v] * dlogits[v]) */
    for (int d = 0; d < E; d++) {
        float grad_d = 0.0f;
        for (int v = 0; v < V; v++) {
            grad_d += model->lm_head[IDX2(d, v, V)] * dlogits[v];
        }
        d_hidden[IDX2(last, d, E)] = grad_d;
    }

    /* Обновление LM head: W[d][v] -= lr * h[d] * dlogits[v] */
    for (int d = 0; d < E; d++) {
        float hd = ws->hidden[IDX2(last, d, E)];
        for (int v = 0; v < V; v++) {
            model->lm_head[IDX2(d, v, V)] -= lr * hd * dlogits[v];
        }
    }

    free(dlogits);

    /* --- Backward через финальный LayerNorm --- */
    /* d_hidden сейчас содержит градиент по выходу final LN (только last) */
    /* Нужен градиент по входу final LN → tc->final_ln_input */
    float *d_pre_final_ln = alloc_floats(seq_len * E);
    memset(d_pre_final_ln, 0, seq_len * E * sizeof(float));

    float *d_final_ln_gamma = alloc_floats(E);
    float *d_final_ln_beta  = alloc_floats(E);
    memset(d_final_ln_gamma, 0, E * sizeof(float));
    memset(d_final_ln_beta,  0, E * sizeof(float));

    /* LayerNorm backward для позиции last */
    layer_norm_backward(
        &d_hidden[last * E],
        &tc->final_ln_input[last * E],
        model->final_ln_gamma,
        &d_pre_final_ln[last * E],
        d_final_ln_gamma,
        d_final_ln_beta,
        E
    );

    /* Обновление final LN параметров */
    clip_grad(d_final_ln_gamma, E, grad_clip);
    clip_grad(d_final_ln_beta, E, grad_clip);
    for (int d = 0; d < E; d++) {
        model->final_ln_gamma[d] -= lr * d_final_ln_gamma[d];
        model->final_ln_beta[d]  -= lr * d_final_ln_beta[d];
    }
    free(d_final_ln_gamma);
    free(d_final_ln_beta);
    free(d_hidden);

    /* Теперь d_pre_final_ln содержит dL/d_hidden[last] перед final LN */
    /* Это то же, что выход последнего блока */

    /* --- Backward через трансформерные блоки (обратный порядок) --- */
    float *d_layer = d_pre_final_ln; /* [seq_len * E] — текущий градиент */

    /* Вспомогательные буферы */
    float *d_attn_proj = alloc_floats(seq_len * E);  /* после Wo */
    float *d_attn_concat = alloc_floats(seq_len * E); /* до Wo */
    float *d_normed = alloc_floats(seq_len * E);       /* после LN */
    float *d_ff_out = alloc_floats(seq_len * E);       /* FFN output */
    float *d_ff_hidden = alloc_floats(seq_len * F);    /* FFN pre/post gelu */
    float *d_head_q = alloc_floats(seq_len * D);
    float *d_head_k = alloc_floats(seq_len * D);
    float *d_head_v = alloc_floats(seq_len * D);
    float *d_scores = alloc_floats(seq_len * S);
    float *d_raw    = alloc_floats(seq_len * S);
    float *d_layer_input = alloc_floats(seq_len * E);

    for (int layer = L - 1; layer >= 0; layer--) {
        KatTransformerBlock *block = &model->layers[layer];

        /* =============================================
         * Backward FFN sub-block
         * ============================================= */

        /* d_layer = dL/d_output_of_block[layer]
         *
         * Forward: output = mid_hidden + ff_out
         *   → d_mid_hidden += d_layer
         *   → d_ff_out      = d_layer (residual connection)
         */
        memcpy(d_ff_out, d_layer, seq_len * E * sizeof(float));

        /* --- Backward через W2: ff_out[t][e] = sum_f(gelu_out[t][f] * W2[f][e]) + b2[e] --- */

        /* dW2[f][e] += sum_t(gelu_out[t][f] * d_ff_out[t][e]) */
        /* db2[e]    += sum_t(d_ff_out[t][e]) */
        /* d_gelu_out[t][f] = sum_e(d_ff_out[t][e] * W2[f][e]) */
        memset(d_ff_hidden, 0, seq_len * F * sizeof(float));

        for (size_t t = 0; t < seq_len; t++) {
            for (int e = 0; e < E; e++) {
                float dfe = d_ff_out[IDX2(t, e, E)];
                block->ffn.b2[e] -= lr * dfe;
                for (int f = 0; f < F; f++) {
                    float pg = tc->ffn_post_gelu[layer][IDX2(t, f, F)];
                    block->ffn.w2[IDX2(f, e, E)] -= lr * pg * dfe;
                    d_ff_hidden[IDX2(t, f, F)] +=
                        dfe * block->ffn.w2[IDX2(f, e, E)];
                }
            }
        }

        /* --- Backward через GELU --- */
        /* d_pre_gelu[t][f] = d_gelu_out[t][f] * gelu'(pre_gelu[t][f]) */
        for (size_t t = 0; t < seq_len; t++) {
            for (int f = 0; f < F; f++) {
                size_t idx = IDX2(t, f, F);
                d_ff_hidden[idx] *= gelu_grad(
                    tc->ffn_pre_gelu[layer][idx]);
            }
        }

        /* --- Backward через W1: pre_gelu[t][f] = sum_e(normed[t][e]*W1[e][f]) + b1[f] --- */
        /* dW1[e][f] += sum_t(normed[t][e] * d_pre_gelu[t][f]) */
        /* db1[f]    += sum_t(d_pre_gelu[t][f]) */
        /* d_normed[t][e] = sum_f(d_pre_gelu[t][f] * W1[e][f]) */
        memset(d_normed, 0, seq_len * E * sizeof(float));

        for (size_t t = 0; t < seq_len; t++) {
            for (int f = 0; f < F; f++) {
                float dpg = d_ff_hidden[IDX2(t, f, F)];
                block->ffn.b1[f] -= lr * dpg;
                for (int e = 0; e < E; e++) {
                    float n = tc->ffn_normed[layer][IDX2(t, e, E)];
                    block->ffn.w1[IDX2(e, f, F)] -= lr * n * dpg;
                    d_normed[IDX2(t, e, E)] +=
                        dpg * block->ffn.w1[IDX2(e, f, F)];
                }
            }
        }

        /* --- Backward через FFN LayerNorm --- */
        /* d_normed → d_mid_hidden (через LN backward) */
        float *d_ffn_ln_gamma = alloc_floats(E);
        float *d_ffn_ln_beta  = alloc_floats(E);
        memset(d_ffn_ln_gamma, 0, E * sizeof(float));
        memset(d_ffn_ln_beta,  0, E * sizeof(float));

        /* Буфер для dx через LN */
        float *d_mid = alloc_floats(seq_len * E);
        memset(d_mid, 0, seq_len * E * sizeof(float));

        for (size_t t = 0; t < seq_len; t++) {
            layer_norm_backward(
                &d_normed[IDX2(t, 0, E)],
                &tc->mid_hidden[layer][t * E],
                block->ffn.ln_gamma,
                &d_mid[t * E],
                d_ffn_ln_gamma,
                d_ffn_ln_beta,
                E
            );
        }

        /* Обновление FFN LN параметров */
        clip_grad(d_ffn_ln_gamma, E, grad_clip);
        clip_grad(d_ffn_ln_beta, E, grad_clip);
        for (int d = 0; d < E; d++) {
            block->ffn.ln_gamma[d] -= lr * d_ffn_ln_gamma[d];
            block->ffn.ln_beta[d]  -= lr * d_ffn_ln_beta[d];
        }
        free(d_ffn_ln_gamma);
        free(d_ffn_ln_beta);

        /* d_mid_hidden = d_layer (residual) + d_mid (через LN) */
        for (size_t i = 0; i < seq_len * (size_t)E; i++) {
            d_mid[i] += d_layer[i];
        }

        /* =============================================
         * Backward Attention sub-block
         * ============================================= */

        /* Forward: hidden = layer_input + attn_proj
         *   → d_layer_input += d_mid
         *   → d_attn_proj    = d_mid
         */
        memcpy(d_attn_proj, d_mid, seq_len * E * sizeof(float));

        /* --- Backward через Wo: attn_proj[t][i] = sum_j(concat[t][j]*Wo[j][i]) --- */
        /* dWo[j][i] += sum_t(concat[t][j] * d_proj[t][i]) */
        /* d_concat[t][j] = sum_i(d_proj[t][i] * Wo[j][i]) */
        memset(d_attn_concat, 0, seq_len * E * sizeof(float));

        for (size_t t = 0; t < seq_len; t++) {
            for (int i = 0; i < E; i++) {
                float dp = d_attn_proj[IDX2(t, i, E)];
                for (int j = 0; j < E; j++) {
                    float c = tc->attn_concat[layer][IDX2(t, j, E)];
                    block->attn.wo[IDX2(j, i, E)] -= lr * c * dp;
                    d_attn_concat[IDX2(t, j, E)] +=
                        dp * block->attn.wo[IDX2(j, i, E)];
                }
            }
        }

        /* --- Backward через each head --- */
        /* Суммируем вклады всех голов в d_normed */
        memset(d_normed, 0, seq_len * E * sizeof(float));

        for (int h = 0; h < H; h++) {
            int lh = layer * H + h;
            KatAttentionHead *head = &block->attn.heads[h];
            size_t offset = (size_t)h * D;

            float *qbuf = tc->head_q[lh];
            float *kbuf = tc->head_k[lh];
            float *vbuf = tc->head_v[lh];
            float *sbuf = tc->head_scores[lh];  /* softmax output */

            /* Извлекаем d_head_out из d_attn_concat[:, offset:offset+D] */
            /* head_out[i][d] = sum_j(scores[i][j] * V[j][d]) */

            /* d_scores[i][j] = sum_d(d_head_out[i][d] * V[j][d]) */
            /* d_V[j][d]      += sum_i(scores[i][j] * d_head_out[i][d]) */
            memset(d_head_v, 0, seq_len * D * sizeof(float));
            memset(d_scores, 0, seq_len * S * sizeof(float));

            for (size_t i = 0; i < seq_len; i++) {
                for (int d = 0; d < D; d++) {
                    float dho = d_attn_concat[IDX2(i, offset + d, E)];
                    for (size_t j = 0; j <= i; j++) {
                        d_scores[IDX2(i, j, S)] +=
                            dho * vbuf[IDX2(j, d, D)];
                        d_head_v[IDX2(j, d, D)] +=
                            sbuf[IDX2(i, j, S)] * dho;
                    }
                }
            }

            /* --- Backward через softmax --- */
            /* d_raw_scores = softmax_backward(scores, d_scores) * scale */
            float scale = 1.0f / sqrtf((float)D);
            for (size_t i = 0; i < seq_len; i++) {
                softmax_backward(
                    &sbuf[i * S],
                    &d_scores[i * S],
                    &d_raw[i * S],
                    seq_len
                );
                /* Домножаем на scale (scores = raw * scale → d_raw *= scale) */
                for (size_t j = 0; j < seq_len; j++) {
                    d_raw[IDX2(i, j, S)] *= scale;
                }
            }

            /* --- Backward Q/K score: raw[i][j] = Q[i]·K[j] --- */
            /* d_Q[i][d] = sum_j(d_raw[i][j] * K[j][d]) */
            /* d_K[j][d] = sum_i(d_raw[i][j] * Q[i][d]) */
            memset(d_head_q, 0, seq_len * D * sizeof(float));
            memset(d_head_k, 0, seq_len * D * sizeof(float));

            for (size_t i = 0; i < seq_len; i++) {
                for (size_t j = 0; j <= i; j++) {
                    float dr = d_raw[IDX2(i, j, S)];
                    for (int d = 0; d < D; d++) {
                        d_head_q[IDX2(i, d, D)] +=
                            dr * kbuf[IDX2(j, d, D)];
                        d_head_k[IDX2(j, d, D)] +=
                            dr * qbuf[IDX2(i, d, D)];
                    }
                }
            }

            /* --- Backward Q/K/V проекции --- */
            /* Q = normed @ Wq → dWq += normed^T @ dQ, d_normed += dQ @ Wq^T */
            /* K = normed @ Wk → аналогично */
            /* V = normed @ Wv → аналогично */

            for (size_t t = 0; t < seq_len; t++) {
                for (int d = 0; d < D; d++) {
                    float dq = d_head_q[IDX2(t, d, D)];
                    float dk = d_head_k[IDX2(t, d, D)];
                    float dv = d_head_v[IDX2(t, d, D)];

                    for (int e = 0; e < E; e++) {
                        float n = tc->attn_normed[layer][IDX2(t, e, E)];

                        /* Обновление Wq, Wk, Wv */
                        head->wq[IDX2(e, d, D)] -= lr * n * dq;
                        head->wk[IDX2(e, d, D)] -= lr * n * dk;
                        head->wv[IDX2(e, d, D)] -= lr * n * dv;

                        /* Градиент по normed */
                        d_normed[IDX2(t, e, E)] +=
                            dq * head->wq[IDX2(e, d, D)] +
                            dk * head->wk[IDX2(e, d, D)] +
                            dv * head->wv[IDX2(e, d, D)];
                    }
                }
            }
        } /* end heads loop */

        /* --- Backward через Attention LayerNorm --- */
        float *d_attn_ln_gamma = alloc_floats(E);
        float *d_attn_ln_beta  = alloc_floats(E);
        memset(d_attn_ln_gamma, 0, E * sizeof(float));
        memset(d_attn_ln_beta,  0, E * sizeof(float));

        memset(d_layer_input, 0, seq_len * E * sizeof(float));

        for (size_t t = 0; t < seq_len; t++) {
            layer_norm_backward(
                &d_normed[IDX2(t, 0, E)],
                &tc->layer_input[layer][t * E],
                block->attn.ln_gamma,
                &d_layer_input[t * E],
                d_attn_ln_gamma,
                d_attn_ln_beta,
                E
            );
        }

        /* Обновление attention LN */
        clip_grad(d_attn_ln_gamma, E, grad_clip);
        clip_grad(d_attn_ln_beta, E, grad_clip);
        for (int d = 0; d < E; d++) {
            block->attn.ln_gamma[d] -= lr * d_attn_ln_gamma[d];
            block->attn.ln_beta[d]  -= lr * d_attn_ln_beta[d];
        }
        free(d_attn_ln_gamma);
        free(d_attn_ln_beta);

        /* d_layer_input += d_mid (residual в attention sub-block) */
        for (size_t i = 0; i < seq_len * (size_t)E; i++) {
            d_layer_input[i] += d_mid[i];
        }

        /* Передаём градиент в предыдущий слой */
        memcpy(d_layer, d_layer_input, seq_len * E * sizeof(float));

        free(d_mid);
    } /* end layers loop */

    /* --- Backward через эмбеддинги --- */
    /* d_layer теперь содержит dL/d_embed_output */

    /* Обновление token/position embeddings */
    float emb_lr = lr * 0.1f;  /* Уменьшенный LR для эмбеддингов */
    for (size_t t = 0; t < seq_len; t++) {
        int tok = tokens[t];
        if (tok >= V) tok = 0;
        for (int d = 0; d < E; d++) {
            float grad = d_layer[IDX2(t, d, E)];
            model->embed.token_embed[IDX2(tok, d, E)] -= emb_lr * grad;
            model->embed.pos_embed[IDX2(t, d, E)]     -= emb_lr * grad;
        }
    }

    /* Освобождение */
    free(d_pre_final_ln);  /* = d_layer */
    free(d_attn_proj);
    free(d_attn_concat);
    free(d_normed);
    free(d_ff_out);
    free(d_ff_hidden);
    free(d_head_q);
    free(d_head_k);
    free(d_head_v);
    free(d_scores);
    free(d_raw);
    free(d_layer_input);
    train_cache_destroy(tc);

    return loss;
}

/* ============================================================================
 * Утилиты
 * ============================================================================ */

float kat_cosine_similarity(const float *a, const float *b, size_t dim) {
    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
    for (size_t i = 0; i < dim; i++) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    float denom = sqrtf(norm_a) * sqrtf(norm_b);
    return denom > 1e-10f ? dot / denom : 0.0f;
}

/** Обратная совместимость: подсчёт параметров small конфигурации */
size_t kat_count_params(void) {
    KatConfig cfg = kat_config_small();
    return kat_config_count_params(&cfg);
}

/* ============================================================================
 * Сериализация (поддержка динамических размеров)
 * ============================================================================ */

/* Магическое число для идентификации формата */
#define KAT_SERIAL_MAGIC 0x4B415431u  /* "KAT1" */

/*
 * Формат:
 *   [4 bytes] magic "KAT1"
 *   [sizeof(KatConfig)] config
 *   [8 bytes] seed
 *   [8 bytes] param_count
 *   [V*E floats] token_embed
 *   [S*E floats] pos_embed
 *   Для каждого слоя l = 0..L-1:
 *     Для каждой головы h = 0..H-1: [E*D] wq, [E*D] wk, [E*D] wv
 *     [E*E] wo, [E] ln_gamma, [E] ln_beta
 *     [E*F] w1, [F] b1, [F*E] w2, [E] b2, [E] ln_gamma, [E] ln_beta
 *   [E] final_ln_gamma, [E] final_ln_beta
 *   [E*V] lm_head
 */
static size_t kat_serial_size(const KatConfig *cfg) {
    size_t sz = 4 + sizeof(KatConfig) + 8 + 8;  /* header */

    int V = cfg->vocab_size, E = cfg->embed_dim;
    int H = cfg->num_heads,  D = cfg->head_dim;
    int F = cfg->ff_dim,     L = cfg->num_layers, S = cfg->max_seq;

    /* Эмбеддинги */
    sz += ((size_t)V * E + (size_t)S * E) * sizeof(float);

    /* Слои */
    for (int l = 0; l < L; l++) {
        sz += (size_t)H * 3 * E * D * sizeof(float);       /* heads Q/K/V    */
        sz += ((size_t)E * E + E + E) * sizeof(float);     /* wo, ln_g, ln_b */
        sz += ((size_t)E * F + F + (size_t)F * E + E + E + E)
              * sizeof(float);                              /* FFN            */
    }

    /* Финальный LN + LM head */
    sz += (E + E) * sizeof(float);
    sz += (size_t)E * V * sizeof(float);

    return sz;
}

size_t kat_serialize(const KatModel *model, uint8_t *buf, size_t buf_size) {
    if (!model) return 0;

    const KatConfig *cfg = &model->cfg;
    size_t needed = kat_serial_size(cfg);
    if (!buf || buf_size < needed) return needed;

    uint8_t *p = buf;

    /* Magic */
    uint32_t magic = KAT_SERIAL_MAGIC;
    memcpy(p, &magic, 4); p += 4;

    /* Config */
    memcpy(p, cfg, sizeof(KatConfig)); p += sizeof(KatConfig);

    /* Seed + param_count */
    memcpy(p, &model->seed, 8); p += 8;
    memcpy(p, &model->param_count, 8); p += 8;

    int V = cfg->vocab_size, E = cfg->embed_dim;
    int H = cfg->num_heads,  D = cfg->head_dim;
    int F = cfg->ff_dim,     L = cfg->num_layers, S = cfg->max_seq;

    /* Макрос для записи float-массива */
    #define WRITE_F(ptr, count) do { \
        size_t _n = (size_t)(count) * sizeof(float); \
        memcpy(p, (ptr), _n); p += _n; \
    } while(0)

    WRITE_F(model->embed.token_embed, (size_t)V * E);
    WRITE_F(model->embed.pos_embed,   (size_t)S * E);

    for (int l = 0; l < L; l++) {
        const KatTransformerBlock *block = &model->layers[l];
        for (int h = 0; h < H; h++) {
            WRITE_F(block->attn.heads[h].wq, (size_t)E * D);
            WRITE_F(block->attn.heads[h].wk, (size_t)E * D);
            WRITE_F(block->attn.heads[h].wv, (size_t)E * D);
        }
        WRITE_F(block->attn.wo,       (size_t)E * E);
        WRITE_F(block->attn.ln_gamma, E);
        WRITE_F(block->attn.ln_beta,  E);

        WRITE_F(block->ffn.w1,       (size_t)E * F);
        WRITE_F(block->ffn.b1,       F);
        WRITE_F(block->ffn.w2,       (size_t)F * E);
        WRITE_F(block->ffn.b2,       E);
        WRITE_F(block->ffn.ln_gamma, E);
        WRITE_F(block->ffn.ln_beta,  E);
    }

    WRITE_F(model->final_ln_gamma, E);
    WRITE_F(model->final_ln_beta,  E);
    WRITE_F(model->lm_head, (size_t)E * V);

    #undef WRITE_F

    return needed;
}

int kat_deserialize(KatModel *model, const uint8_t *buf, size_t buf_size) {
    if (!model || !buf || buf_size < 4 + sizeof(KatConfig) + 16) return -1;

    const uint8_t *p = buf;

    /* Magic */
    uint32_t magic;
    memcpy(&magic, p, 4); p += 4;
    if (magic != KAT_SERIAL_MAGIC) return -1;

    /* Config */
    KatConfig cfg;
    memcpy(&cfg, p, sizeof(KatConfig)); p += sizeof(KatConfig);

    size_t needed = kat_serial_size(&cfg);
    if (buf_size < needed) return -1;

    /* Если конфигурация не совпадает — пересоздаём внутренности */
    if (model->cfg.vocab_size != cfg.vocab_size ||
        model->cfg.embed_dim  != cfg.embed_dim  ||
        model->cfg.num_heads  != cfg.num_heads  ||
        model->cfg.head_dim   != cfg.head_dim   ||
        model->cfg.ff_dim     != cfg.ff_dim     ||
        model->cfg.num_layers != cfg.num_layers ||
        model->cfg.max_seq    != cfg.max_seq) {
        /* Освобождаем старые данные */
        free(model->embed.token_embed);
        free(model->embed.pos_embed);
        if (model->layers) {
            for (int l = 0; l < model->cfg.num_layers; l++) {
                destroy_layer(&model->layers[l], model->cfg.num_heads);
            }
            free(model->layers);
        }
        free(model->final_ln_gamma);
        free(model->final_ln_beta);
        free(model->lm_head);

        /* Создаём новую модель с нужной конфигурацией */
        KatModel *tmp = kat_model_create_ex(&cfg, 1);
        if (!tmp) return -1;

        /* Копируем структуру (указатели) */
        *model = *tmp;
        /* Освобождаем только оболочку, не данные */
        free(tmp);
    }

    /* seed + param_count */
    memcpy(&model->seed,        p, 8); p += 8;
    memcpy(&model->param_count, p, 8); p += 8;

    int V = cfg.vocab_size, E = cfg.embed_dim;
    int H = cfg.num_heads,  D = cfg.head_dim;
    int F = cfg.ff_dim,     L = cfg.num_layers, S = cfg.max_seq;

    /* Макрос для чтения float-массива */
    #define READ_F(ptr, count) do { \
        size_t _n = (size_t)(count) * sizeof(float); \
        memcpy((ptr), p, _n); p += _n; \
    } while(0)

    READ_F(model->embed.token_embed, (size_t)V * E);
    READ_F(model->embed.pos_embed,   (size_t)S * E);

    for (int l = 0; l < L; l++) {
        KatTransformerBlock *block = &model->layers[l];
        for (int h = 0; h < H; h++) {
            READ_F(block->attn.heads[h].wq, (size_t)E * D);
            READ_F(block->attn.heads[h].wk, (size_t)E * D);
            READ_F(block->attn.heads[h].wv, (size_t)E * D);
        }
        READ_F(block->attn.wo,       (size_t)E * E);
        READ_F(block->attn.ln_gamma, E);
        READ_F(block->attn.ln_beta,  E);

        READ_F(block->ffn.w1,       (size_t)E * F);
        READ_F(block->ffn.b1,       F);
        READ_F(block->ffn.w2,       (size_t)F * E);
        READ_F(block->ffn.b2,       E);
        READ_F(block->ffn.ln_gamma, E);
        READ_F(block->ffn.ln_beta,  E);
    }

    READ_F(model->final_ln_gamma, E);
    READ_F(model->final_ln_beta,  E);
    READ_F(model->lm_head, (size_t)E * V);

    #undef READ_F

    model->cfg = cfg;
    return 0;
}
