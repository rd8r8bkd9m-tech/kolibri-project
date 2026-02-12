/*
 * attention.h
 *
 * Модуль внимания (Self-Attention) и эмбеддингов для Kolibri AGI
 *
 * Реализует:
 *   - Таблицу эмбеддингов (токен → плотный вектор)
 *   - Multi-Head Self-Attention (масштабированное скалярное произведение)
 *   - Feed-Forward Network (двуслойный перцептрон)
 *   - Transformer Block (LayerNorm → Attention → Residual → FFN → Residual)
 *   - Стек трансформерных блоков с контекстным окном
 *
 * Масштабируемая архитектура:
 *   small  — vocab=256  embed=64   heads=4   ff=256   layers=2   seq=512    (~165K)
 *   medium — vocab=256  embed=256  heads=8   ff=1024  layers=8   seq=512    (~6.5M)
 *   large  — vocab=256  embed=768  heads=12  ff=3072  layers=14  seq=1024   (~100M)
 *
 * Интеграция:
 *   Текст → токены → embed → N × TransformerBlock → выходной вектор
 *   Используется World Model для предсказания следующего токена,
 *   а также inference pipeline для семантического понимания запроса.
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_ATTENTION_H
#define KOLIBRI_ATTENTION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНФИГУРАЦИЯ МОДЕЛИ (runtime)
 * ============================================================================ */

/** Конфигурация архитектуры трансформера */
typedef struct {
    int vocab_size;     /* Размер словаря (байт-уровень)          */
    int embed_dim;      /* Размерность эмбеддинга                 */
    int num_heads;      /* Количество голов внимания               */
    int head_dim;       /* Размерность одной головы (embed/heads)  */
    int ff_dim;         /* Скрытый слой Feed-Forward              */
    int num_layers;     /* Количество трансформерных блоков       */
    int max_seq;        /* Максимальная длина последовательности  */
} KatConfig;

#define KAT_EPSILON  1e-5f  /* Epsilon для LayerNorm */

/* --- Предустановленные конфигурации --- */

/** ~165K параметров — оригинальная конфигурация */
KatConfig kat_config_small(void);

/** ~6.5M параметров — промежуточная */
KatConfig kat_config_medium(void);

/** ~100M параметров — production-уровень */
KatConfig kat_config_large(void);

/** Подсчитать количество параметров для заданной конфигурации */
size_t kat_config_count_params(const KatConfig *cfg);

/* --- Обратная совместимость (compile-time значения для small) --- */
#define KAT_VOCAB_SIZE     256
#define KAT_EMBED_DIM       64
#define KAT_NUM_HEADS         4
#define KAT_HEAD_DIM         16
#define KAT_FF_DIM          256
#define KAT_NUM_LAYERS        2
#define KAT_MAX_SEQ         512

/* --- Макрос для 2D-индексации плоского массива --- */
#define KAT_IDX2(row, col, stride) \
    ((size_t)(row) * (size_t)(stride) + (size_t)(col))

/* ============================================================================
 * СТРУКТУРЫ (динамическая аллокация)
 * ============================================================================ */

/* --- Таблица эмбеддингов --- */
typedef struct {
    float *token_embed;     /* [vocab_size * embed_dim] Лексические */
    float *pos_embed;       /* [max_seq * embed_dim]    Позиционные */
} KatEmbeddingTable;

/* --- Веса одной головы внимания --- */
typedef struct {
    float *wq;  /* [embed_dim * head_dim] Проекция Query  */
    float *wk;  /* [embed_dim * head_dim] Проекция Key    */
    float *wv;  /* [embed_dim * head_dim] Проекция Value  */
} KatAttentionHead;

/* --- Multi-Head Attention слой --- */
typedef struct {
    KatAttentionHead *heads;    /* [num_heads] Головы внимания     */
    float *wo;                  /* [embed_dim * embed_dim] Выход   */
    float *ln_gamma;            /* [embed_dim] LayerNorm масштаб   */
    float *ln_beta;             /* [embed_dim] LayerNorm смещение  */
} KatMultiHeadAttention;

/* --- Feed-Forward Network --- */
typedef struct {
    float *w1;          /* [embed_dim * ff_dim] Первый слой  */
    float *b1;          /* [ff_dim]             Bias 1       */
    float *w2;          /* [ff_dim * embed_dim] Второй слой  */
    float *b2;          /* [embed_dim]          Bias 2       */
    float *ln_gamma;    /* [embed_dim]          LN масштаб   */
    float *ln_beta;     /* [embed_dim]          LN смещение  */
} KatFeedForward;

/* --- Один трансформерный блок (Pre-LN архитектура) --- */
typedef struct {
    KatMultiHeadAttention attn;   /* Multi-Head Self-Attention */
    KatFeedForward        ffn;    /* Feed-Forward Network      */
} KatTransformerBlock;

/* --- Полная модель внимания --- */
typedef struct {
    KatConfig             cfg;              /* Конфигурация архитектуры */
    KatEmbeddingTable     embed;            /* Эмбеддинги              */
    KatTransformerBlock  *layers;           /* [num_layers] Стек блоков*/
    float                *final_ln_gamma;   /* [embed_dim] Финальн. LN*/
    float                *final_ln_beta;    /* [embed_dim]             */
    float                *lm_head;          /* [embed_dim * vocab_size]*/
    uint64_t              seed;             /* PRNG состояние          */
    size_t                param_count;      /* Общее число параметров  */
} KatModel;

/* --- Промежуточные буферы для forward pass --- */
typedef struct {
    KatConfig cfg;              /* Конфигурация (копия из модели)   */
    float *hidden;              /* [max_seq * embed_dim]            */
    float *residual;            /* [max_seq * embed_dim]            */
    float *q;                   /* [max_seq * head_dim]             */
    float *k;                   /* [max_seq * head_dim]             */
    float *v;                   /* [max_seq * head_dim]             */
    float *attn_scores;         /* [max_seq * max_seq]              */
    float *attn_out;            /* [max_seq * embed_dim]            */
    float *ff_hidden;           /* [max_seq * ff_dim]               */
    float *logits;              /* [vocab_size]                     */
    float *probs;               /* [vocab_size]                     */
    float *mha_normed;          /* [max_seq * embed_dim]            */
    float *ffn_normed;          /* [max_seq * embed_dim]            */
    float *layer_attn_out;      /* [max_seq * embed_dim]            */
    float *layer_ffn_out;       /* [max_seq * embed_dim]            */
    float *head_out;            /* [max_seq * head_dim]             */
    size_t seq_len;             /* Текущая длина                    */
} KatWorkspace;

/* ============================================================================
 * API
 * ============================================================================ */

/* --- Жизненный цикл --- */

/** Создать модель с заданной конфигурацией и Xavier инициализацией */
KatModel* kat_model_create_ex(const KatConfig *cfg, uint64_t seed);

/** Создать модель small (~165K) — обратная совместимость */
KatModel* kat_model_create(uint64_t seed);

/** Уничтожить модель */
void kat_model_destroy(KatModel *model);

/** Создать рабочий буфер для заданной конфигурации */
KatWorkspace* kat_workspace_create_ex(const KatConfig *cfg);

/** Создать рабочий буфер small — обратная совместимость */
KatWorkspace* kat_workspace_create(void);

/** Уничтожить рабочий буфер */
void kat_workspace_destroy(KatWorkspace *ws);

/* --- Forward Pass --- */

int kat_forward(const KatModel *model, KatWorkspace *ws,
                const uint8_t *tokens, size_t seq_len);

void kat_extract_embedding(const KatWorkspace *ws, float *out);

uint8_t kat_sample(KatModel *model, const KatWorkspace *ws,
                   float temperature);

/* --- Обучение --- */

float kat_train_step(KatModel *model, KatWorkspace *ws,
                     const uint8_t *tokens, size_t seq_len,
                     uint8_t target, float lr);

/** Быстрый тренировочный шаг: 1 forward + аналитический градиент LM head.
 *  ~20× быстрее kat_train_step (без SPSA проб).
 *  Возвращает cross-entropy loss. */
float kat_train_step_fast(KatModel *model, KatWorkspace *ws,
                          const uint8_t *tokens, size_t seq_len,
                          uint8_t target, float lr);

/** Полный backpropagation через все слои трансформера.
 *  Обновляет ВСЕ параметры: LM head, LayerNorm, FFN, Attention, Embeddings.
 *  Медленнее kat_train_step_fast, но даёт глубокое обучение.
 *  Возвращает cross-entropy loss. */
float kat_train_step_full(KatModel *model, KatWorkspace *ws,
                          const uint8_t *tokens, size_t seq_len,
                          uint8_t target, float lr);

/* --- Утилиты --- */

float kat_cosine_similarity(const float *a, const float *b, size_t dim);

/** Подсчёт параметров small — обратная совместимость */
size_t kat_count_params(void);

size_t kat_serialize(const KatModel *model, uint8_t *buf, size_t buf_size);
int kat_deserialize(KatModel *model, const uint8_t *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_ATTENTION_H */
