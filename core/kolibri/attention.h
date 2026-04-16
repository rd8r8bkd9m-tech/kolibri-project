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
 * Modern архитектура (v2):
 *   small_v2  — vocab=256  embed=64   heads=4   kv_groups=4  ff=256   layers=2   seq=2048
 *   medium_v2 — vocab=256  embed=256  heads=8   kv_groups=4  ff=1024  layers=8   seq=2048
 *   large_v2  — vocab=256  embed=768  heads=12  kv_groups=3  ff=3072  layers=14  seq=2048
 *
 *   v2 отличия: RoPE вместо sinusoidal, RMSNorm вместо LayerNorm,
 *   GQA (Grouped-Query Attention), SwiGLU вместо GELU.
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

#include <pthread.h>
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
    int vocab_size; /* Размер словаря (байт-уровень)          */
    int embed_dim;  /* Размерность эмбеддинга                 */
    int num_heads;  /* Количество голов внимания               */
    int head_dim;   /* Размерность одной головы (embed/heads)  */
    int ff_dim;     /* Скрытый слой Feed-Forward              */
    int num_layers; /* Количество трансформерных блоков       */
    int max_seq;    /* Максимальная длина последовательности  */
} KatConfig;

#define KAT_EPSILON 1e-5f     /* Epsilon для LayerNorm */
#define KAT_RMS_EPSILON 1e-8f /* Epsilon для RMSNorm */

/* --- Тип активации --- */
typedef enum {
    KAT_ACTIVATION_GELU = 0,  /* Gaussian ErrorLU (оригинал) */
    KAT_ACTIVATION_SWIGLU = 1 /* Swish-Gated Linear Unit (modern) */
} KatActivation;

/* --- Конфигурация GQA (Grouped-Query Attention) --- */
typedef struct {
    int num_kv_heads; /* Количество KV голов (num_heads / kv_groups) */
    int kv_groups;    /* Сколько query heads разделяют одну KV голову */
} KatGQAConfig;

/* --- Конфигурация архитектуры v2 (современная) --- */
typedef struct {
    int vocab_size;           /* Размер словаря (байт-уровень)           */
    int embed_dim;            /* Размерность эмбеддинга                  */
    int num_heads;            /* Количество голов внимания                */
    int head_dim;             /* Размерность одной головы (embed/heads)  */
    int ff_dim;               /* Скрытый слой Feed-Forward               */
    int num_layers;           /* Количество трансформерных блоков        */
    int max_seq;              /* Максимальная длина последовательности   */
    int kv_groups;            /* GQA: групп query на одну KV голову      */
    int num_kv_heads;         /* GQA: количество KV голов                */
    KatActivation activation; /* Тип функции активации              */
    int use_rope;             /* Флаг использования RoPE (1/0)          */
    float rope_theta;         /* Базовая частота RoPE (default 10000.0) */
} KatConfigV2;

/* --- Предустановленные конфигурации --- */

/** ~165K параметров — оригинальная конфигурация */
KatConfig kat_config_small(void);

/** ~6.5M параметров — промежуточная */
KatConfig kat_config_medium(void);

/** ~100M параметров — production-уровень */
KatConfig kat_config_large(void);

/** Подсчитать количество параметров для заданной конфигурации */
size_t kat_config_count_params(const KatConfig *cfg);

/* --- V2 конфигурации (современная архитектура) --- */

/** ~165K параметров с RoPE, RMSNorm, GQA, SwiGLU */
KatConfigV2 kat_config_v2_small(void);

/** ~6.5M параметров с RoPE, RMSNorm, GQA, SwiGLU */
KatConfigV2 kat_config_v2_medium(void);

/** ~100M параметров с RoPE, RMSNorm, GQA, SwiGLU */
KatConfigV2 kat_config_v2_large(void);

/** Подсчитать количество параметров для v2 конфигурации */
size_t kat_config_v2_count_params(const KatConfigV2 *cfg);

/** Конвертировать KatConfigV2 в KatConfig (обратная совместимость) */
KatConfig kat_config_v2_to_v1(const KatConfigV2 *cfg_v2);

/* --- Обратная совместимость (compile-time значения для small) --- */
#define KAT_VOCAB_SIZE 256
#define KAT_EMBED_DIM 64
#define KAT_NUM_HEADS 4
#define KAT_HEAD_DIM 16
#define KAT_FF_DIM 256
#define KAT_NUM_LAYERS 2
#define KAT_MAX_SEQ 512

/* --- Макрос для 2D-индексации плоского массива --- */
#define KAT_IDX2(row, col, stride) ((size_t)(row) * (size_t)(stride) + (size_t)(col))

/* ============================================================================
 * СТРУКТУРЫ (динамическая аллокация)
 * ============================================================================ */

/* --- Таблица эмбеддингов --- */
typedef struct {
    float *token_embed; /* [vocab_size * embed_dim] Лексические */
    float *pos_embed;   /* [max_seq * embed_dim]    Позиционные (sinusoidal) */
} KatEmbeddingTable;

/* --- RoPE кэш (precomputed rotary embeddings) --- */
typedef struct {
    float *cos_cache; /* [max_seq * head_dim] Cosine cache  */
    float *sin_cache; /* [max_seq * head_dim] Sine cache    */
    int max_seq;      /* Максимальная длина                 */
    int head_dim;     /* Размерность головы                 */
    float theta;      /* Базовая частота RoPE               */
} KatRoPECache;

/* --- Веса одной головы внимания --- */
typedef struct {
    float *wq; /* [embed_dim * head_dim] Проекция Query  */
    float *wk; /* [embed_dim * head_dim] Проекция Key    */
    float *wv; /* [embed_dim * head_dim] Проекция Value  */
} KatAttentionHead;

/* --- Multi-Head Attention слой --- */
typedef struct {
    KatAttentionHead *heads; /* [num_heads] Головы внимания     */
    float *wo;               /* [embed_dim * embed_dim] Выход   */
    float *ln_gamma;         /* [embed_dim] LayerNorm/RMSNorm масштаб */
    float *ln_beta;          /* [embed_dim] LayerNorm смещение (NULL для RMSNorm) */
} KatMultiHeadAttention;

/* --- KV проекции для GQA (разделяются между группами query heads) --- */
typedef struct {
    float *wk; /* [embed_dim * (head_dim * num_kv_heads)] Key для всех KV голов */
    float *wv; /* [embed_dim * (head_dim * num_kv_heads)] Value для всех KV голов */
} KatGQAHead;

/* --- Feed-Forward Network --- */
typedef struct {
    float *w1;       /* [embed_dim * ff_dim] Первый слой  */
    float *b1;       /* [ff_dim]             Bias 1       */
    float *w2;       /* [ff_dim * embed_dim] Второй слой  */
    float *b2;       /* [embed_dim]          Bias 2       */
    float *ln_gamma; /* [embed_dim]          LN/RMSNorm масштаб */
    float *ln_beta;  /* [embed_dim]          LN смещение (NULL для RMSNorm) */
    /* SwiGLU variant: w3 for gated branch, no bias needed for RMSNorm */
    float *w3; /* [embed_dim * ff_dim] SwiGLU gate (NULL для GELU) */
} KatFeedForward;

/* --- Один трансформерный блок (Pre-LN архитектура) --- */
typedef struct {
    KatMultiHeadAttention attn; /* Multi-Head Self-Attention */
    KatFeedForward ffn;         /* Feed-Forward Network      */
} KatTransformerBlock;

/* --- Полная модель внимания --- */
typedef struct {
    KatConfig cfg;               /* Конфигурация архитектуры */
    KatEmbeddingTable embed;     /* Эмбеддинги              */
    KatTransformerBlock *layers; /* [num_layers] Стек блоков*/
    float *final_ln_gamma;       /* [embed_dim] Финальн. LN/RMSNorm*/
    float *final_ln_beta;        /* [embed_dim] Финальн. LN (NULL для RMSNorm)*/
    float *lm_head;              /* [embed_dim * vocab_size]*/
    uint64_t seed;               /* PRNG состояние          */
    size_t param_count;          /* Общее число параметров  */
    /* --- v2 поля (modern architecture) --- */
    KatConfigV2 cfg_v2;    /* V2 конфигурация         */
    KatRoPECache rope;     /* RoPE кэш                */
    int is_v2;             /* Флаг: 1 = v2 модель, 0 = v1 */
    KatGQAHead *gqa_heads; /* [num_layers] GQA KV проекции (NULL для v1) */
    pthread_mutex_t       mutex;            /* Защита weights от concurrent access */
} KatModel;

/* --- Промежуточные буферы для forward pass --- */
typedef struct {
    KatConfig cfg;         /* Конфигурация (копия из модели)   */
    float *hidden;         /* [max_seq * embed_dim]            */
    float *residual;       /* [max_seq * embed_dim]            */
    float *q;              /* [max_seq * head_dim]             */
    float *k;              /* [max_seq * head_dim]             */
    float *v;              /* [max_seq * head_dim]             */
    float *attn_scores;    /* [max_seq * max_seq]              */
    float *attn_out;       /* [max_seq * embed_dim]            */
    float *ff_hidden;      /* [max_seq * ff_dim]               */
    float *logits;         /* [vocab_size]                     */
    float *probs;          /* [vocab_size]                     */
    float *mha_normed;     /* [max_seq * embed_dim]            */
    float *ffn_normed;     /* [max_seq * embed_dim]            */
    float *layer_attn_out; /* [max_seq * embed_dim]            */
    float *layer_ffn_out;  /* [max_seq * embed_dim]            */
    float *head_out;       /* [max_seq * head_dim]             */
    size_t seq_len;        /* Текущая длина                    */
} KatWorkspace;

/* ============================================================================
 * API
 * ============================================================================ */

/* --- Жизненный цикл --- */

/** Создать модель с заданной конфигурацией и Xavier инициализацией */
KatModel *kat_model_create_ex(const KatConfig *cfg, uint64_t seed);

/** Создать модель v2 с заданной конфигурацией (RoPE, RMSNorm, GQA, SwiGLU) */
KatModel *kat_model_create_v2(const KatConfigV2 *cfg, uint64_t seed);

/** Создать модель small (~165K) — обратная совместимость */
KatModel *kat_model_create(uint64_t seed);

/** Уничтожить модель */
void kat_model_destroy(KatModel *model);

/** Создать рабочий буфер для заданной конфигурации */
KatWorkspace *kat_workspace_create_ex(const KatConfig *cfg);

/** Создать рабочий буфер для v2 конфигурации (с поддержкой GQA) */
KatWorkspace *kat_workspace_create_v2(const KatConfigV2 *cfg);

/** Создать рабочий буфер small — обратная совместимость */
KatWorkspace *kat_workspace_create(void);

/** Уничтожить рабочий буфер */
void kat_workspace_destroy(KatWorkspace *ws);

/* --- Forward Pass --- */

int kat_forward(const KatModel *model, KatWorkspace *ws, const uint8_t *tokens, size_t seq_len);

/** Forward pass v2 (поддерживает RoPE, RMSNorm, GQA, SwiGLU) */
int kat_forward_v2(const KatModel *model, KatWorkspace *ws, const uint8_t *tokens, size_t seq_len);

void kat_extract_embedding(const KatWorkspace *ws, float *out);

uint8_t kat_sample(KatModel *model, const KatWorkspace *ws, float temperature);

/* --- RoPE утилиты --- */

/** Инициализировать RoPE кэш для заданной конфигурации */
void kat_rope_init(KatRoPECache *rope, int max_seq, int head_dim, float theta);

/** Освободить RoPE кэш */
void kat_rope_destroy(KatRoPECache *rope);

/** Применить RoPE к query/key вектору */
void kat_rope_apply(const KatRoPECache *rope, float *x, int seq_len, int head_dim);

/* --- RMSNorm утилиты --- */

/** RMSNorm: нормализация без смещения */
void kat_rms_norm(const float *x, float *out, const float *weight, int dim);

/* --- GQA утилиты --- */

/** Повторить KV головы для соответствия query heads (GQA) */
void kat_gqa_expand_kv(const float *kv, int num_kv_heads, int num_q_heads, int head_dim, float *out);

/* --- SwiGLU утилиты --- */

/** SwiGLU активация: silu(x * w3) * (x * w1 + b1) */
void kat_swiglu(const float *x, const float *w1, const float *b1, const float *w3, float *out, int rows, int in_dim,
                int out_dim);

/* --- Обучение --- */

float kat_train_step(KatModel *model, KatWorkspace *ws, const uint8_t *tokens, size_t seq_len, uint8_t target,
                     float lr);

/** Быстрый тренировочный шаг: 1 forward + аналитический градиент LM head.
 *  ~20× быстрее kat_train_step (без SPSA проб).
 *  Возвращает cross-entropy loss. */
float kat_train_step_fast(KatModel *model, KatWorkspace *ws, const uint8_t *tokens, size_t seq_len, uint8_t target,
                          float lr);

/** Полный backpropagation через все слои трансформера.
 *  Обновляет ВСЕ параметры: LM head, LayerNorm, FFN, Attention, Embeddings.
 *  Медленнее kat_train_step_fast, но даёт глубокое обучение.
 *  Возвращает cross-entropy loss. */
float kat_train_step_full(KatModel *model, KatWorkspace *ws, const uint8_t *tokens, size_t seq_len, uint8_t target,
                          float lr);

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
