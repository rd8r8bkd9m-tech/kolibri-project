/*
 * test_swarm_learner.c
 *
 * Тесты для Swarm Learning
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "kolibri/swarm_learner.h"
#include "kolibri/evolutionary_trainer.h"

/* ============================================================================
 * FITNESS FUNCTIONS
 * ============================================================================ */

/** Простая fitness функция: сумма цифр */
static double sum_fitness(const uint8_t *formula, int formula_size, void *data) {
    (void)data;
    double sum = 0.0;
    for (int i = 0; i < formula_size; i++) {
        sum += formula[i];
    }
    return sum / formula_size;
}

/* ============================================================================
 * ТЕСТЫ ИНИЦИАЛИЗАЦИИ
 * ============================================================================ */

void test_swarm_init(void) {
    printf("Testing swarm learner initialization...\n");

    KolibriSwarmConfig config = {0};
    config.num_nodes = 3;  /* Уменьшено для скорости */
    config.local_population_size = 15;  /* Уменьшено для скорости */
    config.local_generations_per_sync = 5;
    config.num_formulas_to_exchange = 2;
    config.sync_interval_ms = 5000;
    config.verify_received_formulas = 1;
    config.fitness_improvement_threshold = 0.9;
    config.genome = NULL;
    config.verbose = 0;
    config.log_every_n_syncs = 1;

    KolibriSwarmLearner learner;
    int ret = kolibri_swarm_init(&learner, &config, 42);

    assert(ret == 0);
    assert(learner.config.num_nodes == 3);
    assert(learner.total_sync_rounds == 0);

    printf("✓ Swarm learner initialization test passed\n\n");

    kolibri_swarm_free(&learner);
}

/* ============================================================================
 * ТЕСТЫ LOKALNOGO ОБУЧЕНИЯ
 * ============================================================================ */

void test_local_training(void) {
    printf("Testing local training on swarm nodes...\n");

    KolibriSwarmConfig config = {0};
    config.num_nodes = 3;
    config.local_population_size = 20;
    config.local_generations_per_sync = 5;
    config.num_formulas_to_exchange = 2;
    config.verify_received_formulas = 1;
    config.fitness_improvement_threshold = 0.9;
    config.verbose = 0;

    KolibriSwarmLearner learner;
    kolibri_swarm_init(&learner, &config, 42);

    /* Обучаем все узлы локально */
    int ret = kolibri_swarm_train_local(&learner, 10, sum_fitness, NULL);
    assert(ret == 0);

    /* Проверяем что все узлы обучились */
    for (int i = 0; i < 3; i++) {
        double best = learner.node_stats[i].local_best_fitness;
        double avg = learner.node_stats[i].local_avg_fitness;

        printf("  Node %d: best = %.4f, avg = %.4f\n", i, best, avg);
        assert(best > -1e29);
        assert(avg > -1e29);
    }

    printf("✓ Local training test passed\n\n");

    kolibri_swarm_free(&learner);
}

/* ============================================================================
 * ТЕСТЫ EXCHANGE PACKET
 * ============================================================================ */

void test_exchange_packet(void) {
    printf("Testing exchange packet preparation...\n");

    KolibriSwarmConfig config = {0};
    config.num_nodes = 3;
    config.local_population_size = 20;
    config.num_formulas_to_exchange = 3;
    config.verify_received_formulas = 1;
    config.fitness_improvement_threshold = 0.9;
    config.verbose = 0;

    KolibriSwarmLearner learner;
    kolibri_swarm_init(&learner, &config, 42);

    /* Обучаем узел 0 */
    kolibri_swarm_train_node(&learner, 0, 10, sum_fitness, NULL);

    /* Подготавливаем пакет */
    KolibriSwarmExchangePacket packet;
    int num_sent = kolibri_swarm_prepare_exchange_packet(&learner, 0, &packet);

    assert(num_sent == 3);
    assert(packet.source_node_id == 0);
    assert(packet.num_formulas == 3);

    /* Проверяем что формулы имеют корректные checksums */
    for (int i = 0; i < 3; i++) {
        uint32_t computed = kolibri_swarm_checksum(packet.formulas[i].digits,
                                          packet.formulas[i].formula_size);
        assert(computed == packet.formulas[i].checksum);
        assert(packet.formulas[i].source_node_id == 0);
        printf("  Formula %d: fitness = %.4f, size = %d\n",
               i, packet.formulas[i].fitness, packet.formulas[i].formula_size);
    }

    printf("✓ Exchange packet test passed\n\n");

    kolibri_swarm_free(&learner);
}

/* ============================================================================
 * ТЕСТЫ MERGE
 * ============================================================================ */

void test_receive_and_merge(void) {
    printf("Testing receive and merge...\n");

    KolibriSwarmConfig config = {0};
    config.num_nodes = 3;
    config.local_population_size = 20;
    config.num_formulas_to_exchange = 3;
    config.verify_received_formulas = 1;
    config.fitness_improvement_threshold = 0.8;
    config.verbose = 0;

    KolibriSwarmLearner learner;
    kolibri_swarm_init(&learner, &config, 42);

    /* Обучаем узел 0 */
    kolibri_swarm_train_node(&learner, 0, 20, sum_fitness, NULL);

    /* Обучаем узел 1 */
    kolibri_swarm_train_node(&learner, 1, 15, sum_fitness, NULL);

    /* Получаем лучший fitness узла 1 до merge */
    double fitness_before = learner.node_stats[1].local_best_fitness;

    /* Подготавливаем пакет от узла 0 */
    KolibriSwarmExchangePacket packet;
    kolibri_swarm_prepare_exchange_packet(&learner, 0, &packet);

    /* Merge в узел 1 */
    KolibriSwarmMergeReport report;
    int ret = kolibri_swarm_receive_and_merge(&learner, 1, &packet,
                                              sum_fitness, NULL, &report);

    assert(ret == 0);
    assert(report.received_count == 3);
    assert(report.accepted_count + report.rejected_count == 3);

    printf("  Merge report:\n");
    printf("    Received: %d\n", report.received_count);
    printf("    Accepted: %d\n", report.accepted_count);
    printf("    Rejected: %d\n", report.rejected_count);
    printf("    Best received: %.4f\n", report.best_received_fitness);
    printf("    Best local (before): %.4f\n", report.best_local_fitness);
    printf("    Improvement: %.4f\n", report.improvement);
    printf("    Crossovers: %d\n", report.crossover_count);

    printf("✓ Receive and merge test passed\n\n");

    kolibri_swarm_free(&learner);
}

/* ============================================================================
 * ТЕСТЫ SYNC ROUND
 * ============================================================================ */

void test_sync_round(void) {
    printf("Testing sync round...\n");

    KolibriSwarmConfig config = {0};
    config.num_nodes = 5;
    config.local_population_size = 25;
    config.num_formulas_to_exchange = 3;
    config.verify_received_formulas = 1;
    config.fitness_improvement_threshold = 0.85;
    config.verbose = 0;
    config.log_every_n_syncs = 1;

    KolibriSwarmLearner learner;
    kolibri_swarm_init(&learner, &config, 42);

    /* Обучаем все узлы */
    kolibri_swarm_train_local(&learner, 10, sum_fitness, NULL);

    double best_before = learner.global_best_fitness;
    double diversity_before = learner.swarm_diversity;

    printf("  Before sync: best = %.4f, diversity = %.4f\n",
           best_before, diversity_before);

    /* Sync round */
    int ret = kolibri_swarm_sync_round(&learner, sum_fitness, NULL, NULL, NULL);
    assert(ret == 0);

    printf("  After sync:  best = %.4f, diversity = %.4f\n",
           learner.global_best_fitness, learner.swarm_diversity);
    printf("  Total sync rounds: %d\n", learner.total_sync_rounds);

    printf("✓ Sync round test passed\n\n");

    kolibri_swarm_free(&learner);
}

/* ============================================================================
 * ТЕСТЫ FULL SWARM TRAINING
 * ============================================================================ */

static void swarm_progress_callback(int sync_round, int node_id,
                                    const KolibriSwarmMergeReport *report,
                                    void *user_data) {
    (void)node_id;
    (void)report;
    int *rounds = (int*)user_data;
    *rounds = sync_round;
}

void test_full_swarm_training(void) {
    printf("Testing full swarm training...\n");

    KolibriSwarmConfig config = {0};
    config.num_nodes = 5;
    config.local_population_size = 30;
    config.local_generations_per_sync = 5;
    config.num_formulas_to_exchange = 3;
    config.verify_received_formulas = 1;
    config.fitness_improvement_threshold = 0.85;
    config.verbose = 0;
    config.log_every_n_syncs = 2;

    KolibriSwarmLearner learner;
    kolibri_swarm_init(&learner, &config, 12345);

    int max_sync_reached = 0;

    /* Обучаем swarm */
    int ret = kolibri_swarm_train(&learner, 5, 10, sum_fitness, NULL,
                                 swarm_progress_callback, &max_sync_reached);
    assert(ret == 0);

    printf("\n  Final swarm statistics:\n");
    kolibri_swarm_print_stats(&learner);

    /* Проверяем что обучение прошло */
    assert(learner.total_sync_rounds == 5);
    assert(learner.global_best_fitness > -1e29);
    assert(max_sync_reached >= 5);

    /* Сравнение с isolated learning */
    char comparison[512];
    kolibri_swarm_compare_with_isolated(&learner, comparison, sizeof(comparison));
    printf("  Comparison: %s\n", comparison);

    /* Сохраняем статистику */
    kolibri_swarm_save_stats(&learner, "/tmp/swarm_stats.csv");
    printf("  Stats saved to /tmp/swarm_stats.csv\n");

    printf("✓ Full swarm training test passed\n\n");

    kolibri_swarm_free(&learner);
}

/* ============================================================================
 * ТЕСТЫ SCALABILITY (10 узлов)
 * ============================================================================ */

void test_10_node_swarm(void) {
    printf("Testing 10-node swarm scalability...\n");

    KolibriSwarmConfig config = {0};
    config.num_nodes = 10;
    config.local_population_size = 20;
    config.local_generations_per_sync = 5;
    config.num_formulas_to_exchange = 2;
    config.verify_received_formulas = 1;
    config.fitness_improvement_threshold = 0.9;
    config.verbose = 0;
    config.log_every_n_syncs = 3;

    KolibriSwarmLearner learner;
    kolibri_swarm_init(&learner, &config, 99999);

    double start_time = kolibri_swarm_time_ms();

    /* Обучаем swarm */
    int ret = kolibri_swarm_train(&learner, 3, 5, sum_fitness, NULL, NULL, NULL);
    assert(ret == 0);

    double elapsed = kolibri_swarm_time_ms() - start_time;

    printf("\n  10-node swarm results:\n");
    printf("    Elapsed time: %.1fms\n", elapsed);
    printf("    Global best: %.4f\n", learner.global_best_fitness);
    printf("    Swarm diversity: %.4f\n", learner.swarm_diversity);

    /* Подсчитываем общую статистику */
    int total_accepted = 0;
    for (int i = 0; i < 10; i++) {
        total_accepted += learner.node_stats[i].formulas_accepted;
    }

    printf("    Total formulas accepted: %d\n", total_accepted);

    /* Все узлы должны быть активны */
    for (int i = 0; i < 10; i++) {
        assert(learner.node_stats[i].local_generation > 0);
    }

    printf("✓ 10-node swarm scalability test passed\n\n");

    kolibri_swarm_free(&learner);
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("===========================================\n");
    printf("Kolibri Swarm Learner Tests\n");
    printf("===========================================\n\n");

    /* Initialization */
    printf("--- Initialization ---\n\n");
    test_swarm_init();

    /* Local Training */
    printf("--- Local Training ---\n\n");
    test_local_training();

    /* Exchange */
    printf("--- Exchange ---\n\n");
    test_exchange_packet();
    test_receive_and_merge();

    /* Sync */
    printf("--- Synchronization ---\n\n");
    test_sync_round();

    /* Full Training */
    printf("--- Full Swarm Training ---\n\n");
    test_full_swarm_training();

    /* Scalability */
    printf("--- Scalability ---\n\n");
    test_10_node_swarm();

    printf("===========================================\n");
    printf("All tests passed! ✓\n");
    printf("===========================================\n");

    return 0;
}
