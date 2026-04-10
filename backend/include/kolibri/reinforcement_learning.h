/*
 * reinforcement_learning.h
 *
 * Reinforcement Learning для Kolibri
 * Q-learning с функцией вознаграждения на основе качества ответов
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_REINFORCEMENT_LEARNING_H
#define KOLIBRI_REINFORCEMENT_LEARNING_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#include "kolibri/intent_classifier.h"
#include "kolibri/reasoning_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНСТАНТЫ
 * ============================================================================ */

/** Максимальное количество состояний */
#define KRL_MAX_STATES 1024

/** Максимальное количество действий */
#define KRL_MAX_ACTIONS 64

/** Максимальный размер replay buffer */
#define KRL_REPLAY_BUFFER_SIZE 10000

/** Размер мини-батча для обучения */
#define KRL_MINIBATCH_SIZE 32

/** Коэффициент дисконтирования (gamma) */
#define KRL_DEFAULT_GAMMA 0.99

/** Скорость обучения (alpha) */
#define KRL_DEFAULT_ALPHA 0.001

/** Epsilon для epsilon-greedy стратегии */
#define KRL_DEFAULT_EPSILON 0.1

/** Минимальная epsilon */
#define KRL_MIN_EPSILON 0.01

/** Скорость затухания epsilon */
#define KRL_EPSILON_DECAY 0.995

/* ============================================================================
 * ТИПЫ ДАННЫХ
 * ============================================================================ */

/** Действие (выбор метода обработки) */
typedef enum {
    KRL_ACTION_USE_KNOWLEDGE_BASE = 0,   /* Использовать базу знаний */
    KRL_ACTION_USE_REASONING = 1,         /* Использовать reasoning engine */
    KRL_ACTION_USE_MATH_SOLVER = 2,       /* Использовать math solver */
    KRL_ACTION_USE_FORMULA_POOL = 3,      /* Использовать формулы */
    KRL_ACTION_USE_WORLD_MODEL = 4,       /* Использовать world model */
    KRL_ACTION_USE_ANALOGY = 5,           /* Использовать аналогии */
    KRL_ACTION_USE_COUNTERFACTUAL = 6,    /* Counterfactual reasoning */
    KRL_ACTION_FALLBACK_CHAT = 7,         /* Fallback на LLM */
    KRL_ACTION_COUNT
} KolibriRLAction;

/** Состояние (контекст запроса) */
typedef struct {
    KolibriIntent intent;                 /* Intent запроса */
    double complexity;                    /* Сложность (0-1) */
    int requires_reasoning;               /* Требуется рассуждение */
    int requires_knowledge;               /* Требуется знание */
    char domain[64];                      /* Домен */
    uint32_t hash;                        /* Хэш состояния */
} KolibriRLState;

/** Experience tuple */
typedef struct {
    KolibriRLState state;                 /* Состояние */
    KolibriRLAction action;               /* Действие */
    double reward;                        /* Вознаграждение */
    KolibriRLState next_state;            /* Следующее состояние */
    int done;                             /* Терминальное состояние */
} KolibriExperience;

/** Q-Table entry */
typedef struct {
    KolibriRLState state;
    double q_values[KRL_MAX_ACTIONS];
    int visit_count;
    uint64_t last_updated;
} KolibriQEntry;

/** Статистика метода */
typedef struct {
    uint64_t total_updates;
    double total_reward;
    double average_reward;
    double best_reward;
    double worst_reward;
    
    /* По действиям */
    uint64_t action_counts[KRL_MAX_ACTIONS];
    double action_rewards[KRL_MAX_ACTIONS];
    
    /* Обучение */
    double current_epsilon;
    uint64_t exploration_count;
    uint64_t exploitation_count;
    
    /* Качество */
    double accuracy;                      /* Точность */
    double response_quality;              /* Качество ответов */
    uint64_t successful_responses;
    uint64_t total_responses;
} KolibriRLStats;

/** Контекст reinforcement learning */
typedef struct {
    /* Q-Table */
    KolibriQEntry q_table[KRL_MAX_STATES];
    int num_states;
    
    /* Replay buffer */
    KolibriExperience replay_buffer[KRL_REPLAY_BUFFER_SIZE];
    int replay_position;
    int replay_count;
    
    /* Параметры */
    double alpha;                         /* Learning rate */
    double gamma;                         /* Discount factor */
    double epsilon;                       /* Exploration rate */
    
    /* Thread safety */
    pthread_mutex_t lock;
    
    /* Статистика */
    KolibriRLStats stats;
    
    /* Callback для обратной связи */
    void (*reward_callback)(double reward, void *user_data);
    void *reward_user_data;
} KolibriRLContext;

/* ============================================================================
 * API: ИНИЦИАЛИЗАЦИЯ
 * ============================================================================ */

/**
 * Инициализировать RL контекст
 */
int kolibri_rl_init(KolibriRLContext *ctx);

/**
 * Освободить ресурсы
 */
void kolibri_rl_destroy(KolibriRLContext *ctx);

/* ============================================================================
 * API: ВЫБОР ДЕЙСТВИЯ
 * ============================================================================ */

/**
 * Выбрать действие (epsilon-greedy с Q-learning)
 *
 * @param ctx       RL контекст
 * @param state     Текущее состояние
 * @param action    Выбранное действие (output)
 * @return 0 на успех
 */
int kolibri_rl_select_action(KolibriRLContext *ctx,
                             const KolibriRLState *state,
                             KolibriRLAction *action);

/**
 * Выбрать действие жадно (без exploration)
 */
KolibriRLAction kolibri_rl_select_action_greedy(KolibriRLContext *ctx,
                                                const KolibriRLState *state);

/**
 * Выбрать случайное действие (exploration)
 */
KolibriRLAction kolibri_rl_select_action_random(const KolibriRLState *state);

/* ============================================================================
 * API: ОБНОВЛЕНИЕ Q-TABLE
 * ============================================================================ */

/**
 * Обновить Q-значение (one-step Q-learning)
 *
 * Q(s,a) += alpha * [r + gamma * max_a' Q(s',a') - Q(s,a)]
 */
int kolibri_rl_update(KolibriRLContext *ctx,
                      const KolibriRLState *state,
                      KolibriRLAction action,
                      double reward,
                      const KolibriRLState *next_state);

/**
 * Сохранить experience в replay buffer
 */
int kolibri_rl_store_experience(KolibriRLContext *ctx,
                                const KolibriExperience *experience);

/**
 * Обучить на мини-батче из replay buffer
 */
int kolibri_rl_replay(KolibriRLContext *ctx, int batch_size);

/* ============================================================================
 * API: ВОЗНАГРАЖДЕНИЕ
 * ============================================================================ */

/**
 * Вычислить вознаграждение на основе качества ответа
 *
 * @param confidence      Уверенность ответа
 * @param response_time   Время ответа (ms)
 * @param user_feedback   Обратная связь пользователя (-1 до +1)
 * @param intent_match    Intent классификация совпала
 * @return вознаграждение (0-1)
 */
double kolibri_rl_compute_reward(double confidence,
                                 double response_time,
                                 double user_feedback,
                                 int intent_match);

/**
 * Установить callback для вознаграждения
 */
void kolibri_rl_set_reward_callback(KolibriRLContext *ctx,
                                    void (*callback)(double, void*),
                                    void *user_data);

/* ============================================================================
 * API: СТАТИСТИКА И МОНИТОРИНГ
 * ============================================================================ */

/**
 * Получить статистику
 */
void kolibri_rl_get_stats(const KolibriRLContext *ctx, KolibriRLStats *stats);

/**
 * Распечатать статистику
 */
void kolibri_rl_print_stats(const KolibriRLContext *ctx);

/**
 * Распечатать Q-Table (топ-N записей)
 */
void kolibri_rl_print_q_table(const KolibriRLContext *ctx, int top_n);

/**
 * Сохранить Q-Table в файл
 */
int kolibri_rl_save_qtable(const KolibriRLContext *ctx, const char *filepath);

/**
 * Загрузить Q-Table из файла
 */
int kolibri_rl_load_qtable(KolibriRLContext *ctx, const char *filepath);

/* ============================================================================
 * API: УТИЛИТЫ
 * ============================================================================ */

/**
 * Получить название действия
 */
const char* kolibri_rl_action_name(KolibriRLAction action);

/**
 * Получить описание действия
 */
const char* kolibri_rl_action_desc(KolibriRLAction action);

/**
 * Хешировать состояние
 */
uint32_t kolibri_rl_hash_state(const KolibriRLState *state);

/**
 * Проверить, есть ли состояние в Q-Table
 */
int kolibri_rl_has_state(const KolibriRLContext *ctx, const KolibriRLState *state);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_REINFORCEMENT_LEARNING_H */
