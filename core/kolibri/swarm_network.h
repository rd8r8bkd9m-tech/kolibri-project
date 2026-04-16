/*
 * swarm_network.h
 *
 * Сетевой слой для распределённого обучения роя (Swarm Learning)
 *
 * Архитектура:
 *   1. Каждый узел — отдельный процесс, слушающий TCP порт
 *   2. Узлы обмениваются лучшими формулами по сети
 *   3. Полученные формулы верифицируются и оцениваются локально
 *   4. Лучшие формулы интегрируются через crossover
 *   5. Provenance tracking — откуда получена каждая формула
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_SWARM_NETWORK_H
#define KOLIBRI_SWARM_NETWORK_H

#include "kolibri/net.h"
#include "kolibri/evolutionary_trainer.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНСТАНТЫ
 * ============================================================================ */

/** Максимальное количество peer'ов */
#define KSN_MAX_PEERS 16

/** Максимальное количество формул для обмена за раунд */
#define KSN_MAX_EXCHANGE_FORMULAS 16

/* ============================================================================
 * ТИПЫ ДАННЫХ
 * ============================================================================ */

/** Peer (соседний узел) */
typedef struct {
    char host[64];
    uint16_t port;
} KolibriSwarmPeer;

/** Конфигурация сетевого swarm learning */
typedef struct {
    uint32_t node_id;                 /* ID этого узла */
    uint16_t listen_port;             /* Порт для входящих соединений */
    
    const KolibriSwarmPeer *peers;    /* Список peer'ов */
    int num_peers;                    /* Количество peer'ов */
    
    int local_population_size;        /* Размер локальной популяции */
    int num_formulas_to_exchange;     /* Формул для обмена за раунд */
    
    /* Верификация */
    int verify_received_formulas;     /* Проверять полученные формулы? */
    double fitness_improvement_threshold; /* Минимальное улучшение для принятия */
    int crossover_with_external;      /* Делать crossover с внешними формулами? */
    
    /* Logging */
    int verbose;                      /* 0=тихо, 1=ошибки, 2=info, 3=debug */
    int log_every_n_syncs;
    
    /* RNG */
    uint64_t rng_seed;
} KolibriSwarmNetConfig;

/** Статистика приёма формул */
typedef struct {
    int received;
    int accepted;
    int rejected;
    double elapsed_ms;
} KolibriSwarmNetStats;

/** Статистика sync раунда */
typedef struct {
    int formulas_sent;
    int formulas_received;
    int formulas_accepted;
    double elapsed_ms;
} KolibriSwarmNetSyncStats;

/** Полная статистика узла */
typedef struct {
    uint32_t node_id;
    int sync_rounds;
    int local_generations;
    double local_best_fitness;
    double local_avg_fitness;
    double global_best_fitness;
    int formulas_sent;
    int formulas_received;
    int formulas_accepted;
    int send_errors;
    double local_train_time_ms;
    double total_training_time_ms;
    int num_peers;
} KolibriSwarmNetFullStats;

/** Сетевой swarm learner */
typedef struct {
    KolibriSwarmNetConfig config;
    uint32_t node_id;
    
    /* Локальный trainer */
    KolibriEvoTrainer trainer;
    
    /* Сеть */
    KolibriNetListener listener;
    int listener_ready;
    
    KolibriSwarmPeer peers[KSN_MAX_PEERS];
    int num_peers;
    
    /* Статистика */
    int sync_rounds;
    int local_generations;
    double local_best_fitness;
    double local_avg_fitness;
    double global_best_fitness;
    
    int formulas_sent;
    int formulas_received;
    int formulas_accepted;
    int send_errors;
    
    double local_train_time_ms;
    double total_training_time_ms;
    
    /* Timing */
    double start_time;
} KolibriSwarmNetwork;

/* Callback для прогресса */
typedef void (*KolibriSwarmNetProgressCallback)(
    uint32_t node_id, int current_round, int total_rounds,
    double local_best, double global_best,
    int sent, int received, void *user_data
);

/* ============================================================================
 * API: ИНИЦИАЛИЗАЦИЯ
 * ============================================================================ */

/**
 * Инициализировать сетевой swarm learner
 *
 * @param net     Learner (output)
 * @param config  Конфигурация
 * @return 0 на успех
 */
int kolibri_swarm_net_init(KolibriSwarmNetwork *net,
                           const KolibriSwarmNetConfig *config);

/**
 * Освободить ресурсы
 */
void kolibri_swarm_net_free(KolibriSwarmNetwork *net);

/* ============================================================================
 * API: LOKALNOE ОБУЧЕНИЕ
 * ============================================================================ */

/**
 * Локальное обучение (без сети)
 *
 * @param net           Learner
 * @param generations   Количество поколений
 * @param fitness_func  Функция fitness
 * @param fitness_data  Данные для fitness
 * @return 0 на успех
 */
int kolibri_swarm_net_train_local(KolibriSwarmNetwork *net,
                                  int generations,
                                  KolibriEvoFitnessFunc fitness_func,
                                  void *fitness_data);

/* ============================================================================
 * API: СЕТЕВОЙ ОБМЕН
 * ============================================================================ */

/**
 * Отправить лучшие формулы peer'ам
 *
 * @param net           Learner
 * @param num_to_send   Количество формул для отправки
 * @return Количество отправленных формул
 */
int kolibri_swarm_net_send_best_formulas(KolibriSwarmNetwork *net,
                                         int num_to_send);

/**
 * Принять и интегрировать формулы от peer'ов
 *
 * @param net           Learner
 * @param fitness_func  Функция для оценки формул
 * @param fitness_data  Данные для fitness
 * @param stats_out     Статистика (опционально)
 * @return Количество полученных формул
 */
int kolibri_swarm_net_receive_formulas(KolibriSwarmNetwork *net,
                                       KolibriEvoFitnessFunc fitness_func,
                                       void *fitness_data,
                                       KolibriSwarmNetStats *stats_out);

/**
 * Полный раунд синхронизации: отправка + приём
 *
 * @param net           Learner
 * @param fitness_func  Функция fitness
 * @param fitness_data  Данные для fitness
 * @param sync_stats    Статистика sync (опционально)
 * @return 0 на успех
 */
int kolibri_swarm_net_sync_round(KolibriSwarmNetwork *net,
                                 KolibriEvoFitnessFunc fitness_func,
                                 void *fitness_data,
                                 KolibriSwarmNetSyncStats *sync_stats);

/* ============================================================================
 * API: FULL TRAINING
 * ============================================================================ */

/**
 * Полный цикл: локальное обучение + сетевая синхронизация
 *
 * @param net               Learner
 * @param sync_rounds       Количество раундов синхронизации
 * @param generations_per_round  Локальных поколений между синхронизациями
 * @param fitness_func      Функция fitness
 * @param fitness_data      Данные для fitness
 * @param callback          Callback прогресса (опционально)
 * @param user_data         Данные для callback
 * @return 0 на успех
 */
int kolibri_swarm_net_train(KolibriSwarmNetwork *net,
                            int sync_rounds,
                            int generations_per_round,
                            KolibriEvoFitnessFunc fitness_func,
                            void *fitness_data,
                            KolibriSwarmNetProgressCallback callback,
                            void *user_data);

/* ============================================================================
 * API: СТАТИСТИКА
 * ============================================================================ */

/**
 * Распечатать статистику
 */
void kolibri_swarm_net_print_stats(const KolibriSwarmNetwork *net);

/**
 * Получить полную статистику
 */
void kolibri_swarm_net_get_stats(const KolibriSwarmNetwork *net,
                                 KolibriSwarmNetFullStats *out);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_SWARM_NETWORK_H */
