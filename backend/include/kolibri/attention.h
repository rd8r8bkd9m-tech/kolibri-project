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
 * Архитектура:
 *   vocab_size=256  embed_dim=64  num_heads=4  head_dim=16
 *   ff_dim=256  num_layers=2  context_window=512
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

/* ========== КОНФИГУРАЦИЯ ========== */

#define KAT_VOCAB_SIZE     256    /* Размер словаря (байт-уровень)          */
#define KAT_EMBED_DIM       64    /* Размерность эмбеддинга                 */
#define KAT_NUM_HEADS         4    /* Количество голов внимания               */
#define KAT_HEAD_DIM         16    /* Размерность одной головы (EMBED/HEADS) */
#define KAT_FF_DIM          256    /* Скрытый слой Feed-Forward              */
#define KAT_NUM_LAYERS        2    /* Количество трансформерных блоков       */
#define KAT_MAX_SEQ         512    /* Максимальная длина последовательности  */
#define KAT_EPSILON       1e-5f   /* Epsilon для LayerNorm                  */

/* ========== СТРУКТУРЫ ========== */

/* --- Таблица эмбеддингов --- */
typedef struct {
    float token_embed[KAT_VOCAB_SIZE][KAT_EMBED_DIM];  /* Лексические эмбеддинги */
    float pos_embed[KAT_MAX_SEQ][KAT_EMBED_DIM];       /* Позиционные эмбеддинги */
} KatEmbeddingTable;

/* --- Веса одной головы внимания --- */
typedef struct {
    float wq[KAT_EMBED_DIM][KAT_HEAD_DIM];  /* Проекция Query  */
    float wk[KAT_EMBED_DIM][KAT_HEAD_DIM];  /* Проекция Key    */
    float wv[KAT_EMBED_DIM][KAT_HEAD_DIM];  /* Проекция Value  */
} KatAttentionHead;

/* --- Multi-Head Attention слой --- */
typedef struct {
    KatAttentionHead heads[KAT_NUM_HEADS];           /* Головы внимания     */
    float wo[KAT_EMBED_DIM][KAT_EMBED_DIM];         /* Выходная проекция   */
    float ln_gamma[KAT_EMBED_DIM];                   /* LayerNorm масштаб   */
    float ln_beta[KAT_EMBED_DIM];                    /* LayerNorm смещение  */
} KatMultiHeadAttention;

/* --- Feed-Forward Network --- */
typedef struct {
    float w1[KAT_EMBED_DIM][KAT_FF_DIM];   /* Первый линейный слой    */
    float b1[KAT_FF_DIM];                   /* Bias первого слоя       */
    float w2[KAT_FF_DIM][KAT_EMBED_DIM];   /* Второй линейный слой   */
    float b2[KAT_EMBED_DIM];               /* Bias второго слоя       */
    float ln_gamma[KAT_EMBED_DIM];         /* LayerNorm масштаб       */
    float ln_beta[KAT_EMBED_DIM];          /* LayerNorm смещение      */
} KatFeedForward;

/* --- Один трансформерный блок (Pre-LN архитектура) --- */
typedef struct {
    KatMultiHeadAttention attn;   /* Multi-Head Self-Attention */
    KatFeedForward        ffn;    /* Feed-Forward Network      */
} KatTransformerBlock;

/* --- Полная модель внимания --- */
typedef struct {
    KatEmbeddingTable     embed;                       /* Эмбеддинги             */
    KatTransformerBlock   layers[KAT_NUM_LAYERS];      /* Стек блоков            */
    float                 final_ln_gamma[KAT_EMBED_DIM]; /* Финальный LayerNorm */
    float                 final_ln_beta[KAT_EMBED_DIM];
    float                 lm_head[KAT_EMBED_DIM][KAT_VOCAB_SIZE]; /* Language Model head */
    uint64_t              seed;                        /* PRNG состояние         */
    size_t                param_count;                 /* Общее число параметров */
} KatModel;

/* --- Промежуточные буферы для forward pass --- */
typedef struct {
    float hidden[KAT_MAX_SEQ][KAT_EMBED_DIM];         /* Текущие скрытые         */
    float residual[KAT_MAX_SEQ][KAT_EMBED_DIM];       /* Residual буфер          */
    float q[KAT_MAX_SEQ][KAT_HEAD_DIM];               /* Query одной головы      */
    float k[KAT_MAX_SEQ][KAT_HEAD_DIM];               /* Key одной головы        */
    float v[KAT_MAX_SEQ][KAT_HEAD_DIM];               /* Value одной головы      */
    float attn_scores[KAT_MAX_SEQ][KAT_MAX_SEQ];      /* Матрица внимания        */
    float attn_out[KAT_MAX_SEQ][KAT_EMBED_DIM];       /* Выход Multi-Head        */
    float ff_hidden[KAT_MAX_SEQ][KAT_FF_DIM];         /* Скрытый слой FFN        */
    float logits[KAT_VOCAB_SIZE];                      /* Выходные логиты         */
    float probs[KAT_VOCAB_SIZE];                       /* Softmax вероятности     */
    /* --- Буферы перенесённые со стека в heap --- */
    float mha_normed[KAT_MAX_SEQ][KAT_EMBED_DIM];     /* LayerNorm перед attention */
    float ffn_normed[KAT_MAX_SEQ][KAT_EMBED_DIM];     /* LayerNorm перед FFN       */
    float layer_attn_out[KAT_MAX_SEQ][KAT_EMBED_DIM]; /* Attention output        */
    float layer_ffn_out[KAT_MAX_SEQ][KAT_EMBED_DIM];  /* FFN output              */
    size_t seq_len;                                     /* Текущая длина           */
} KatWorkspace;

/* ========== API ========== */

/* --- Жизненный цикл --- */

/** Создать модель с инициализацией Xavier */
KatModel* kat_model_create(uint64_t seed);

/** Уничтожить модель */
void kat_model_destroy(KatModel *model);

/** Создать рабочий буфер для forward pass */
KatWorkspace* kat_workspace_create(void);

/** Уничтожить рабочий буфер */
void kat_workspace_destroy(KatWorkspace *ws);

/* --- Forward Pass --- */

/**
 * Пропустить последовательность токенов через модель
 *
 * @param model   Модель
 * @param ws      Рабочий буфер
 * @param tokens  Входные токены (байты 0-255)
 * @param seq_len Длина последовательности (≤ KAT_MAX_SEQ)
 * @return 0 при успехе
 *
 * Результат: ws->hidden содержит выходные эмбеддинги,
 *            ws->logits — логиты для следующего токена,
 *            ws->probs — softmax-вероятности
 */
int kat_forward(const KatModel *model, KatWorkspace *ws,
                const uint8_t *tokens, size_t seq_len);

/**
 * Извлечь эмбеддинг последовательности (среднее по позициям)
 *
 * @param ws    Рабочий буфер после forward pass
 * @param out   Выходной вектор [KAT_EMBED_DIM]
 */
void kat_extract_embedding(const KatWorkspace *ws, float *out);

/**
 * Семэплирование следующего токена из распределения
 *
 * @param model        Модель (для PRNG)
 * @param ws           Рабочий буфер после forward pass
 * @param temperature  Температура (0 = greedy, >1 = random)
 * @return Токен 0-255
 */
uint8_t kat_sample(KatModel *model, const KatWorkspace *ws, float temperature);

/* --- Обучение (простой SGD) --- */

/**
 * Один шаг обучения: forward + backward + update
 *
 * @param model      Модель
 * @param ws         Рабочий буфер
 * @param tokens     Входная последовательность
 * @param seq_len    Длина
 * @param target     Целевой следующий токен
 * @param lr         Скорость обучения
 * @return Значение loss (cross-entropy)
 */
float kat_train_step(KatModel *model, KatWorkspace *ws,
                     const uint8_t *tokens, size_t seq_len,
                     uint8_t target, float lr);

/* --- Утилиты --- */

/** Вычислить косинусное сходство двух эмбеддингов */
float kat_cosine_similarity(const float *a, const float *b, size_t dim);

/** Подсчитать общее количество параметров модели */
size_t kat_count_params(void);

/** Сериализация модели в буфер */
size_t kat_serialize(const KatModel *model, uint8_t *buf, size_t buf_size);

/** Десериализация модели из буфера */
int kat_deserialize(KatModel *model, const uint8_t *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_ATTENTION_H */
