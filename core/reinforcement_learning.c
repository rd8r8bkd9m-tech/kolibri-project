/*
 * reinforcement_learning.c
 *
 * Reinforcement Learning для Kolibri
 * Q-learning с функцией вознаграждения на основе качества ответов
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/reinforcement_learning.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
 * ============================================================================ */

static double rl_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static double random_double(void) { return (double)rand() / RAND_MAX; }

static int random_int(int min, int max) { return min + rand() % (max - min + 1); }

/* ============================================================================
 * ИНИЦИАЛИЗАЦИЯ
 * ============================================================================ */

int kolibri_rl_init(KolibriRLContext *ctx) {
    if (!ctx)
        return -1;

    memset(ctx, 0, sizeof(*ctx));

    /* Параметры по умолчанию */
    ctx->alpha = KRL_DEFAULT_ALPHA;
    ctx->gamma = KRL_DEFAULT_GAMMA;
    ctx->epsilon = KRL_DEFAULT_EPSILON;

    /* Инициализация mutex */
    pthread_mutex_init(&ctx->lock, NULL);

    /* Статистика */
    ctx->stats.worst_reward = 1.0;
    ctx->stats.current_epsilon = ctx->epsilon;

    /* Seed для random */
    srand((unsigned int)time(NULL));

    return 0;
}

void kolibri_rl_destroy(KolibriRLContext *ctx) {
    if (!ctx)
        return;
    pthread_mutex_destroy(&ctx->lock);
}

/* ============================================================================
 * ВЫБОР ДЕЙСТВИЯ
 * ============================================================================ */

int kolibri_rl_select_action(KolibriRLContext *ctx, const KolibriRLState *state, KolibriRLAction *action) {
    if (!ctx || !state || !action)
        return -1;

    pthread_mutex_lock(&ctx->lock);

    /* Epsilon-greedy стратегия */
    double epsilon = ctx->epsilon;

    if (random_double() < epsilon) {
        /* Exploration */
        *action = kolibri_rl_select_action_random(state);
        ctx->stats.exploration_count++;
    } else {
        /* Exploitation */
        *action = kolibri_rl_select_action_greedy(ctx, state);
        ctx->stats.exploitation_count++;
    }

    pthread_mutex_unlock(&ctx->lock);

    return 0;
}

KolibriRLAction kolibri_rl_select_action_greedy(KolibriRLContext *ctx, const KolibriRLState *state) {
    /* Находим состояние в Q-Table */
    KolibriQEntry *best_entry = NULL;
    double best_q = -1e10;
    KolibriRLAction best_action = KRL_ACTION_USE_KNOWLEDGE_BASE; /* Default */

    for (int i = 0; i < ctx->num_states; i++) {
        KolibriQEntry *entry = &ctx->q_table[i];

        /* Проверяем совпадение состояния */
        if (entry->state.hash == state->hash && entry->state.intent == state->intent &&
            entry->state.requires_reasoning == state->requires_reasoning) {

            /* Находим лучшее действие */
            for (int a = 0; a < KRL_ACTION_COUNT; a++) {
                if (entry->q_values[a] > best_q) {
                    best_q = entry->q_values[a];
                    best_action = (KolibriRLAction)a;
                }
            }
            return best_action;
        }
    }

    /* Состояние не найдено - возвращаем действие по умолчанию */
    if (state->requires_knowledge) {
        return KRL_ACTION_USE_KNOWLEDGE_BASE;
    } else if (state->requires_reasoning) {
        return KRL_ACTION_USE_REASONING;
    }

    return KRL_ACTION_FALLBACK_CHAT;
}

KolibriRLAction kolibri_rl_select_action_random(const KolibriRLState *state) {
    (void)state;
    return (KolibriRLAction)random_int(0, KRL_ACTION_COUNT - 1);
}

/* ============================================================================
 * ОБНОВЛЕНИЕ Q-TABLE
 * ============================================================================ */

int kolibri_rl_update(KolibriRLContext *ctx, const KolibriRLState *state, KolibriRLAction action, double reward,
                      const KolibriRLState *next_state) {
    if (!ctx || !state)
        return -1;

    pthread_mutex_lock(&ctx->lock);

    /* Находим или создаем запись */
    KolibriQEntry *entry = NULL;

    for (int i = 0; i < ctx->num_states; i++) {
        if (ctx->q_table[i].state.hash == state->hash && ctx->q_table[i].state.intent == state->intent) {
            entry = &ctx->q_table[i];
            break;
        }
    }

    /* Создаем новую запись если не найдена */
    if (!entry && ctx->num_states < KRL_MAX_STATES) {
        entry = &ctx->q_table[ctx->num_states];
        memset(entry, 0, sizeof(*entry));
        memcpy(&entry->state, state, sizeof(*state));
        ctx->num_states++;
    }

    if (!entry) {
        pthread_mutex_unlock(&ctx->lock);
        return -1; /* Q-Table full */
    }

    /* Вычисляем max Q(s', a') */
    double max_next_q = 0.0;

    for (int i = 0; i < ctx->num_states; i++) {
        if (ctx->q_table[i].state.hash == next_state->hash) {
            for (int a = 0; a < KRL_ACTION_COUNT; a++) {
                if (ctx->q_table[i].q_values[a] > max_next_q) {
                    max_next_q = ctx->q_table[i].q_values[a];
                }
            }
            break;
        }
    }

    /* Q-learning update */
    double old_q = entry->q_values[action];
    double new_q = old_q + ctx->alpha * (reward + ctx->gamma * max_next_q - old_q);

    entry->q_values[action] = new_q;
    entry->visit_count++;
    entry->last_updated = (uint64_t)rl_time_ms();

    /* Обновляем статистику */
    ctx->stats.total_updates++;
    ctx->stats.total_reward += reward;
    ctx->stats.average_reward = ctx->stats.total_reward / ctx->stats.total_updates;

    if (reward > ctx->stats.best_reward)
        ctx->stats.best_reward = reward;
    if (reward < ctx->stats.worst_reward)
        ctx->stats.worst_reward = reward;

    ctx->stats.action_counts[action]++;
    ctx->stats.action_rewards[action] += reward;

    /* Decay epsilon */
    ctx->epsilon *= KRL_EPSILON_DECAY;
    if (ctx->epsilon < KRL_MIN_EPSILON) {
        ctx->epsilon = KRL_MIN_EPSILON;
    }
    ctx->stats.current_epsilon = ctx->epsilon;

    pthread_mutex_unlock(&ctx->lock);

    /* Callback */
    if (ctx->reward_callback) {
        ctx->reward_callback(reward, ctx->reward_user_data);
    }

    return 0;
}

int kolibri_rl_store_experience(KolibriRLContext *ctx, const KolibriExperience *experience) {
    if (!ctx || !experience)
        return -1;

    pthread_mutex_lock(&ctx->lock);

    ctx->replay_buffer[ctx->replay_position] = *experience;
    ctx->replay_position = (ctx->replay_position + 1) % KRL_REPLAY_BUFFER_SIZE;

    if (ctx->replay_count < KRL_REPLAY_BUFFER_SIZE) {
        ctx->replay_count++;
    }

    pthread_mutex_unlock(&ctx->lock);

    return 0;
}

int kolibri_rl_replay(KolibriRLContext *ctx, int batch_size) {
    if (!ctx || ctx->replay_count < batch_size)
        return -1;

    if (batch_size > KRL_MINIBATCH_SIZE) {
        batch_size = KRL_MINIBATCH_SIZE;
    }

    double total_loss = 0.0;

    for (int i = 0; i < batch_size; i++) {
        /* Случайная выборка из replay buffer */
        int idx = random_int(0, ctx->replay_count - 1);
        KolibriExperience *exp = &ctx->replay_buffer[idx];

        /* Обновляем Q-значение */
        kolibri_rl_update(ctx, &exp->state, exp->action, exp->reward, &exp->next_state);
    }

    return 0;
}

/* ============================================================================
 * ВОЗНАГРАЖДЕНИЕ
 * ============================================================================ */

double kolibri_rl_compute_reward(double confidence, double response_time, double user_feedback, int intent_match) {
    double reward = 0.0;

    /* Компонент уверенности (0-0.35) */
    reward += confidence * 0.35;

    /* Компонент времени (0-0.2) - быстрее = лучше */
    double time_score = response_time < 100 ? 1.0 : (response_time < 500 ? 0.7 : (response_time < 1000 ? 0.4 : 0.1));
    reward += time_score * 0.2;

    /* Обратная связь пользователя (-0.25 до +0.25) */
    reward += user_feedback * 0.25;

    /* Совпадение intent (0-0.2) */
    reward += (intent_match ? 0.2 : 0.0);

    /* Нормализация к [0, 1] */
    reward = (reward + 0.25) / 1.25;

    if (reward < 0.0)
        reward = 0.0;
    if (reward > 1.0)
        reward = 1.0;

    return reward;
}

void kolibri_rl_set_reward_callback(KolibriRLContext *ctx, void (*callback)(double, void *), void *user_data) {
    if (!ctx)
        return;
    ctx->reward_callback = callback;
    ctx->reward_user_data = user_data;
}

/* ============================================================================
 * СТАТИСТИКА
 * ============================================================================ */

void kolibri_rl_get_stats(const KolibriRLContext *ctx, KolibriRLStats *stats) {
    if (!ctx || !stats)
        return;

    pthread_mutex_lock(&ctx->lock);
    memcpy(stats, &ctx->stats, sizeof(*stats));
    pthread_mutex_unlock(&ctx->lock);
}

void kolibri_rl_print_stats(const KolibriRLContext *ctx) {
    if (!ctx)
        return;

    printf("\n=== Reinforcement Learning Stats ===\n");
    printf("Total Updates: %" PRIu64 "\n", ctx->stats.total_updates);
    printf("Average Reward: %.3f\n", ctx->stats.average_reward);
    printf("Best Reward: %.3f\n", ctx->stats.best_reward);
    printf("Worst Reward: %.3f\n", ctx->stats.worst_reward);
    printf("Current Epsilon: %.3f\n", ctx->stats.current_epsilon);
    printf("Exploration Count: %" PRIu64 "\n", ctx->stats.exploration_count);
    printf("Exploitation Count: %" PRIu64 "\n", ctx->stats.exploitation_count);
    printf("Accuracy: %.1f%% (%" PRIu64 "/%" PRIu64 ")\n",
           ctx->stats.total_responses > 0 ? (double)ctx->stats.successful_responses / ctx->stats.total_responses * 100
                                          : 0,
           ctx->stats.successful_responses, ctx->stats.total_responses);

    printf("\nAction Distribution:\n");
    for (int i = 0; i < KRL_ACTION_COUNT; i++) {
        if (ctx->stats.action_counts[i] > 0) {
            double avg_reward = ctx->stats.action_rewards[i] / ctx->stats.action_counts[i];
            printf("  %-30s: %6" PRIu64 " times (avg reward: %.3f)\n", kolibri_rl_action_name((KolibriRLAction)i),
                   ctx->stats.action_counts[i], avg_reward);
        }
    }
}

void kolibri_rl_print_q_table(const KolibriRLContext *ctx, int top_n) {
    if (!ctx || top_n <= 0)
        return;

    /* Create mutable copy for sorting */
    KolibriQEntry *sorted_table = malloc(sizeof(KolibriQEntry) * ctx->num_states);
    if (!sorted_table) {
        fprintf(stderr, "Failed to allocate memory for sorting\n");
        return;
    }
    memcpy(sorted_table, ctx->q_table, sizeof(KolibriQEntry) * ctx->num_states);

    printf("\n=== Q-Table (Top %d States) ===\n", top_n);
    printf("Total States: %d\n", ctx->num_states);

    /* Sort by visit count (selection sort for top N) */
    for (int i = 0; i < ctx->num_states && i < top_n; i++) {
        int max_idx = i;
        for (int j = i + 1; j < ctx->num_states; j++) {
            if (sorted_table[j].visit_count > sorted_table[max_idx].visit_count) {
                max_idx = j;
            }
        }

        /* Swap */
        if (max_idx != i) {
            KolibriQEntry temp = sorted_table[i];
            sorted_table[i] = sorted_table[max_idx];
            sorted_table[max_idx] = temp;
        }

        KolibriQEntry *entry = &sorted_table[i];
        printf("\nState %d: intent=%s, hash=0x%08x, visits=%d\n", i, kolibri_ic_intent_name(entry->state.intent),
               entry->state.hash, entry->visit_count);

        for (int a = 0; a < KRL_ACTION_COUNT; a++) {
            if (entry->q_values[a] != 0.0) {
                printf("  %-30s: %.3f\n", kolibri_rl_action_name((KolibriRLAction)a), entry->q_values[a]);
            }
        }
    }

    free(sorted_table);
}

int kolibri_rl_save_qtable(const KolibriRLContext *ctx, const char *filepath) {
    if (!ctx || !filepath)
        return -1;

    FILE *f = fopen(filepath, "wb");
    if (!f)
        return -1;

    /* Записываем заголовок */
    uint32_t magic = 0x4B524C51; /* "KRLQ" */
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&ctx->num_states, sizeof(ctx->num_states), 1, f);

    /* Записываем Q-Table */
    fwrite(ctx->q_table, sizeof(KolibriQEntry), ctx->num_states, f);

    fclose(f);
    return 0;
}

int kolibri_rl_load_qtable(KolibriRLContext *ctx, const char *filepath) {
    if (!ctx || !filepath)
        return -1;

    FILE *f = fopen(filepath, "rb");
    if (!f)
        return -1;

    /* Читаем заголовок */
    uint32_t magic;
    fread(&magic, sizeof(magic), 1, f);

    if (magic != 0x4B524C51) {
        fclose(f);
        return -1; /* Invalid magic */
    }

    fread(&ctx->num_states, sizeof(ctx->num_states), 1, f);

    if (ctx->num_states > KRL_MAX_STATES) {
        fclose(f);
        return -1;
    }

    /* Читаем Q-Table */
    fread(ctx->q_table, sizeof(KolibriQEntry), ctx->num_states, f);

    fclose(f);
    return 0;
}

/* ============================================================================
 * УТИЛИТЫ
 * ============================================================================ */

const char *kolibri_rl_action_name(KolibriRLAction action) {
    static const char *names[] = {"USE_KNOWLEDGE_BASE", "USE_REASONING", "USE_MATH_SOLVER",    "USE_FORMULA_POOL",
                                  "USE_WORLD_MODEL",    "USE_ANALOGY",   "USE_COUNTERFACTUAL", "FALLBACK_CHAT"};

    if (action < 0 || action >= KRL_ACTION_COUNT)
        return "INVALID";
    return names[action];
}

const char *kolibri_rl_action_desc(KolibriRLAction action) {
    static const char *descs[] = {"Ответ из базы знаний",           "Логический вывод",
                                  "Решение математической задачи",  "Использование формул",
                                  "Генерация текста (world model)", "Рассуждение по аналогии",
                                  "Counterfactual анализ",          "Fallback на внешний LLM"};

    if (action < 0 || action >= KRL_ACTION_COUNT)
        return "Неверное действие";
    return descs[action];
}

uint32_t kolibri_rl_hash_state(const KolibriRLState *state) {
    /* Простой hash FNV-1a */
    uint32_t hash = 2166136261u;

    hash ^= (uint32_t)state->intent;
    hash *= 16777619;

    hash ^= (uint32_t)(state->complexity * 1000);
    hash *= 16777619;

    hash ^= (uint32_t)(state->requires_reasoning << 16);
    hash *= 16777619;

    hash ^= (uint32_t)(state->requires_knowledge << 17);
    hash *= 16777619;

    /* Hash domain */
    for (size_t i = 0; i < strlen(state->domain); i++) {
        hash ^= (uint32_t)state->domain[i];
        hash *= 16777619;
    }

    return hash;
}

int kolibri_rl_has_state(const KolibriRLContext *ctx, const KolibriRLState *state) {
    uint32_t hash = kolibri_rl_hash_state(state);

    for (int i = 0; i < ctx->num_states; i++) {
        if (ctx->q_table[i].state.hash == hash && ctx->q_table[i].state.intent == state->intent) {
            return 1;
        }
    }

    return 0;
}
