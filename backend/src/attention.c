/*
 * attention.c
 *
 * Реализация модуля Self-Attention для Kolibri AGI
 *
 * Архитектура: Pre-LN Transformer
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

/* ========== ВНУТРЕННИЙ PRNG (xorshift64) ========== */

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

/* ========== LAYERNORM ========== */

/*
 * LayerNorm: нормализация вектора по среднему и дисперсии
 * out[i] = gamma[i] * (x[i] - mean) / sqrt(var + eps) + beta[i]
 */
static void layer_norm(
    const float *x, float *out,
    const float *gamma, const float *beta,
    size_t dim
) {
    /* Среднее */
    float mean = 0.0f;
    for (size_t i = 0; i < dim; i++) mean += x[i];
    mean /= (float)dim;

    /* Дисперсия */
    float var = 0.0f;
    for (size_t i = 0; i < dim; i++) {
        float d = x[i] - mean;
        var += d * d;
    }
    var /= (float)dim;

    /* Нормализация */
    float inv_std = 1.0f / sqrtf(var + KAT_EPSILON);
    for (size_t i = 0; i < dim; i++) {
        out[i] = gamma[i] * (x[i] - mean) * inv_std + beta[i];
    }
}

/* ========== АКТИВАЦИИ ========== */

/*
 * GELU: Gaussian Error Linear Unit
 * Быстрое приближение: x * 0.5 * (1 + tanh(sqrt(2/pi) * (x + 0.044715 * x^3)))
 */
static float gelu(float x) {
    float c = 0.7978845608f;  /* sqrt(2/pi) */
    float inner = c * (x + 0.044715f * x * x * x);
    return 0.5f * x * (1.0f + tanhf(inner));
}

/* ========== SOFTMAX ========== */

static void softmax(float *x, size_t n) {
    /* Стабильный softmax с вычитанием максимума */
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

/* ========== ИНИЦИАЛИЗАЦИЯ XAVIER ========== */

/*
 * Xavier Uniform: U(-sqrt(6/(fan_in+fan_out)), sqrt(6/(fan_in+fan_out)))
 * Обеспечивает стабильный градиент при прямом проходе
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

/* ========== ЖИЗНЕННЫЙ ЦИКЛ ========== */

KatModel* kat_model_create(uint64_t seed) {
    KatModel *model = calloc(1, sizeof(KatModel));
    if (!model) return NULL;

    model->seed = seed ? seed : 42;
    uint64_t *s = &model->seed;

    /* --- Инициализация эмбеддингов --- */

    /* Токеновые эмбеддинги: Xavier */
    xavier_init(&model->embed.token_embed[0][0],
                KAT_VOCAB_SIZE, KAT_EMBED_DIM, s);

    /* Позиционные эмбеддинги: sinusoidal (фиксированные) */
    for (size_t pos = 0; pos < KAT_MAX_SEQ; pos++) {
        for (size_t i = 0; i < KAT_EMBED_DIM; i++) {
            float angle = (float)pos / powf(10000.0f,
                          2.0f * (float)(i / 2) / (float)KAT_EMBED_DIM);
            if (i % 2 == 0) {
                model->embed.pos_embed[pos][i] = sinf(angle);
            } else {
                model->embed.pos_embed[pos][i] = cosf(angle);
            }
        }
    }

    /* --- Инициализация трансформерных блоков --- */
    for (size_t layer = 0; layer < KAT_NUM_LAYERS; layer++) {
        KatTransformerBlock *block = &model->layers[layer];

        /* Multi-Head Attention */
        for (size_t h = 0; h < KAT_NUM_HEADS; h++) {
            KatAttentionHead *head = &block->attn.heads[h];
            xavier_init(&head->wq[0][0], KAT_EMBED_DIM, KAT_HEAD_DIM, s);
            xavier_init(&head->wk[0][0], KAT_EMBED_DIM, KAT_HEAD_DIM, s);
            xavier_init(&head->wv[0][0], KAT_EMBED_DIM, KAT_HEAD_DIM, s);
        }
        xavier_init(&block->attn.wo[0][0], KAT_EMBED_DIM, KAT_EMBED_DIM, s);

        /* LayerNorm для attention */
        for (size_t i = 0; i < KAT_EMBED_DIM; i++) {
            block->attn.ln_gamma[i] = 1.0f;
            block->attn.ln_beta[i] = 0.0f;
        }

        /* Feed-Forward Network */
        xavier_init(&block->ffn.w1[0][0], KAT_EMBED_DIM, KAT_FF_DIM, s);
        memset(block->ffn.b1, 0, sizeof(block->ffn.b1));
        xavier_init(&block->ffn.w2[0][0], KAT_FF_DIM, KAT_EMBED_DIM, s);
        memset(block->ffn.b2, 0, sizeof(block->ffn.b2));

        /* LayerNorm для FFN */
        for (size_t i = 0; i < KAT_EMBED_DIM; i++) {
            block->ffn.ln_gamma[i] = 1.0f;
            block->ffn.ln_beta[i] = 0.0f;
        }
    }

    /* --- Финальный LayerNorm --- */
    for (size_t i = 0; i < KAT_EMBED_DIM; i++) {
        model->final_ln_gamma[i] = 1.0f;
        model->final_ln_beta[i] = 0.0f;
    }

    /* --- Language Model Head --- */
    xavier_init(&model->lm_head[0][0], KAT_EMBED_DIM, KAT_VOCAB_SIZE, s);

    /* Подсчёт параметров */
    model->param_count = kat_count_params();

    return model;
}

void kat_model_destroy(KatModel *model) {
    free(model);
}

KatWorkspace* kat_workspace_create(void) {
    KatWorkspace *ws = calloc(1, sizeof(KatWorkspace));
    return ws;
}

void kat_workspace_destroy(KatWorkspace *ws) {
    free(ws);
}

/* ========== FORWARD PASS ========== */

/*
 * Одна голова Self-Attention (каузальная):
 *   Q = X @ Wq,  K = X @ Wk,  V = X @ Wv
 *   scores = Q @ K^T / sqrt(head_dim) + mask
 *   attn = softmax(scores) @ V
 */
static void attention_head_forward(
    const KatAttentionHead *head,
    const float hidden[][KAT_EMBED_DIM],
    size_t seq_len,
    float q[][KAT_HEAD_DIM],
    float k[][KAT_HEAD_DIM],
    float v[][KAT_HEAD_DIM],
    float scores[][KAT_MAX_SEQ],
    float out[][KAT_HEAD_DIM]
) {
    /* Проекции: Q, K, V */
    for (size_t t = 0; t < seq_len; t++) {
        for (size_t d = 0; d < KAT_HEAD_DIM; d++) {
            float sq = 0.0f, sk = 0.0f, sv = 0.0f;
            for (size_t j = 0; j < KAT_EMBED_DIM; j++) {
                sq += hidden[t][j] * head->wq[j][d];
                sk += hidden[t][j] * head->wk[j][d];
                sv += hidden[t][j] * head->wv[j][d];
            }
            q[t][d] = sq;
            k[t][d] = sk;
            v[t][d] = sv;
        }
    }

    /* Масштаб: 1/sqrt(head_dim) */
    float scale = 1.0f / sqrtf((float)KAT_HEAD_DIM);

    /* Матрица внимания: scores[i][j] = dot(Q[i], K[j]) * scale */
    for (size_t i = 0; i < seq_len; i++) {
        for (size_t j = 0; j < seq_len; j++) {
            if (j > i) {
                /* Каузальная маска: будущие позиции = -inf */
                scores[i][j] = -1e9f;
            } else {
                float dot = 0.0f;
                for (size_t d = 0; d < KAT_HEAD_DIM; d++) {
                    dot += q[i][d] * k[j][d];
                }
                scores[i][j] = dot * scale;
            }
        }
        /* Softmax по строке */
        softmax(&scores[i][0], seq_len);
    }

    /* Взвешенная сумма values: out[i] = sum_j(scores[i][j] * V[j]) */
    for (size_t i = 0; i < seq_len; i++) {
        for (size_t d = 0; d < KAT_HEAD_DIM; d++) {
            float sum = 0.0f;
            for (size_t j = 0; j <= i; j++) {
                sum += scores[i][j] * v[j][d];
            }
            out[i][d] = sum;
        }
    }
}

/*
 * Multi-Head Attention: конкатенация голов + выходная проекция
 */
static void multi_head_attention_forward(
    const KatMultiHeadAttention *mha,
    const float input[][KAT_EMBED_DIM],
    size_t seq_len,
    KatWorkspace *ws,
    float output[][KAT_EMBED_DIM]
) {
    /* LayerNorm перед attention (Pre-LN) */
    float normed[KAT_MAX_SEQ][KAT_EMBED_DIM];
    for (size_t t = 0; t < seq_len; t++) {
        layer_norm(input[t], normed[t], mha->ln_gamma, mha->ln_beta,
                   KAT_EMBED_DIM);
    }

    /* Обнуляем выход */
    for (size_t t = 0; t < seq_len; t++) {
        memset(output[t], 0, KAT_EMBED_DIM * sizeof(float));
    }

    /* Каждая голова обрабатывает свой подпространство */
    float head_out[KAT_MAX_SEQ][KAT_HEAD_DIM];

    for (size_t h = 0; h < KAT_NUM_HEADS; h++) {
        attention_head_forward(
            &mha->heads[h], normed, seq_len,
            ws->q, ws->k, ws->v, ws->attn_scores,
            head_out
        );

        /* Конкатенация: записать head_out в соответствующие позиции */
        size_t offset = h * KAT_HEAD_DIM;
        for (size_t t = 0; t < seq_len; t++) {
            for (size_t d = 0; d < KAT_HEAD_DIM; d++) {
                ws->attn_out[t][offset + d] = head_out[t][d];
            }
        }
    }

    /* Выходная проекция: Wo */
    for (size_t t = 0; t < seq_len; t++) {
        for (size_t i = 0; i < KAT_EMBED_DIM; i++) {
            float sum = 0.0f;
            for (size_t j = 0; j < KAT_EMBED_DIM; j++) {
                sum += ws->attn_out[t][j] * mha->wo[j][i];
            }
            output[t][i] = sum;
        }
    }
}

/*
 * Feed-Forward Network: GELU(x @ W1 + b1) @ W2 + b2
 */
static void feed_forward_forward(
    const KatFeedForward *ffn,
    const float input[][KAT_EMBED_DIM],
    size_t seq_len,
    KatWorkspace *ws,
    float output[][KAT_EMBED_DIM]
) {
    /* LayerNorm перед FFN (Pre-LN) */
    float normed[KAT_MAX_SEQ][KAT_EMBED_DIM];
    for (size_t t = 0; t < seq_len; t++) {
        layer_norm(input[t], normed[t], ffn->ln_gamma, ffn->ln_beta,
                   KAT_EMBED_DIM);
    }

    /* Первый слой: hidden = GELU(x @ W1 + b1) */
    for (size_t t = 0; t < seq_len; t++) {
        for (size_t j = 0; j < KAT_FF_DIM; j++) {
            float sum = ffn->b1[j];
            for (size_t i = 0; i < KAT_EMBED_DIM; i++) {
                sum += normed[t][i] * ffn->w1[i][j];
            }
            ws->ff_hidden[t][j] = gelu(sum);
        }
    }

    /* Второй слой: output = hidden @ W2 + b2 */
    for (size_t t = 0; t < seq_len; t++) {
        for (size_t i = 0; i < KAT_EMBED_DIM; i++) {
            float sum = ffn->b2[i];
            for (size_t j = 0; j < KAT_FF_DIM; j++) {
                sum += ws->ff_hidden[t][j] * ffn->w2[j][i];
            }
            output[t][i] = sum;
        }
    }
}

int kat_forward(const KatModel *model, KatWorkspace *ws,
                const uint8_t *tokens, size_t seq_len) {
    if (!model || !ws || !tokens || seq_len == 0) return -1;
    if (seq_len > KAT_MAX_SEQ) seq_len = KAT_MAX_SEQ;

    ws->seq_len = seq_len;

    /* --- Шаг 1: Эмбеддинги (токен + позиция) --- */
    for (size_t t = 0; t < seq_len; t++) {
        uint8_t tok = tokens[t];
        for (size_t d = 0; d < KAT_EMBED_DIM; d++) {
            ws->hidden[t][d] = model->embed.token_embed[tok][d]
                              + model->embed.pos_embed[t][d];
        }
    }

    /* --- Шаг 2: Стек трансформерных блоков --- */
    for (size_t layer = 0; layer < KAT_NUM_LAYERS; layer++) {
        const KatTransformerBlock *block = &model->layers[layer];

        /* Сохраняем residual */
        memcpy(ws->residual, ws->hidden,
               seq_len * KAT_EMBED_DIM * sizeof(float));

        /* Multi-Head Attention */
        float attn_output[KAT_MAX_SEQ][KAT_EMBED_DIM];
        multi_head_attention_forward(
            &block->attn, ws->hidden, seq_len, ws, attn_output
        );

        /* Residual connection после attention */
        for (size_t t = 0; t < seq_len; t++) {
            for (size_t d = 0; d < KAT_EMBED_DIM; d++) {
                ws->hidden[t][d] = ws->residual[t][d] + attn_output[t][d];
            }
        }

        /* Сохраняем residual перед FFN */
        memcpy(ws->residual, ws->hidden,
               seq_len * KAT_EMBED_DIM * sizeof(float));

        /* Feed-Forward Network */
        float ffn_output[KAT_MAX_SEQ][KAT_EMBED_DIM];
        feed_forward_forward(
            &block->ffn, ws->hidden, seq_len, ws, ffn_output
        );

        /* Residual connection после FFN */
        for (size_t t = 0; t < seq_len; t++) {
            for (size_t d = 0; d < KAT_EMBED_DIM; d++) {
                ws->hidden[t][d] = ws->residual[t][d] + ffn_output[t][d];
            }
        }
    }

    /* --- Шаг 3: Финальный LayerNorm --- */
    for (size_t t = 0; t < seq_len; t++) {
        float tmp[KAT_EMBED_DIM];
        layer_norm(ws->hidden[t], tmp,
                   model->final_ln_gamma, model->final_ln_beta,
                   KAT_EMBED_DIM);
        memcpy(ws->hidden[t], tmp, KAT_EMBED_DIM * sizeof(float));
    }

    /* --- Шаг 4: LM Head — логиты для следующего токена --- */
    /* Берём последнюю позицию */
    size_t last = seq_len - 1;
    for (size_t v = 0; v < KAT_VOCAB_SIZE; v++) {
        float sum = 0.0f;
        for (size_t d = 0; d < KAT_EMBED_DIM; d++) {
            sum += ws->hidden[last][d] * model->lm_head[d][v];
        }
        ws->logits[v] = sum;
    }

    /* Softmax для вероятностей */
    memcpy(ws->probs, ws->logits, KAT_VOCAB_SIZE * sizeof(float));
    softmax(ws->probs, KAT_VOCAB_SIZE);

    return 0;
}

/* ========== ИЗВЛЕЧЕНИЕ ЭМБЕДДИНГА ========== */

void kat_extract_embedding(const KatWorkspace *ws, float *out) {
    if (!ws || !out || ws->seq_len == 0) return;

    /* Среднее по всем позициям (mean pooling) */
    memset(out, 0, KAT_EMBED_DIM * sizeof(float));
    for (size_t t = 0; t < ws->seq_len; t++) {
        for (size_t d = 0; d < KAT_EMBED_DIM; d++) {
            out[d] += ws->hidden[t][d];
        }
    }
    float inv_n = 1.0f / (float)ws->seq_len;
    for (size_t d = 0; d < KAT_EMBED_DIM; d++) {
        out[d] *= inv_n;
    }
}

/* ========== СЭМПЛИРОВАНИЕ ========== */

uint8_t kat_sample(KatModel *model, const KatWorkspace *ws,
                   float temperature) {
    if (!model || !ws) return 0;

    if (temperature < 0.01f) {
        /* Greedy: argmax */
        uint8_t best = 0;
        float best_p = ws->probs[0];
        for (size_t i = 1; i < KAT_VOCAB_SIZE; i++) {
            if (ws->probs[i] > best_p) {
                best_p = ws->probs[i];
                best = (uint8_t)i;
            }
        }
        return best;
    }

    /* Температурное сэмплирование */
    float scaled[KAT_VOCAB_SIZE];
    memcpy(scaled, ws->logits, KAT_VOCAB_SIZE * sizeof(float));
    float inv_t = 1.0f / temperature;
    for (size_t i = 0; i < KAT_VOCAB_SIZE; i++) {
        scaled[i] *= inv_t;
    }
    softmax(scaled, KAT_VOCAB_SIZE);

    /* Выбор по кумулятивной вероятности */
    float r = (float)(kat_rng_next(&model->seed) & 0xFFFFFF) / (float)0xFFFFFF;
    float cumsum = 0.0f;
    for (size_t i = 0; i < KAT_VOCAB_SIZE; i++) {
        cumsum += scaled[i];
        if (cumsum >= r) return (uint8_t)i;
    }
    return 255;  /* fallback */
}

/* ========== ОБУЧЕНИЕ ========== */

/*
 * Приближённое обучение через «прямую оценку градиента»
 * (Evolutionary Gradient Estimation)
 *
 * Для каждого параметра:
 *   1. Пертурбация параметра на +epsilon
 *   2. Измерение изменения loss
 *   3. Обновление: param -= lr * (delta_loss / epsilon)
 *
 * Это медленнее backprop, но:
 *   - Не требует вычислительного графа
 *   - Работает с любой функцией (даже недифференцируемой)
 *   - Проще в реализации на чистом C
 *
 * Оптимизация: сэмплируем случайные подмножества параметров
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

    /* Текущий loss */
    float base_loss = compute_loss(model, ws, tokens, seq_len, target);

    /*
     * Стохастическая оценка градиента:
     * Сэмплируем N_PROBE случайных параметров из LM head
     * (самый критичный для обучения слой)
     */
    #define N_PROBE 16
    float epsilon = 1e-3f;

    float *lm_params = &model->lm_head[0][0];
    size_t lm_total = KAT_EMBED_DIM * KAT_VOCAB_SIZE;

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
        uint8_t tok = tokens[t];
        float *emb = model->embed.token_embed[tok];
        for (size_t d = 0; d < KAT_EMBED_DIM; d += 16) {
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

/* ========== УТИЛИТЫ ========== */

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

size_t kat_count_params(void) {
    size_t total = 0;

    /* Эмбеддинги */
    total += KAT_VOCAB_SIZE * KAT_EMBED_DIM;   /* token  */
    total += KAT_MAX_SEQ * KAT_EMBED_DIM;      /* pos    */

    /* Трансформерные блоки */
    for (size_t l = 0; l < KAT_NUM_LAYERS; l++) {
        /* Attention: 4 головы × (Wq + Wk + Wv) */
        total += KAT_NUM_HEADS * 3 * KAT_EMBED_DIM * KAT_HEAD_DIM;
        /* Wo */
        total += KAT_EMBED_DIM * KAT_EMBED_DIM;
        /* LayerNorm (attention) */
        total += 2 * KAT_EMBED_DIM;

        /* FFN: W1 + b1 + W2 + b2 */
        total += KAT_EMBED_DIM * KAT_FF_DIM;
        total += KAT_FF_DIM;
        total += KAT_FF_DIM * KAT_EMBED_DIM;
        total += KAT_EMBED_DIM;
        /* LayerNorm (FFN) */
        total += 2 * KAT_EMBED_DIM;
    }

    /* Финальный LayerNorm */
    total += 2 * KAT_EMBED_DIM;

    /* LM head */
    total += KAT_EMBED_DIM * KAT_VOCAB_SIZE;

    return total;
}

size_t kat_serialize(const KatModel *model, uint8_t *buf, size_t buf_size) {
    if (!model) return 0;
    size_t needed = sizeof(KatModel);
    if (!buf || buf_size < needed) return needed;
    memcpy(buf, model, needed);
    return needed;
}

int kat_deserialize(KatModel *model, const uint8_t *buf, size_t buf_size) {
    if (!model || !buf) return -1;
    if (buf_size < sizeof(KatModel)) return -1;
    memcpy(model, buf, sizeof(KatModel));
    return 0;
}
