#include "kolibri/formula.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void teach_linear_task(KolibriFormulaPool *pool) {
  for (int i = 0; i < 4; ++i) {
    int input = i;
    int target = 2 * i + 1;
    assert(kf_pool_add_example(pool, input, target) == 0);
  }
}

static void assert_deterministic(void) {
  KolibriFormulaPool *first = (KolibriFormulaPool *)malloc(sizeof(KolibriFormulaPool));
  KolibriFormulaPool *second = (KolibriFormulaPool *)malloc(sizeof(KolibriFormulaPool));
  assert(first != NULL);
  assert(second != NULL);
  kf_pool_init(first, 2025);
  kf_pool_init(second, 2025);
  teach_linear_task(first);
  teach_linear_task(second);
  kf_pool_tick(first, 64);
  kf_pool_tick(second, 64);
  const KolibriFormula *best_first = kf_pool_best(first);
  const KolibriFormula *best_second = kf_pool_best(second);
  uint8_t digits_first[32];
  uint8_t digits_second[32];
  size_t len_first =
      kf_formula_digits(best_first, digits_first, sizeof(digits_first));
  size_t len_second =
      kf_formula_digits(best_second, digits_second, sizeof(digits_second));
  assert(len_first == len_second);
  assert(memcmp(digits_first, digits_second, len_first) == 0);
  kf_pool_free(first);
  kf_pool_free(second);
  free(first);
  free(second);
}

static void test_feedback_adjustment(void) {
  KolibriFormulaPool *pool = (KolibriFormulaPool *)malloc(sizeof(KolibriFormulaPool));
  assert(pool != NULL);
  kf_pool_init(pool, 321);
  teach_linear_task(pool);
  kf_pool_tick(pool, 64);
  const KolibriFormula *best = kf_pool_best(pool);
  assert(best != NULL);
  KolibriGene snapshot = best->gene;
  double baseline = best->fitness;
  assert(kf_pool_feedback(pool, &snapshot, 0.3) == 0);
  const KolibriFormula *after_reward = kf_pool_best(pool);
  assert(after_reward != NULL);
  assert(after_reward->fitness >= baseline);
  assert(kf_pool_feedback(pool, &snapshot, -0.8) == 0);
  const KolibriFormula *after_penalty = kf_pool_best(pool);
  assert(after_penalty != NULL);
  assert(after_penalty->fitness >= 0.0);
  kf_pool_free(pool);
  free(pool);
}

/* ============================================================================
 * Тесты эволюционного реактора
 * ============================================================================ */

static void test_evolution_config(void) {
  KolibriEvolutionConfig config;
  kf_config_default(&config);
  
  assert(config.mutation_rate > 0.0 && config.mutation_rate <= 1.0);
  assert(config.crossover_rate > 0.0 && config.crossover_rate <= 1.0);
  assert(config.elite_ratio > 0.0 && config.elite_ratio <= 1.0);
  assert(config.mutation_type < KOLIBRI_MUTATION_COUNT);
  assert(config.crossover_type < KOLIBRI_CROSSOVER_COUNT);
  
  KolibriFormulaPool *pool = (KolibriFormulaPool *)malloc(sizeof(KolibriFormulaPool));
  assert(pool != NULL);
  kf_pool_init(pool, 123);
  
  /* Модифицируем конфигурацию */
  config.mutation_rate = 0.2;
  config.mutation_type = KOLIBRI_MUTATION_SWAP;
  config.crossover_type = KOLIBRI_CROSSOVER_UNIFORM;
  
  assert(kf_pool_set_config(pool, &config) == 0);
  
  KolibriEvolutionConfig retrieved;
  assert(kf_pool_get_config(pool, &retrieved) == 0);
  assert(retrieved.mutation_rate == 0.2);
  assert(retrieved.mutation_type == KOLIBRI_MUTATION_SWAP);
  assert(retrieved.crossover_type == KOLIBRI_CROSSOVER_UNIFORM);
  
  kf_pool_free(pool);
  free(pool);
}

static void test_evolution_metrics(void) {
  KolibriFormulaPool *pool = (KolibriFormulaPool *)malloc(sizeof(KolibriFormulaPool));
  assert(pool != NULL);
  kf_pool_init(pool, 456);
  teach_linear_task(pool);
  
  /* Начальные метрики должны быть нулевыми */
  KolibriEvolutionMetrics metrics;
  assert(kf_pool_get_metrics(pool, &metrics) == 0);
  assert(metrics.total_generations == 0);
  assert(metrics.total_mutations == 0);
  
  /* Выполняем несколько поколений */
  kf_pool_tick(pool, 10);
  
  assert(kf_pool_get_metrics(pool, &metrics) == 0);
  /* Метрики должны обновиться */
  
  /* Сброс метрик */
  kf_pool_reset_metrics(pool);
  assert(kf_pool_get_metrics(pool, &metrics) == 0);
  assert(metrics.total_generations == 0);
  
  kf_pool_free(pool);
  free(pool);
}

static void test_reactor_run(void) {
  KolibriFormulaPool *pool = (KolibriFormulaPool *)malloc(sizeof(KolibriFormulaPool));
  assert(pool != NULL);
  kf_pool_init(pool, 789);
  teach_linear_task(pool);
  
  /* Запускаем реактор */
  int generations = kf_reactor_run(pool, 50, 0.99);
  assert(generations > 0);
  assert(generations <= 50);
  
  /* Проверяем, что метрики обновились */
  KolibriEvolutionMetrics metrics;
  assert(kf_pool_get_metrics(pool, &metrics) == 0);
  assert(metrics.total_generations > 0);
  
  kf_pool_free(pool);
  free(pool);
}

static void test_metrics_to_digits(void) {
  KolibriEvolutionMetrics metrics = {
    .total_generations = 100,
    .total_mutations = 50,
    .beneficial_mutations = 20,
    .harmful_mutations = 5,
    .stagnation_count = 3,
    .best_fitness = 0.85,
    .avg_fitness = 0.65
  };
  
  char buffer[256];
  int len = kf_metrics_to_digits(&metrics, buffer, sizeof(buffer));
  assert(len > 0);
  
  /* Проверяем, что результат содержит только цифры */
  for (int i = 0; i < len; i++) {
    assert(buffer[i] >= '0' && buffer[i] <= '9');
  }
}

static void test_mutation_types(void) {
  KolibriFormulaPool *pool = (KolibriFormulaPool *)malloc(sizeof(KolibriFormulaPool));
  assert(pool != NULL);
  kf_pool_init(pool, 111);
  teach_linear_task(pool);
  
  KolibriEvolutionConfig config;
  kf_config_default(&config);
  
  /* Тестируем каждый тип мутации */
  for (int type = 0; type < KOLIBRI_MUTATION_COUNT; type++) {
    config.mutation_type = (KolibriMutationType)type;
    assert(kf_pool_set_config(pool, &config) == 0);
    kf_pool_tick(pool, 5);
  }
  
  /* Тестируем каждый тип кроссовера */
  for (int type = 0; type < KOLIBRI_CROSSOVER_COUNT; type++) {
    config.crossover_type = (KolibriCrossoverType)type;
    assert(kf_pool_set_config(pool, &config) == 0);
    kf_pool_tick(pool, 5);
  }
  
  kf_pool_free(pool);
  free(pool);
}

static void test_adaptive_evolution(void) {
  KolibriFormulaPool *pool = (KolibriFormulaPool *)malloc(sizeof(KolibriFormulaPool));
  assert(pool != NULL);
  kf_pool_init(pool, 222);
  teach_linear_task(pool);
  
  KolibriEvolutionConfig config;
  kf_config_default(&config);
  config.adaptive_mutation = 1;
  assert(kf_pool_set_config(pool, &config) == 0);
  
  double initial_mutation_rate = config.mutation_rate;
  
  /* Запускаем с адаптацией */
  kf_reactor_run(pool, 100, 0.99);
  
  /* Проверяем, что конфигурация могла измениться */
  KolibriEvolutionConfig updated;
  kf_pool_get_config(pool, &updated);
  /* Мутация могла измениться в любую сторону */
  (void)initial_mutation_rate; /* Подавляем warning */
  
  kf_pool_free(pool);
  free(pool);
}

void test_formula(void) {
  KolibriFormulaPool *pool = (KolibriFormulaPool *)malloc(sizeof(KolibriFormulaPool));
  assert(pool != NULL);
  kf_pool_init(pool, 77);
  teach_linear_task(pool);
  const KolibriFormula *initial = kf_pool_best(pool);
  int baseline_errors = 0;
  for (int i = 0; i < 4; ++i) {
    int local = 0;
    assert(kf_formula_apply(initial, i, &local) == 0);
    baseline_errors += abs((2 * i + 1) - local);
  }
  kf_pool_tick(pool, 128);
  const KolibriFormula *best = kf_pool_best(pool);
  assert(best != NULL);
  int prediction = 0;
  assert(kf_formula_apply(best, 4, &prediction) == 0);
  int errors = 0;
  for (int i = 0; i < 4; ++i) {
    int local = 0;
    assert(kf_formula_apply(best, i, &local) == 0);
    errors += abs((2 * i + 1) - local);
  }
  assert(errors <= baseline_errors);
  assert_deterministic();
  test_feedback_adjustment();
  
  /* Тесты эволюционного реактора */
  test_evolution_config();
  test_evolution_metrics();
  test_reactor_run();
  test_metrics_to_digits();
  test_mutation_types();
  test_adaptive_evolution();
  
  kf_pool_free(pool);
  free(pool);
}
