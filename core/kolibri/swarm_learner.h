/*
 * swarm_learner.h
 *
 * Распределённое обучение роя (Swarm Learning) для Kolibri
 *
 * Архитектура:
 *   1. Каждый узел локально обучает свою популяцию формул
 *   2. Периодически узлы обмениваются лучшими формулами
 *   3. Полученные формулы тестируются локально
 *   4. Лучшие формулы интегрируются через crossover
 *   5. Provenance сохраняется в геном
 *
 * Протокол обмена:
 *   - Узел-инициатор: SEND_FORMULAS (top-K формул)
 *   - Узел-получатель: RECEIVE_FORMULAS → EVALUATE → MERGE
 *   - Ответ: MERGE_REPORT (статистика интеграции)
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_SWARM_LEARNER_H
#define KOLIBRI_SWARM_LEARNER_H

#include "kolibri/evolutionary_trainer.h"
#include "kolibri/genome.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНСТАНТЫ
 * ============================================================================ */

/** Максимальное количество узлов в рое */
#define KSL_MAX_NODES 16

/** Максимальное количество формул для обмена */
#define KSL_MAX_EXCHANGE_FORMULAS 16

/** Максимальный размер формулы при обмене */
#define KSL_FORMULA_DATA_SIZE 4096

/** Максимальный размер популяции (для сортировки) */
#define KSL_MAX_POPULATION 256

/** Интервал синхронизации по умолчанию (мс) */
#define KSL_DEFAULT_SYNC_INTERVAL_MS 5000

/* ============================================================================
 * ТИПЫ ДАННЫХ
 * ============================================================================ */

/** Статус узла роя */
typedef enum {
    KSL_NODE_IDLE = 0,
    KSL_NODE_TRAINING,
    KSL_NODE_SYNCING,
    KSL_NODE_EVALUATING,
    KSL_NODE_ERROR
} KolibriSwarmNodeStatus;

/** Формула для обмена */
typedef struct {
    uint8_t digits[KSL_FORMULA_DATA_SIZE];  /* Цифры формулы */
    int formula_size;                       /* Актуальный размер */
    double fitness;                         /* Fitness на отправителе */
    int source_node_id;                     /* ID исходного узла */
    int generation;                         /* Поколение создания */
    uint64_t timestamp;                     /* Время создания */
    uint32_t checksum;                      /* Контрольная сумма */
} KolibriSwarmFormula;

/** Пакет для обмена формулами */
typedef struct {
    int source_node_id;                 /* ID отправителя */
    int target_node_id;                 /* ID получателя (-1 = broadcast) */
    KolibriSwarmFormula formulas[KSL_MAX_EXCHANGE_FORMULAS];
    int num_formulas;
    uint64_t timestamp;
} KolibriSwarmExchangePacket;

/** Отчёт об интеграции */
typedef struct {
    int received_count;         /* Количество полученных формул */
    int accepted_count;         /* Количество принятых формул */
    int rejected_count;         /* Количество отклонённых */
    double best_received_fitness; /* Лучший fitness среди полученных */
    double best_local_fitness;    /* Лучший fitness локальный */
    double improvement;           /* Улучшение после интеграции */
    int crossover_count;         /* Количество crossover операций */
    double elapsed_ms;           /* Время обработки */
} KolibriSwarmMergeReport;

/** Статистика узла */
typedef struct {
    int node_id;
    KolibriSwarmNodeStatus status;

    /* Локальная эволюция */
    int local_generation;
    double local_best_fitness;
    double local_avg_fitness;

    /* Синхронизация */
    int sync_count;             /* Количество синхронизаций */
    int formulas_sent;          /* Отправлено формул */
    int formulas_received;      /* Получено формул */
    int formulas_accepted;      /* Принято формул */

    /* Качество */
    double isolated_best_fitness;   /* Лучший fitness без swarm */
    double swarm_best_fitness;      /* Лучший fitness с swarm */
    double swarm_improvement_pct;   /* Процент улучшения от swarm */

    /* Timing */
    double training_time_ms;
    double sync_time_ms;
    double total_time_ms;
} KolibriSwarmNodeStats;

/** Конфигурация swarm learning */
typedef struct {
    int num_nodes;                  /* Количество узлов */
    int local_population_size;      /* Размер локальной популяции */
    int local_generations_per_sync; /* Локальных поколений до синхронизации */
    int num_formulas_to_exchange;   /* Формул для обмена */
    int sync_interval_ms;           /* Интервал синхронизации (мс) */

    /* Верификация */
    int verify_received_formulas;  /* Проверять полученные формулы? */
    double fitness_improvement_threshold; /* Минимальное улучшение для принятия */

    /* Provenance */
    KolibriGenome *genome;         /* Геном для provenance tracking */

    /* Logging */
    int verbose;
    int log_every_n_syncs;
} KolibriSwarmConfig;

/** Состояние swarm learner */
typedef struct {
    KolibriSwarmConfig config;

    /* Локальные эволюционные тренеры для каждого узла (heap-allocated) */
    KolibriEvoTrainer *nodes[KSL_MAX_NODES];
    KolibriSwarmNodeStats node_stats[KSL_MAX_NODES];

    /* Глобальная статистика */
    int total_sync_rounds;
    double global_best_fitness;
    double global_avg_fitness;
    double swarm_diversity;         /* Разнообразие между узлами */

    /* Буфер для обмена */
    KolibriSwarmExchangePacket exchange_buffer;
    KolibriSwarmMergeReport merge_report;

    /* RNG */
    uint64_t rng_state;

    /* Timing */
    double start_time;
} KolibriSwarmLearner;

/* Callback для прогресса синхронизации */
typedef void (*KolibriSwarmSyncCallback)(
    int sync_round, int node_id, const KolibriSwarmMergeReport *report, void *user_data
);

/* ============================================================================
 * API: ИНИЦИАЛИЗАЦИЯ
 * ============================================================================ */

/**
 * Инициализировать swarm learner
 *
 * @param learner   Learner (output)
 * @param config    Конфигурация
 * @param seed      RNG seed
 * @return 0 на успех
 */
int kolibri_swarm_init(KolibriSwarmLearner *learner,
                       const KolibriSwarmConfig *config,
                       uint64_t seed);

/**
 * Освободить ресурсы learner
 */
void kolibri_swarm_free(KolibriSwarmLearner *learner);

/* ============================================================================
 * API: LOKALNOE ОБУЧЕНИЕ
 * ============================================================================ */

/**
 * Запустить локальное обучение на всех узлах
 *
 * @param learner       Learner
 * @param generations   Локальных поколений
 * @param fitness_func  Функция fitness
 * @param fitness_data  Данные для fitness
 * @return 0 на успех
 */
int kolibri_swarm_train_local(KolibriSwarmLearner *learner,
                              int generations,
                              KolibriEvoFitnessFunc fitness_func,
                              void *fitness_data);

/**
 * Запустить локальное обучение на одном узле
 *
 * @param learner       Learner
 * @param node_id       ID узла
 * @param generations   Поколений
 * @param fitness_func  Функция fitness
 * @param fitness_data  Данные для fitness
 * @return 0 на успех
 */
int kolibri_swarm_train_node(KolibriSwarmLearner *learner,
                             int node_id,
                             int generations,
                             KolibriEvoFitnessFunc fitness_func,
                             void *fitness_data);

/* ============================================================================
 * API: SYNC / EXCHANGE
 * ============================================================================ */

/**
 * Подготовить пакет формул для отправки (top-K лучших)
 *
 * @param learner       Learner
 * @param node_id       ID узла-отправителя
 * @param packet        Пакет (output)
 * @return Количество формул в пакете
 */
int kolibri_swarm_prepare_exchange_packet(KolibriSwarmLearner *learner,
                                          int node_id,
                                          KolibriSwarmExchangePacket *packet);

/**
 * Получить пакет формул и интегрировать в локальную популяцию
 *
 * @param learner       Learner
 * @param target_node_id  ID узла-получателя
 * @param packet        Полученный пакет
 * @param fitness_func  Функция для оценки формул
 * @param fitness_data  Данные для fitness
 * @param report        Отчёт об интеграции (output)
 * @return 0 на успех
 */
int kolibri_swarm_receive_and_merge(KolibriSwarmLearner *learner,
                                    int target_node_id,
                                    const KolibriSwarmExchangePacket *packet,
                                    KolibriEvoFitnessFunc fitness_func,
                                    void *fitness_data,
                                    KolibriSwarmMergeReport *report);

/**
 * Полный раунд синхронизации: все узлы обмениваются формулами
 *
 * @param learner       Learner
 * @param fitness_func  Функция fitness
 * @param fitness_data  Данные для fitness
 * @param callback      Callback (опционально)
 * @param user_data     Данные для callback
 * @return 0 на успех
 */
int kolibri_swarm_sync_round(KolibriSwarmLearner *learner,
                             KolibriEvoFitnessFunc fitness_func,
                             void *fitness_data,
                             KolibriSwarmSyncCallback callback,
                             void *user_data);

/* ============================================================================
 * API: FULL SWARM TRAINING
 * ============================================================================ */

/**
 * Полный цикл swarm learning: локальное обучение + синхронизация
 *
 * @param learner           Learner
 * @param sync_rounds       Количество раундов синхронизации
 * @param generations_per_round  Локальных поколений между синхронизациями
 * @param fitness_func      Функция fitness
 * @param fitness_data      Данные для fitness
 * @param callback          Callback (опционально)
 * @param user_data         Данные для callback
 * @return 0 на успех
 */
int kolibri_swarm_train(KolibriSwarmLearner *learner,
                        int sync_rounds,
                        int generations_per_round,
                        KolibriEvoFitnessFunc fitness_func,
                        void *fitness_data,
                        KolibriSwarmSyncCallback callback,
                        void *user_data);

/* ============================================================================
 * API: СТАТИСТИКА
 * ============================================================================ */

/**
 * Распечатать общую статистику swarm
 */
void kolibri_swarm_print_stats(const KolibriSwarmLearner *learner);

/**
 * Распечатать статистику конкретного узла
 */
void kolibri_swarm_print_node_stats(const KolibriSwarmLearner *learner, int node_id);

/**
 * Сохранить статистику в файл
 */
int kolibri_swarm_save_stats(const KolibriSwarmLearner *learner, const char *filepath);

/**
 * Сравнить swarm learning с isolated learning
 *
 * @param learner       Learner
 * @param output        Буфер для вывода
 * @param size          Размер буфера
 */
void kolibri_swarm_compare_with_isolated(const KolibriSwarmLearner *learner,
                                         char *output, size_t size);

/**
 * Утилиты (публичные для тестов)
 */
uint32_t kolibri_swarm_checksum(const uint8_t *data, int size);
double kolibri_swarm_time_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_SWARM_LEARNER_H */
