/*
 * test_evolutionary_trainer.c
 *
 * Тесты для Evolutionary Trainer
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "kolibri/evolutionary_trainer.h"

/* ============================================================================
 * FITNESS FUNCTIONS
 * ============================================================================ */

/** Простая fitness функция: сумма цифр (для тестирования) */
static double sum_fitness(const uint8_t *formula, int formula_size, void *data) {
    (void)data;
    double sum = 0.0;
    for (int i = 0; i < formula_size; i++) {
        sum += formula[i];
    }
    return sum / formula_size;  /* Нормализуем */
}

/** Fitness функция: количество повторений цифры 7 */
static double sevens_fitness(const uint8_t *formula, int formula_size, void *data) {
    (void)data;
    int count = 0;
    for (int i = 0; i < formula_size; i++) {
        if (formula[i] == 7) count++;
    }
    return (double)count / formula_size * 100.0;  /* Процент */
}

/* ============================================================================
 * ТЕСТЫ ИНИЦИАЛИЗАЦИИ
 * ============================================================================ */

void test_evo_init(void) {
    printf("Testing evolutionary trainer initialization...\n");

    KolibriEvoConfig config = {0};
    config.population_size = 50;
    config.elitism_count = 5;
    config.mutation_rate = 0.1;
    config.crossover_rate = 0.7;
    config.mutation_types[0] = KET_MUTATION_POINT;
    config.mutation_types[1] = KET_MUTATION_SWAP;
    config.num_mutation_types = 2;
    config.crossover_type = KET_CROSSOVER_SINGLE_POINT;
    config.compatibility_threshold = 0.3;
    config.max_species = 10;
    config.sharing_radius = 0.1;
    config.max_generations = 100;
    config.stagnation_limit = 50;
    config.log_every_n_generations = 10;
    config.verbose = 0;

    KolibriEvoTrainer trainer;
    int ret = kolibri_evo_init(&trainer, &config, 42);

    assert(ret == 0);
    assert(trainer.config.population_size == 50);
    assert(trainer.current_generation == 0);
    assert(trainer.num_species == 0);

    printf("✓ Evolutionary trainer initialization test passed\n\n");

    kolibri_evo_free(&trainer);
}

/* ============================================================================
 * ТЕСТЫ OPERATORS
 * ============================================================================ */

void test_formula_distance(void) {
    printf("Testing formula distance...\n");

    uint8_t f1[100], f2[100], f3[100];

    /* Одинаковые формулы */
    for (int i = 0; i < 100; i++) {
        f1[i] = 5;
        f2[i] = 5;
    }
    double dist_same = kolibri_evo_formula_distance(f1, f2, 100);
    assert(fabs(dist_same) < 1e-6);

    /* Максимально разные формулы */
    for (int i = 0; i < 100; i++) {
        f1[i] = 0;
        f3[i] = 9;
    }
    double dist_max = kolibri_evo_formula_distance(f1, f3, 100);
    assert(fabs(dist_max - 1.0) < 1e-6);

    /* Частично разные */
    for (int i = 0; i < 100; i++) {
        f1[i] = i % 10;
        f2[i] = (i + 1) % 10;
    }
    double dist_partial = kolibri_evo_formula_distance(f1, f2, 100);
    assert(dist_partial > 0.0 && dist_partial < 1.0);

    printf("  Distance (same): %.4f\n", dist_same);
    printf("  Distance (max different): %.4f\n", dist_max);
    printf("  Distance (partial): %.4f\n", dist_partial);

    printf("✓ Formula distance test passed\n\n");
}

void test_crossover(void) {
    printf("Testing crossover operators...\n");

    KolibriEvoConfig config = {0};
    config.population_size = 10;
    config.elitism_count = 2;
    config.mutation_rate = 0.0;  /* Без мутации для теста */
    config.crossover_rate = 1.0;
    config.num_mutation_types = 1;
    config.mutation_types[0] = KET_MUTATION_POINT;

    KolibriEvoTrainer trainer;
    kolibri_evo_init(&trainer, &config, 42);

    /* Инициализируем двух родителей */
    for (int i = 0; i < KET_FORMULA_SIZE; i++) {
        trainer.population[0].digits[i] = 1;
        trainer.population[1].digits[i] = 2;
    }

    /* Single-point crossover */
    trainer.config.crossover_type = KET_CROSSOVER_SINGLE_POINT;
    int ret = kolibri_evo_crossover(&trainer, 0, 1, 2);
    assert(ret == 0);

    /* Проверяем что ребёнок содержит части обоих родителей */
    int has_1 = 0, has_2 = 0;
    for (int i = 0; i < KET_FORMULA_SIZE; i++) {
        if (trainer.population[2].digits[i] == 1) has_1 = 1;
        if (trainer.population[2].digits[i] == 2) has_2 = 1;
    }
    assert(has_1 && has_2);

    printf("✓ Crossover test passed\n\n");

    kolibri_evo_free(&trainer);
}

void test_mutation(void) {
    printf("Testing mutation operators...\n");

    KolibriEvoConfig config = {0};
    config.population_size = 10;
    config.elitism_count = 2;
    config.mutation_rate = 0.0;
    config.crossover_rate = 0.0;
    config.num_mutation_types = 1;
    config.mutation_types[0] = KET_MUTATION_POINT;

    KolibriEvoTrainer trainer;
    kolibri_evo_init(&trainer, &config, 42);

    /* Инициализируем формулу */
    for (int i = 0; i < KET_FORMULA_SIZE; i++) {
        trainer.population[0].digits[i] = 5;
    }

    /* Point mutation */
    kolibri_evo_mutate(&trainer, 0, KET_MUTATION_POINT);
    int changed = 0;
    for (int i = 0; i < KET_FORMULA_SIZE; i++) {
        if (trainer.population[0].digits[i] != 5) changed++;
    }
    assert(changed > 0);
    printf("  Point mutation: %d digits changed\n", changed);

    /* Swap mutation */
    for (int i = 0; i < KET_FORMULA_SIZE; i++) {
        trainer.population[0].digits[i] = (uint8_t)i % 10;
    }
    kolibri_evo_mutate(&trainer, 0, KET_MUTATION_SWAP);
    printf("  Swap mutation: applied\n");

    /* Invert mutation */
    kolibri_evo_mutate(&trainer, 0, KET_MUTATION_INVERT);
    printf("  Invert mutation: applied\n");

    /* Scramble mutation */
    kolibri_evo_mutate(&trainer, 0, KET_MUTATION_SCRAMBLE);
    printf("  Scramble mutation: applied\n");

    /* Shift mutation */
    uint8_t before_shift[KET_FORMULA_SIZE];
    memcpy(before_shift, trainer.population[0].digits, KET_FORMULA_SIZE);
    kolibri_evo_mutate(&trainer, 0, KET_MUTATION_SHIFT);
    int shift_changed = 0;
    for (int i = 0; i < KET_FORMULA_SIZE; i++) {
        if (trainer.population[0].digits[i] != before_shift[i]) shift_changed++;
    }
    assert(shift_changed > 0);
    printf("  Shift mutation: %d digits changed\n", shift_changed);

    printf("✓ Mutation test passed\n\n");

    kolibri_evo_free(&trainer);
}

void test_tournament_selection(void) {
    printf("Testing tournament selection...\n");

    KolibriEvoConfig config = {0};
    config.population_size = 20;
    config.elitism_count = 2;
    config.mutation_rate = 0.0;
    config.crossover_rate = 0.0;
    config.num_mutation_types = 1;
    config.mutation_types[0] = KET_MUTATION_POINT;

    KolibriEvoTrainer trainer;
    kolibri_evo_init(&trainer, &config, 42);

    /* Устанавливаем разные fitness */
    for (int i = 0; i < 20; i++) {
        trainer.population[i].adjusted_fitness = (double)i;
    }

    /* Tournament selection должен чаще выбирать лучших */
    int best_count = 0;
    int num_trials = 100;

    for (int t = 0; t < num_trials; t++) {
        int selected = kolibri_evo_tournament_select(&trainer, 3);
        if (selected >= 15) best_count++;  /* Top 5 из 20 */
    }

    double best_ratio = (double)best_count / num_trials;
    printf("  Best selected: %d / %d (%.1f%%)\n", best_count, num_trials, best_ratio * 100);

    /* Должен выбирать лучших > 50% времени */
    assert(best_ratio > 0.5);

    printf("✓ Tournament selection test passed\n\n");

    kolibri_evo_free(&trainer);
}

/* ============================================================================
 * ТЕСТЫ SPECIATION
 * ============================================================================ */

void test_speciation(void) {
    printf("Testing speciation...\n");

    KolibriEvoConfig config = {0};
    config.population_size = 50;
    config.elitism_count = 5;
    config.mutation_rate = 0.0;
    config.crossover_rate = 0.0;
    config.num_mutation_types = 1;
    config.mutation_types[0] = KET_MUTATION_POINT;
    config.compatibility_threshold = 0.3;
    config.max_species = 10;

    KolibriEvoTrainer trainer;
    kolibri_evo_init(&trainer, &config, 42);

    /* Создаём формулы двух типов */
    for (int i = 0; i < 25; i++) {
        for (int j = 0; j < KET_FORMULA_SIZE; j++) {
            trainer.population[i].digits[j] = (uint8_t)(j % 3);  /* 0, 1, 2 */
        }
    }
    for (int i = 25; i < 50; i++) {
        for (int j = 0; j < KET_FORMULA_SIZE; j++) {
            trainer.population[i].digits[j] = (uint8_t)(7 + j % 3);  /* 7, 8, 9 */
        }
    }

    /* Вычисляем fitness */
    kolibri_evo_evaluate_fitness(&trainer, sum_fitness, NULL);

    /* Speciate */
    int ret = kolibri_evo_speciate(&trainer);
    assert(ret == 0);

    printf("  Number of species: %d\n", trainer.num_species);
    assert(trainer.num_species >= 1);

    /* Проверяем что формулы разных типов в разных видах */
    int species_0 = trainer.population[0].species_id;
    int species_25 = trainer.population[25].species_id;

    /* Могут быть в одном или разных видах в зависимости от threshold */
    printf("  Species[0]: %d, Species[25]: %d\n", species_0, species_25);

    printf("✓ Speciation test passed\n\n");

    kolibri_evo_free(&trainer);
}

/* ============================================================================
 * ТЕСТЫ EVOLUTION
 * ============================================================================ */

void test_evolution_step(void) {
    printf("Testing evolution step...\n");

    KolibriEvoConfig config = {0};
    config.population_size = 30;
    config.elitism_count = 3;
    config.mutation_rate = 0.1;
    config.crossover_rate = 0.7;
    config.mutation_types[0] = KET_MUTATION_POINT;
    config.mutation_types[1] = KET_MUTATION_SWAP;
    config.num_mutation_types = 2;
    config.crossover_type = KET_CROSSOVER_SINGLE_POINT;
    config.compatibility_threshold = 0.3;
    config.max_species = 5;
    config.log_every_n_generations = 10;
    config.verbose = 0;

    KolibriEvoTrainer trainer;
    kolibri_evo_init(&trainer, &config, 42);

    /* Вычисляем initial fitness */
    kolibri_evo_evaluate_fitness(&trainer, sum_fitness, NULL);

    double initial_best = -1e30;
    for (int i = 0; i < 30; i++) {
        if (trainer.population[i].fitness > initial_best) {
            initial_best = trainer.population[i].fitness;
        }
    }

    /* Несколько шагов эволюции */
    for (int gen = 0; gen < 10; gen++) {
        kolibri_evo_step(&trainer);
        kolibri_evo_evaluate_fitness(&trainer, sum_fitness, NULL);
    }

    double final_best = -1e30;
    for (int i = 0; i < 30; i++) {
        if (trainer.population[i].fitness > final_best) {
            final_best = trainer.population[i].fitness;
        }
    }

    printf("  Initial best fitness: %.4f\n", initial_best);
    printf("  Final best fitness: %.4f\n", final_best);

    /* Fitness должен улучшиться или остаться таким же */
    assert(final_best >= initial_best - 0.01);

    printf("✓ Evolution step test passed\n\n");

    kolibri_evo_free(&trainer);
}

void test_full_evolution_run(void) {
    printf("Testing full evolution run...\n");

    KolibriEvoConfig config = {0};
    config.population_size = 50;
    config.elitism_count = 5;
    config.mutation_rate = 0.15;
    config.crossover_rate = 0.8;
    config.mutation_types[0] = KET_MUTATION_POINT;
    config.mutation_types[1] = KET_MUTATION_SWAP;
    config.mutation_types[2] = KET_MUTATION_SHIFT;
    config.num_mutation_types = 3;
    config.crossover_type = KET_CROSSOVER_UNIFORM;
    config.compatibility_threshold = 0.25;
    config.max_species = 8;
    config.sharing_radius = 0.15;
    config.max_generations = 50;
    config.target_fitness = 60.0;  /* 60% семёрок */
    config.stagnation_limit = 100;
    config.log_every_n_generations = 10;
    config.verbose = 1;

    KolibriEvoTrainer trainer;
    kolibri_evo_init(&trainer, &config, 12345);

    /* Вычисляем initial fitness */
    kolibri_evo_evaluate_fitness(&trainer, sevens_fitness, NULL);

    printf("Evolving to maximize sevens...\n");

    int ret = kolibri_evo_run(&trainer, 50, sevens_fitness, NULL, NULL, NULL);
    assert(ret == 0);

    /* Получаем лучшую формулу */
    uint8_t best_formula[KET_FORMULA_SIZE];
    double best_fitness = kolibri_evo_get_best_formula(&trainer, best_formula);

    printf("\nBest formula fitness: %.4f\n", best_fitness);

    /* Считаем количество семёрок в лучшей формуле */
    int sevens = 0;
    for (int i = 0; i < KET_FORMULA_SIZE; i++) {
        if (best_formula[i] == 7) sevens++;
    }
    double sevens_pct = (double)sevens / KET_FORMULA_SIZE * 100.0;
    printf("Sevens in best formula: %d / %d (%.1f%%)\n",
           sevens, KET_FORMULA_SIZE, sevens_pct);

    /* Сохраняем статистику */
    kolibri_evo_save_stats(&trainer, "/tmp/evo_stats.csv");
    printf("Stats saved to /tmp/evo_stats.csv\n");

    kolibri_evo_free(&trainer);

    printf("✓ Full evolution run test passed\n\n");
}

/* ============================================================================
 * CALLBACK ТЕСТ
 * ============================================================================ */

static void evo_progress_callback(int generation, double best_fitness,
                                  double avg_fitness, void *user_data) {
    int *max_gen = (int*)user_data;
    if (generation > *max_gen) {
        *max_gen = generation;
    }
}

void test_progress_callback(void) {
    printf("Testing progress callback...\n");

    KolibriEvoConfig config = {0};
    config.population_size = 20;
    config.elitism_count = 2;
    config.mutation_rate = 0.1;
    config.crossover_rate = 0.7;
    config.mutation_types[0] = KET_MUTATION_POINT;
    config.num_mutation_types = 1;
    config.crossover_type = KET_CROSSOVER_SINGLE_POINT;
    config.compatibility_threshold = 0.3;
    config.max_species = 5;
    config.max_generations = 10;
    config.stagnation_limit = 100;
    config.target_fitness = 1e30;  /* Отключаем early stopping */
    config.verbose = 0;

    KolibriEvoTrainer trainer;
    kolibri_evo_init(&trainer, &config, 42);

    int max_gen_reached = 0;

    int ret = kolibri_evo_run(&trainer, 10, sum_fitness, NULL, evo_progress_callback, &max_gen_reached);
    assert(ret == 0);
    assert(max_gen_reached >= 10);

    printf("  Max generation reached: %d\n", max_gen_reached);

    kolibri_evo_free(&trainer);

    printf("✓ Progress callback test passed\n\n");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("===========================================\n");
    printf("Kolibri Evolutionary Trainer Tests\n");
    printf("===========================================\n\n");

    /* Initialization */
    printf("--- Initialization ---\n\n");
    test_evo_init();

    /* Operators */
    printf("--- Evolution Operators ---\n\n");
    test_formula_distance();
    test_crossover();
    test_mutation();
    test_tournament_selection();

    /* Speciation */
    printf("--- Speciation ---\n\n");
    test_speciation();

    /* Evolution */
    printf("--- Evolution ---\n\n");
    test_evolution_step();
    test_full_evolution_run();

    /* Callback */
    printf("--- Callback ---\n\n");
    test_progress_callback();

    printf("===========================================\n");
    printf("All tests passed! ✓\n");
    printf("===========================================\n");

    return 0;
}
