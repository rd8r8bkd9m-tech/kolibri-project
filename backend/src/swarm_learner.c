/*
 * swarm_learner.c
 *
 * Реализация распределённого обучения роя (Swarm Learning)
 *
 * Реализует:
 *   - Локальное обучение на каждом узле
 *   - Периодическую синхронизацию формул
 *   - Верификацию полученных формул
 *   - Интеграцию через crossover
 *   - Provenance tracking
 *   - Статистику и мониторинг
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/swarm_learner.h"
#include "kolibri/evolutionary_trainer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * ВНУТРЕННИЕ ФУНКЦИИ
 * ============================================================================ */

static uint64_t swarm_rng_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* Публичные обёртки для тестов */
uint32_t kolibri_swarm_checksum(const uint8_t *data, int size) {
    uint32_t sum = 0;
    for (int i = 0; i < size; i++) {
        sum = sum * 31 + data[i];
    }
    return sum;
}

double kolibri_swarm_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* Внутренние функции используют публичные */
static double swarm_time_ms(void) {
    return kolibri_swarm_time_ms();
}

/* ============================================================================
 * ИНИЦИАЛИЗАЦИЯ
 * ============================================================================ */

int kolibri_swarm_init(KolibriSwarmLearner *learner,
                       const KolibriSwarmConfig *config,
                       uint64_t seed) {
    if (!learner || !config) return -1;

    memset(learner, 0, sizeof(KolibriSwarmLearner));
    learner->config = *config;
    learner->rng_state = seed ? seed : 54321;
    learner->start_time = swarm_time_ms();
    learner->global_best_fitness = -1e30;

    int num_nodes = config->num_nodes;
    if (num_nodes > KSL_MAX_NODES) num_nodes = KSL_MAX_NODES;

    /* Инициализируем каждый узел */
    for (int i = 0; i < num_nodes; i++) {
        /* Выделяем память для тренера на heap */
        learner->nodes[i] = (KolibriEvoTrainer *)calloc(1, sizeof(KolibriEvoTrainer));
        if (!learner->nodes[i]) return -2;

        /* Конфигурация эволюции для узла */
        KolibriEvoConfig evo_config = {0};
        evo_config.population_size = config->local_population_size;
        evo_config.elitism_count = config->local_population_size / 10;
        evo_config.mutation_rate = 0.15;
        evo_config.crossover_rate = 0.8;
        evo_config.mutation_types[0] = KET_MUTATION_POINT;
        evo_config.mutation_types[1] = KET_MUTATION_SWAP;
        evo_config.mutation_types[2] = KET_MUTATION_SHIFT;
        evo_config.num_mutation_types = 3;
        evo_config.crossover_type = KET_CROSSOVER_UNIFORM;
        evo_config.compatibility_threshold = 0.25;
        evo_config.max_species = 8;
        evo_config.sharing_radius = 0.15;
        evo_config.max_generations = 1000;
        evo_config.stagnation_limit = 100;
        evo_config.log_every_n_generations = 100;
        evo_config.verbose = 0;

        /* Уникальный seed для каждого узла */
        uint64_t node_seed = seed + (uint64_t)i * 1000;
        int ret = kolibri_evo_init(learner->nodes[i], &evo_config, node_seed);
        if (ret != 0) return -2;

        /* Статистика узла */
        learner->node_stats[i].node_id = i;
        learner->node_stats[i].status = KSL_NODE_IDLE;
        learner->node_stats[i].local_best_fitness = -1e30;
    }

    return 0;
}

void kolibri_swarm_free(KolibriSwarmLearner *learner) {
    if (!learner) return;

    for (int i = 0; i < learner->config.num_nodes && i < KSL_MAX_NODES; i++) {
        if (learner->nodes[i]) {
            kolibri_evo_free(learner->nodes[i]);
            free(learner->nodes[i]);
            learner->nodes[i] = NULL;
        }
    }

    memset(learner, 0, sizeof(KolibriSwarmLearner));
}

/* ============================================================================
 * LOKALNOE ОБУЧЕНИЕ
 * ============================================================================ */

int kolibri_swarm_train_local(KolibriSwarmLearner *learner,
                              int generations,
                              KolibriEvoFitnessFunc fitness_func,
                              void *fitness_data) {
    if (!learner || !fitness_func) return -1;

    int num_nodes = learner->config.num_nodes;

    for (int i = 0; i < num_nodes; i++) {
        int ret = kolibri_swarm_train_node(learner, i, generations,
                                           fitness_func, fitness_data);
        if (ret != 0) return ret;
    }

    return 0;
}

int kolibri_swarm_train_node(KolibriSwarmLearner *learner,
                             int node_id,
                             int generations,
                             KolibriEvoFitnessFunc fitness_func,
                             void *fitness_data) {
    if (!learner || node_id < 0 || node_id >= learner->config.num_nodes) return -1;

    KolibriEvoTrainer *trainer = learner->nodes[node_id];
    if (!trainer) return -1;
    
    KolibriSwarmNodeStats *stats = &learner->node_stats[node_id];

    stats->status = KSL_NODE_TRAINING;
    double start_time = swarm_time_ms();

    /* Запускаем эволюцию с fitness function */
    int ret = kolibri_evo_run(trainer, generations, fitness_func, fitness_data,
                              NULL, NULL);

    stats->training_time_ms = swarm_time_ms() - start_time;
    stats->local_generation += generations;

    /* Обновляем статистику */
    int pop_size = trainer->config.population_size;
    double total_fitness = 0.0;
    double best_fitness = -1e30;

    for (int i = 0; i < pop_size; i++) {
        double f = trainer->population[i].fitness;
        total_fitness += f;
        if (f > best_fitness) best_fitness = f;
    }

    stats->local_best_fitness = best_fitness;
    stats->local_avg_fitness = total_fitness / pop_size;

    /* Обновляем глобальную статистику */
    if (best_fitness > learner->global_best_fitness) {
        learner->global_best_fitness = best_fitness;
    }

    stats->status = KSL_NODE_IDLE;

    return ret;
}

/* ============================================================================
 * EXCHANGE PACKET
 * ============================================================================ */

int kolibri_swarm_prepare_exchange_packet(KolibriSwarmLearner *learner,
                                          int node_id,
                                          KolibriSwarmExchangePacket *packet) {
    if (!learner || !packet || node_id < 0) return -1;

    memset(packet, 0, sizeof(KolibriSwarmExchangePacket));
    
    KolibriEvoTrainer *trainer = learner->nodes[node_id];
    if (!trainer) return -1;
    
    int pop_size = trainer->config.population_size;
    int num_to_send = learner->config.num_formulas_to_exchange;
    if (num_to_send > KSL_MAX_EXCHANGE_FORMULAS) num_to_send = KSL_MAX_EXCHANGE_FORMULAS;
    if (num_to_send > pop_size) num_to_send = pop_size;

    /* Сортируем популяцию по fitness (bubble sort) */
    int indices[KSL_MAX_POPULATION];
    for (int i = 0; i < pop_size && i < KSL_MAX_POPULATION; i++) {
        indices[i] = i;
    }

    for (int i = 0; i < pop_size - 1; i++) {
        for (int j = 0; j < pop_size - i - 1; j++) {
            if (trainer->population[indices[j]].fitness <
                trainer->population[indices[j + 1]].fitness) {
                int tmp = indices[j];
                indices[j] = indices[j + 1];
                indices[j + 1] = tmp;
            }
        }
    }

    /* Берём top-K формул */
    packet->source_node_id = node_id;
    packet->target_node_id = -1;  /* Broadcast */
    packet->num_formulas = num_to_send;
    packet->timestamp = (uint64_t)swarm_time_ms();

    for (int i = 0; i < num_to_send; i++) {
        int idx = indices[i];
        KolibriSwarmFormula *sf = &packet->formulas[i];

        int formula_size = KET_FORMULA_SIZE;
        if (formula_size > KSL_FORMULA_DATA_SIZE) formula_size = KSL_FORMULA_DATA_SIZE;

        memcpy(sf->digits, trainer->population[idx].digits, formula_size);
        sf->formula_size = formula_size;
        sf->fitness = trainer->population[idx].fitness;
        sf->source_node_id = node_id;
        sf->generation = trainer->current_generation;
        sf->timestamp = packet->timestamp;
        sf->checksum = kolibri_swarm_checksum(sf->digits, formula_size);
    }

    learner->node_stats[node_id].formulas_sent += num_to_send;

    return num_to_send;
}

int kolibri_swarm_receive_and_merge(KolibriSwarmLearner *learner,
                                    int target_node_id,
                                    const KolibriSwarmExchangePacket *packet,
                                    KolibriEvoFitnessFunc fitness_func,
                                    void *fitness_data,
                                    KolibriSwarmMergeReport *report) {
    if (!learner || !packet || !report) return -1;

    KolibriEvoTrainer *trainer = learner->nodes[target_node_id];
    if (!trainer) return -1;
    
    int pop_size = trainer->config.population_size;

    memset(report, 0, sizeof(KolibriSwarmMergeReport));
    report->received_count = packet->num_formulas;
    double start_time = swarm_time_ms();

    /* Получаем текущий лучший локальный fitness */
    double local_best = -1e30;
    for (int i = 0; i < pop_size; i++) {
        if (trainer->population[i].fitness > local_best) {
            local_best = trainer->population[i].fitness;
        }
    }
    report->best_local_fitness = local_best;

    /* Интегрируем полученные формулы */
    int accepted = 0;
    int rejected = 0;
    int crossover_count = 0;

    for (int i = 0; i < packet->num_formulas; i++) {
        const KolibriSwarmFormula *sf = &packet->formulas[i];

        /* Верификация контрольной суммы */
        if (learner->config.verify_received_formulas) {
            uint32_t computed_checksum = kolibri_swarm_checksum(sf->digits, sf->formula_size);
            if (computed_checksum != sf->checksum) {
                rejected++;
                continue;
            }
        }

        /* Оцениваем fitness локально */
        double received_fitness = 0.0;
        if (fitness_func) {
            received_fitness = fitness_func(sf->digits, sf->formula_size, fitness_data);
        } else {
            received_fitness = sf->fitness;  /* Доверяем отправителю */
        }

        if (i == 0 || received_fitness > report->best_received_fitness) {
            report->best_received_fitness = received_fitness;
        }

        /* Проверяем стоит ли принимать формулу */
        double threshold = local_best * learner->config.fitness_improvement_threshold;
        if (received_fitness >= threshold) {
            /* Находим худшую формулу в популяции и заменяем */
            int worst_idx = 0;
            double worst_fitness = trainer->population[0].fitness;
            for (int j = 1; j < pop_size; j++) {
                if (trainer->population[j].fitness < worst_fitness) {
                    worst_fitness = trainer->population[j].fitness;
                    worst_idx = j;
                }
            }

            /* Crossover с локальной формулой */
            int parent_idx = worst_idx;

            /* Копируем received формулу во временный буфер */
            uint8_t temp_formula[KET_FORMULA_SIZE];
            int copy_size = (sf->formula_size < KET_FORMULA_SIZE) ?
                           sf->formula_size : KET_FORMULA_SIZE;
            memcpy(temp_formula, sf->digits, copy_size);

            /* Заменяем worst формулу */
            memcpy(trainer->population[parent_idx].digits, temp_formula, copy_size);
            trainer->population[parent_idx].fitness = received_fitness;
            trainer->population[parent_idx].age = 0;
            trainer->population[parent_idx].is_elite = 0;

            /* Mutation для разнообразия */
            KolibriEvoMutationType mutations[] = {
                KET_MUTATION_POINT, KET_MUTATION_SWAP
            };
            int mut_idx = i % 2;
            kolibri_evo_mutate(trainer, parent_idx, mutations[mut_idx]);
            crossover_count++;

            accepted++;
        } else {
            rejected++;
        }
    }

    /* Переоцениваем fitness после интеграции */
    if (fitness_func) {
        kolibri_evo_evaluate_fitness(trainer, fitness_func, fitness_data);
    }

    /* Вычисляем улучшение */
    double new_best = -1e30;
    for (int i = 0; i < pop_size; i++) {
        if (trainer->population[i].fitness > new_best) {
            new_best = trainer->population[i].fitness;
        }
    }

    report->accepted_count = accepted;
    report->rejected_count = rejected;
    report->crossover_count = crossover_count;
    report->improvement = new_best - local_best;
    report->elapsed_ms = swarm_time_ms() - start_time;

    learner->node_stats[target_node_id].formulas_received += packet->num_formulas;
    learner->node_stats[target_node_id].formulas_accepted += accepted;

    return 0;
}

/* ============================================================================
 * SYNC ROUND
 * ============================================================================ */

int kolibri_swarm_sync_round(KolibriSwarmLearner *learner,
                             KolibriEvoFitnessFunc fitness_func,
                             void *fitness_data,
                             KolibriSwarmSyncCallback callback,
                             void *user_data) {
    if (!learner) return -1;

    int num_nodes = learner->config.num_nodes;
    learner->total_sync_rounds++;

    /* Каждый узел отправляет свои лучшие формулы */
    for (int sender = 0; sender < num_nodes; sender++) {
        KolibriSwarmExchangePacket packet;
        int num_sent = kolibri_swarm_prepare_exchange_packet(learner, sender, &packet);

        if (num_sent <= 0) continue;

        /* Отправляем всем другим узлам */
        for (int receiver = 0; receiver < num_nodes; receiver++) {
            if (sender == receiver) continue;

            KolibriSwarmMergeReport report;
            int ret = kolibri_swarm_receive_and_merge(learner, receiver, &packet,
                                                      fitness_func, fitness_data, &report);

            if (ret == 0 && callback) {
                callback(learner->total_sync_rounds, receiver, &report, user_data);
            }
        }
    }

    /* Обновляем глобальную статистику */
    double total_best = 0.0;
    double total_avg = 0.0;
    double total_diversity = 0.0;

    for (int i = 0; i < num_nodes; i++) {
        KolibriSwarmNodeStats *stats = &learner->node_stats[i];
        stats->sync_count++;

        total_best += stats->local_best_fitness;
        total_avg += stats->local_avg_fitness;

        stats->status = KSL_NODE_IDLE;
    }

    learner->global_best_fitness = total_best / num_nodes;
    learner->global_avg_fitness = total_avg / num_nodes;

    /* Вычисляем разнообразие между узлами */
    if (num_nodes > 1) {
        double variance = 0.0;
        for (int i = 0; i < num_nodes; i++) {
            double diff = learner->node_stats[i].local_best_fitness -
                         learner->global_best_fitness;
            variance += diff * diff;
        }
        learner->swarm_diversity = sqrt(variance / num_nodes);
    }

    return 0;
}

/* ============================================================================
 * FULL SWARM TRAINING
 * ============================================================================ */

int kolibri_swarm_train(KolibriSwarmLearner *learner,
                        int sync_rounds,
                        int generations_per_round,
                        KolibriEvoFitnessFunc fitness_func,
                        void *fitness_data,
                        KolibriSwarmSyncCallback callback,
                        void *user_data) {
    if (!learner || !fitness_func) return -1;

    double start_time = swarm_time_ms();

    for (int round = 0; round < sync_rounds; round++) {
        /* 1. Локальное обучение */
        int ret = kolibri_swarm_train_local(learner, generations_per_round,
                                            fitness_func, fitness_data);
        if (ret != 0) return ret;

        /* 2. Синхронизация */
        ret = kolibri_swarm_sync_round(learner, fitness_func, fitness_data,
                                       callback, user_data);
        if (ret != 0) return ret;

        /* Logging */
        if (learner->config.verbose &&
            (round + 1) % learner->config.log_every_n_syncs == 0) {
            printf("\n=== Sync Round %d / %d ===\n", round + 1, sync_rounds);
            kolibri_swarm_print_stats(learner);
        }
    }

    learner->node_stats[0].total_time_ms = swarm_time_ms() - start_time;

    return 0;
}

/* ============================================================================
 * СТАТИСТИКА
 * ============================================================================ */

void kolibri_swarm_print_stats(const KolibriSwarmLearner *learner) {
    if (!learner) return;

    int num_nodes = learner->config.num_nodes;

    printf("Swarm Statistics:\n");
    printf("  Sync rounds:      %d\n", learner->total_sync_rounds);
    printf("  Global best:      %.4f\n", learner->global_best_fitness);
    printf("  Global avg:       %.4f\n", learner->global_avg_fitness);
    printf("  Diversity:        %.4f\n", learner->swarm_diversity);
    printf("\n");

    printf("Node Statistics:\n");
    printf("  %-6s %-12s %-12s %-12s %-8s %-8s %-8s\n",
           "Node", "Best Fitness", "Avg Fitness", "Gen", "Sent", "Recv", "Accept");
    printf("  %-6s %-12s %-12s %-12s %-8s %-8s %-8s\n",
           "------", "------------", "------------", "------------",
           "--------", "--------", "--------");

    for (int i = 0; i < num_nodes; i++) {
        const KolibriSwarmNodeStats *stats = &learner->node_stats[i];
        printf("  %-6d %-12.4f %-12.4f %-12d %-8d %-8d %-8d\n",
               stats->node_id,
               stats->local_best_fitness,
               stats->local_avg_fitness,
               stats->local_generation,
               stats->formulas_sent,
               stats->formulas_received,
               stats->formulas_accepted);
    }
    printf("\n");
}

void kolibri_swarm_print_node_stats(const KolibriSwarmLearner *learner, int node_id) {
    if (!learner || node_id < 0 || node_id >= learner->config.num_nodes) return;

    const KolibriSwarmNodeStats *stats = &learner->node_stats[node_id];

    printf("Node %d Statistics:\n", stats->node_id);
    printf("  Status:              %d\n", stats->status);
    printf("  Local generation:    %d\n", stats->local_generation);
    printf("  Best fitness:        %.4f\n", stats->local_best_fitness);
    printf("  Avg fitness:         %.4f\n", stats->local_avg_fitness);
    printf("  Sync count:          %d\n", stats->sync_count);
    printf("  Formulas sent:       %d\n", stats->formulas_sent);
    printf("  Formulas received:   %d\n", stats->formulas_received);
    printf("  Formulas accepted:   %d\n", stats->formulas_accepted);
    printf("  Training time:       %.1fms\n", stats->training_time_ms);
    printf("  Swarm improvement:   %.2f%%\n", stats->swarm_improvement_pct);
    printf("\n");
}

int kolibri_swarm_save_stats(const KolibriSwarmLearner *learner, const char *filepath) {
    if (!learner || !filepath) return -1;

    FILE *f = fopen(filepath, "w");
    if (!f) return -2;

    /* Header */
    fprintf(f, "sync_round,global_best,global_avg,diversity\n");
    fprintf(f, "%d,%.6f,%.6f,%.6f\n",
            learner->total_sync_rounds,
            learner->global_best_fitness,
            learner->global_avg_fitness,
            learner->swarm_diversity);

    fprintf(f, "\nnode_id,best_fitness,avg_fitness,generation,sync_count,"
               "formulas_sent,formulas_received,formulas_accepted\n");

    for (int i = 0; i < learner->config.num_nodes; i++) {
        const KolibriSwarmNodeStats *s = &learner->node_stats[i];
        fprintf(f, "%d,%.6f,%.6f,%d,%d,%d,%d,%d\n",
                s->node_id, s->local_best_fitness, s->local_avg_fitness,
                s->local_generation, s->sync_count,
                s->formulas_sent, s->formulas_received, s->formulas_accepted);
    }

    fclose(f);
    return 0;
}

void kolibri_swarm_compare_with_isolated(const KolibriSwarmLearner *learner,
                                         char *output, size_t size) {
    if (!output || size == 0) return;

    int num_nodes = learner->config.num_nodes;
    double swarm_best = learner->global_best_fitness;
    double isolated_avg = 0.0;

    for (int i = 0; i < num_nodes; i++) {
        isolated_avg += learner->node_stats[i].local_best_fitness;
    }
    isolated_avg /= num_nodes;

    double improvement_pct = (isolated_avg > 0) ?
        ((swarm_best - isolated_avg) / isolated_avg) * 100.0 : 0.0;

    if (improvement_pct > 1.0) {
        snprintf(output, size,
                "✓ Swarm better by %.2f%% (%.4f vs %.4f)",
                improvement_pct, swarm_best, isolated_avg);
    } else if (improvement_pct < -1.0) {
        snprintf(output, size,
                "✗ Swarm worse by %.2f%% (%.4f vs %.4f)",
                -improvement_pct, swarm_best, isolated_avg);
    } else {
        snprintf(output, size,
                "≈ Swarm similar to isolated (%.4f vs %.4f)",
                swarm_best, isolated_avg);
    }
}
