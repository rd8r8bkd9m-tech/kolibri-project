/*
 * kat_train_backprop.h
 *
 * Настоящий backpropagation для Kolibri Transformer
 * AdamW оптимизатор с gradient clipping и cosine LR scheduler
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_KAT_TRAIN_BACKPROP_H
#define KOLIBRI_KAT_TRAIN_BACKPROP_H

#include "kolibri/attention.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНФИГУРАЦИЯ ОБУЧЕНИЯ
 * ============================================================================ */

/** Тип LR scheduler */
typedef enum {
    KAT_LR_CONSTANT = 0,       /* Постоянный learning rate */
    KAT_LR_COSINE = 1,         /* Cosine annealing с warmup */
    KAT_LR_LINEAR = 2,         /* Linear decay */
    KAT_LR_WARMUP_STABLE = 3   /* Warmup → stable → decay */
} KatLRSchedule;

/** Конфигурация AdamW оптимизатора */
typedef struct {
    float lr;                  /* Learning rate */
    float beta1;               /* Exponential decay для moment (обычно 0.9) */
    float beta2;               /* Exponential decay для variance (обычно 0.999) */
    float eps;                 /* Epsilon для численной стабильности (обычно 1e-8) */
    float weight_decay;        /* Weight decay (обычно 0.01) */
    float max_grad_norm;       /* Max gradient norm для clipping (0 = без clipping) */
} KatAdamWConfig;

/** Конфигурация training */
typedef struct {
    KatAdamWConfig adamw;
    KatLRSchedule lr_schedule;
    int warmup_steps;          /* Количество warmup шагов */
    int total_steps;           /* Общее количество шагов обучения */
    int gradient_accumulation; /* Накопление градиентов (для больших batch) */
} KatTrainingConfig;

/** Состояние оптимизатора AdamW */
typedef struct {
    /* Moment (первый момент градиента) */
    float *m_token_embed;      /* [vocab_size * embed_dim] */
    float *m_pos_embed;        /* [max_seq * embed_dim] */
    float *m_lm_head;          /* [embed_dim * vocab_size] */

    /* Variance (второй момент градиента) */
    float *v_token_embed;
    float *v_pos_embed;
    float *v_lm_head;

    /* Для каждого слоя */
    float *m_attn_wq;          /* [num_layers * num_heads * embed_dim * head_dim] */
    float *m_attn_wk;
    float *m_attn_wv;
    float *m_attn_wo;
    float *m_attn_ln_gamma;
    float *m_ffn_w1;
    float *m_ffn_b1;
    float *m_ffn_w2;
    float *m_ffn_b2;
    float *m_ffn_ln_gamma;
    float *m_final_ln_gamma;

    float *v_attn_wq;
    float *v_attn_wk;
    float *v_attn_wv;
    float *v_attn_wo;
    float *v_attn_ln_gamma;
    float *v_ffn_w1;
    float *v_ffn_b1;
    float *v_ffn_w2;
    float *v_ffn_b2;
    float *v_ffn_ln_gamma;
    float *v_final_ln_gamma;

    /* Счётчики */
    int step;                  /* Текущий шаг */
    int total_steps;           /* Всего шагов */

    /* Статистика */
    float avg_loss;            /* Средний loss */
    float grad_norm;           /* Norm градиента после clipping */
} KatAdamWState;

/** Градиенты для всех параметров */
typedef struct {
    /* Embeddings */
    float *g_token_embed;      /* [vocab_size * embed_dim] */
    float *g_pos_embed;        /* [max_seq * embed_dim] */

    /* LM Head */
    float *g_lm_head;          /* [embed_dim * vocab_size] */

    /* Для каждого слоя */
    float *g_attn_wq;          /* [num_layers * num_heads * embed_dim * head_dim] */
    float *g_attn_wk;
    float *g_attn_wv;
    float *g_attn_wo;
    float *g_attn_ln_gamma;
    float *g_ffn_w1;
    float *g_ffn_b1;
    float *g_ffn_w2;
    float *g_ffn_b2;
    float *g_ffn_ln_gamma;
    float *g_final_ln_gamma;

    /* Мета-информация */
    int num_layers;
    int num_heads;
    int embed_dim;
    int head_dim;
    int ff_dim;
    int vocab_size;
    int max_seq;

    /* Статистика */
    float max_grad_norm;       /* Максимальный norm градиента до clipping */
} KatGradients;

/* ============================================================================
 * API
 * ============================================================================ */

/**
 * Инициализировать состояние AdamW оптимизатора
 *
 * @param state   Состояние оптимизатора (output)
 * @param config  Конфигурация модели
 * @param train_config  Конфигурация training
 * @return 0 на успех
 */
int kat_adamw_init(KatAdamWState *state,
                   const KatConfig *config,
                   const KatTrainingConfig *train_config);

/**
 * Освободить ресурсы AdamW
 */
void kat_adamw_free(KatAdamWState *state);

/**
 * Получить текущий learning rate с учётом scheduler
 */
float kat_get_lr(const KatAdamWState *state,
                 const KatTrainingConfig *train_config);

/**
 * Вычислить градиенты через backpropagation
 *
 * @param model   Модель
 * @param ws      Workspace
 * @param tokens  Входные токены
 * @param seq_len Длина последовательности
 * @param targets Целевые токены (смещены на 1 относительно tokens)
 * @param grads   Градиенты (output)
 * @return Cross-entropy loss
 */
float kat_compute_gradients(KatModel *model,
                            KatWorkspace *ws,
                            const uint8_t *tokens,
                            size_t seq_len,
                            const uint8_t *targets,
                            KatGradients *grads);

/**
 * Применить gradient clipping
 *
 * @param grads       Градиенты
 * @param max_norm    Максимальный norm
 * @return Фактический norm до clipping
 */
float kat_clip_gradients(KatGradients *grads, float max_norm);

/**
 * Шаг AdamW оптимизатора — обновить веса модели
 *
 * @param model   Модель для обновления
 * @param state   Состояние AdamW
 * @param grads   Градиенты
 * @param config  Конфигурация training
 * @return 0 на успех
 */
int kat_adamw_step(KatModel *model,
                   KatAdamWState *state,
                   const KatGradients *grads,
                   const KatTrainingConfig *config);

/**
 * Полный training step: forward + backward + optimizer
 *
 * @param model   Модель
 * @param ws      Workspace
 * @param adamw   Состояние AdamW
 * @param tokens  Входные токены
 * @param seq_len Длина последовательности
 * @param targets Целевые токены
 * @param train_config  Конфигурация training
 * @return Cross-entropy loss
 */
float kat_train_step_backprop(KatModel *model,
                              KatWorkspace *ws,
                              KatAdamWState *adamw,
                              const uint8_t *tokens,
                              size_t seq_len,
                              const uint8_t *targets,
                              const KatTrainingConfig *train_config);

/**
 * Обучение на батче данных
 *
 * @param model         Модель
 * @param ws            Workspace
 * @param adamw         Состояние AdamW
 * @param batch_tokens  Батч токенов [batch_size * seq_len]
 * @param batch_targets Батч targets [batch_size * seq_len]
 * @param batch_size    Размер батча
 * @param seq_len       Длина последовательности
 * @param train_config  Конфигурация training
 * @return Средний loss по батчу
 */
float kat_train_batch(KatModel *model,
                      KatWorkspace *ws,
                      KatAdamWState *adamw,
                      const uint8_t *batch_tokens,
                      const uint8_t *batch_targets,
                      int batch_size,
                      size_t seq_len,
                      const KatTrainingConfig *train_config);

/**
 * Сбросить статистику оптимизатора
 */
void kat_adamw_reset_stats(KatAdamWState *state);

/**
 * Получить статистику обучения
 */
typedef struct {
    int step;
    float lr;
    float loss;
    float grad_norm;
    float avg_loss;
} KatTrainingStats;

KatTrainingStats kat_get_training_stats(const KatAdamWState *state,
                                        const KatTrainingConfig *config);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_KAT_TRAIN_BACKPROP_H */
