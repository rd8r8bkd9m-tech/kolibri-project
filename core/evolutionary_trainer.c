/*
 * evolutionary_trainer.c
 *
 * Реализация эволюционного обучения формул для Kolibri
 *
 * Реализует:
 *   - Популяционную эволюцию (до 256 формул)
 *   - Tournament selection
 *   - 5 типов мутаций + 3 типа кроссовера
 *   - Speciation для поддержания разнообразия
 *   - Fitness sharing
 *   - Elitism
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/evolutionary_trainer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * ВНУТРЕННИЕ ФУНКЦИИ
 * ============================================================================ */

/** Простой PRNG (xorshift64) */
static uint64_t evo_rng_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static int evo_rand_int(KolibriEvoTrainer *trainer, int min, int max) {
    uint64_t val = evo_rng_next(&trainer->rng_state);
    return min + (int)(val % (uint64_t)(max - min + 1));
}

static double evo_rand_double(KolibriEvoTrainer *trainer) {
    uint64_t val = evo_rng_next(&trainer->rng_state) & 0x7FFFFFFF;
    return (double)val / (double)0x7FFFFFFF;
}

/** Получить текущее время */
static double get_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ============================================================================
 * ИНИЦИАЛИЗАЦИЯ
 * ============================================================================ */

int kolibri_evo_init(KolibriEvoTrainer *trainer,
                     const KolibriEvoConfig *config,
                     uint64_t seed) {
    if (!trainer || !config) return -1;

    memset(trainer, 0, sizeof(KolibriEvoTrainer));
    trainer->config = *config;
    trainer->rng_state = seed ? seed : 12345;
    trainer->start_time = get_time_seconds();
    trainer->best_fitness_ever = -1e30;

    int pop_size = config->population_size;
    if (pop_size > KET_MAX_POPULATION) pop_size = KET_MAX_POPULATION;

    /* Инициализируем популяцию случайными формулами */
    for (int i = 0; i < pop_size; i++) {
        KolibriEvoFormula *formula = &trainer->population[i];
        formula->fitness = -1e30;
        formula->adjusted_fitness = 0;
        formula->species_id = -1;
        formula->age = 0;
        formula->offspring_count = 0;
        formula->parent1_id = -1;
        formula->parent2_id = -1;
        formula->is_elite = 0;

        /* Случайные цифры */
        for (int j = 0; j < KET_FORMULA_SIZE; j++) {
            formula->digits[j] = (uint8_t)(evo_rand_int(trainer, 0, 9));
        }
    }

    trainer->num_species = 0;
    trainer->current_generation = 0;

    return 0;
}

void kolibri_evo_free(KolibriEvoTrainer *trainer) {
    if (!trainer) return;
    memset(trainer, 0, sizeof(KolibriEvoTrainer));
}

/* ============================================================================
 * FITNESS EVALUATION
 * ============================================================================ */

int kolibri_evo_evaluate_fitness(KolibriEvoTrainer *trainer,
                                  KolibriEvoFitnessFunc func,
                                  void *data) {
    if (!trainer || !func) return -1;

    int pop_size = trainer->config.population_size;
    double total_fitness = 0.0;
    double best_fitness = -1e30;

    for (int i = 0; i < pop_size; i++) {
        KolibriEvoFormula *formula = &trainer->population[i];

        /* Вычисляем fitness */
        formula->fitness = func(formula->digits, KET_FORMULA_SIZE, data);
        total_fitness += formula->fitness;

        if (formula->fitness > best_fitness) {
            best_fitness = formula->fitness;
            trainer->best_formula_index = i;
        }
    }

    /* Обновляем best_fitness_ever */
    if (best_fitness > trainer->best_fitness_ever) {
        trainer->best_fitness_ever = best_fitness;
        trainer->generations_without_improvement = 0;
    } else {
        trainer->generations_without_improvement++;
    }

    /* Fitness sharing */
    if (trainer->config.sharing_radius > 0.0) {
        for (int i = 0; i < pop_size; i++) {
            double niche_count = 1.0;
            for (int j = 0; j < pop_size; j++) {
                if (i == j) continue;

                double dist = kolibri_evo_formula_distance(
                    trainer->population[i].digits,
                    trainer->population[j].digits,
                    KET_FORMULA_SIZE
                );

                if (dist < trainer->config.sharing_radius) {
                    niche_count += 1.0 - dist / trainer->config.sharing_radius;
                }
            }
            trainer->population[i].adjusted_fitness =
                trainer->population[i].fitness / niche_count;
        }
    } else {
        for (int i = 0; i < pop_size; i++) {
            trainer->population[i].adjusted_fitness = trainer->population[i].fitness;
        }
    }

    return 0;
}

/* ============================================================================
 * SPECIATION
 * ============================================================================ */

double kolibri_evo_formula_distance(const uint8_t *f1,
                                    const uint8_t *f2,
                                    int size) {
    if (!f1 || !f2 || size <= 0) return 1.0;

    double total_diff = 0.0;
    for (int i = 0; i < size; i++) {
        int diff = abs((int)f1[i] - (int)f2[i]);
        total_diff += diff;
    }

    /* Нормализуем: максимальная разница = 9 на каждую позицию */
    return total_diff / (9.0 * size);
}

int kolibri_evo_speciate(KolibriEvoTrainer *trainer) {
    if (!trainer) return -1;

    int pop_size = trainer->config.population_size;
    double threshold = trainer->config.compatibility_threshold;

    /* Сбрасываем виды */
    trainer->num_species = 0;
    for (int i = 0; i < pop_size; i++) {
        trainer->population[i].species_id = -1;
    }

    /* Назначаем виды */
    for (int i = 0; i < pop_size; i++) {
        if (trainer->population[i].species_id >= 0) continue;

        /* Проверяем совместимость с существующими видами */
        int assigned_species = -1;
        for (int s = 0; s < trainer->num_species && s < trainer->config.max_species; s++) {
            double dist = kolibri_evo_formula_distance(
                trainer->population[i].digits,
                trainer->species[s].representative,
                KET_FORMULA_SIZE
            );

            if (dist < threshold) {
                assigned_species = s;
                break;
            }
        }

        /* Создаём новый вид если не нашли */
        if (assigned_species < 0 && trainer->num_species < trainer->config.max_species) {
            assigned_species = trainer->num_species;
            KolibriEvoSpecies *species = &trainer->species[assigned_species];
            species->id = assigned_species;
            memcpy(species->representative, trainer->population[i].digits, KET_FORMULA_SIZE);
            species->member_count = 0;
            species->avg_fitness = 0;
            species->best_fitness = -1e30;
            species->no_improvement_generations = 0;
            trainer->num_species++;
        }

        if (assigned_species >= 0) {
            trainer->population[i].species_id = assigned_species;
        }
    }

    /* Обновляем статистику видов */
    for (int s = 0; s < trainer->num_species; s++) {
        trainer->species[s].member_count = 0;
        trainer->species[s].avg_fitness = 0;
        trainer->species[s].best_fitness = -1e30;
    }

    for (int i = 0; i < pop_size; i++) {
        int s = trainer->population[i].species_id;
        if (s < 0 || s >= trainer->num_species) continue;

        trainer->species[s].member_count++;
        trainer->species[s].avg_fitness += trainer->population[i].fitness;
        if (trainer->population[i].fitness > trainer->species[s].best_fitness) {
            trainer->species[s].best_fitness = trainer->population[i].fitness;
        }
    }

    for (int s = 0; s < trainer->num_species; s++) {
        if (trainer->species[s].member_count > 0) {
            trainer->species[s].avg_fitness /= trainer->species[s].member_count;
        }
    }

    return 0;
}

/* ============================================================================
 * SELECTION
 * ============================================================================ */

int kolibri_evo_tournament_select(KolibriEvoTrainer *trainer,
                                  int tournament_size) {
    if (!trainer || tournament_size <= 0) return -1;

    int pop_size = trainer->config.population_size;
    int best_idx = -1;
    double best_fitness = -1e30;

    for (int t = 0; t < tournament_size; t++) {
        int idx = evo_rand_int(trainer, 0, pop_size - 1);
        double fitness = trainer->population[idx].adjusted_fitness;

        if (fitness > best_fitness) {
            best_fitness = fitness;
            best_idx = idx;
        }
    }

    return best_idx;
}

/* ============================================================================
 * CROSSOVER
 * ============================================================================ */

int kolibri_evo_crossover(KolibriEvoTrainer *trainer,
                          int parent1, int parent2,
                          int child) {
    if (!trainer || parent1 < 0 || parent2 < 0 || child < 0) return -1;

    KolibriEvoFormula *p1 = &trainer->population[parent1];
    KolibriEvoFormula *p2 = &trainer->population[parent2];
    KolibriEvoFormula *c = &trainer->population[child];

    c->parent1_id = parent1;
    c->parent2_id = parent2;
    c->age = 0;
    c->offspring_count = 0;
    c->is_elite = 0;

    switch (trainer->config.crossover_type) {
        case KET_CROSSOVER_SINGLE_POINT: {
            /* Одноточечный кроссовер */
            int point = evo_rand_int(trainer, 1, KET_FORMULA_SIZE - 1);
            for (int i = 0; i < point; i++) {
                c->digits[i] = p1->digits[i];
            }
            for (int i = point; i < KET_FORMULA_SIZE; i++) {
                c->digits[i] = p2->digits[i];
            }
            break;
        }

        case KET_CROSSOVER_TWO_POINT: {
            /* Двухточечный кроссовер */
            int p1_point = evo_rand_int(trainer, 1, KET_FORMULA_SIZE - 1);
            int p2_point = evo_rand_int(trainer, 1, KET_FORMULA_SIZE - 1);
            if (p1_point > p2_point) {
                int tmp = p1_point;
                p1_point = p2_point;
                p2_point = tmp;
            }
            for (int i = 0; i < p1_point; i++) {
                c->digits[i] = p1->digits[i];
            }
            for (int i = p1_point; i < p2_point; i++) {
                c->digits[i] = p2->digits[i];
            }
            for (int i = p2_point; i < KET_FORMULA_SIZE; i++) {
                c->digits[i] = p1->digits[i];
            }
            break;
        }

        case KET_CROSSOVER_UNIFORM: {
            /* Равномерный кроссовер */
            for (int i = 0; i < KET_FORMULA_SIZE; i++) {
                c->digits[i] = (evo_rand_double(trainer) < 0.5) ?
                               p1->digits[i] : p2->digits[i];
            }
            break;
        }

        default:
            return -1;
    }

    p1->offspring_count++;
    p2->offspring_count++;

    return 0;
}

/* ============================================================================
 * MUTATION
 * ============================================================================ */

int kolibri_evo_mutate(KolibriEvoTrainer *trainer,
                       int formula_idx,
                       KolibriEvoMutationType mutation_type) {
    if (!trainer || formula_idx < 0) return -1;

    KolibriEvoFormula *formula = &trainer->population[formula_idx];

    switch (mutation_type) {
        case KET_MUTATION_POINT: {
            /* Точечная мутация: меняем несколько случайных цифр */
            int num_changes = evo_rand_int(trainer, 1, 10);
            for (int i = 0; i < num_changes; i++) {
                int pos = evo_rand_int(trainer, 0, KET_FORMULA_SIZE - 1);
                formula->digits[pos] = (uint8_t)evo_rand_int(trainer, 0, 9);
            }
            break;
        }

        case KET_MUTATION_SWAP: {
            /* Swap: меняем две случайные позиции */
            int pos1 = evo_rand_int(trainer, 0, KET_FORMULA_SIZE - 1);
            int pos2 = evo_rand_int(trainer, 0, KET_FORMULA_SIZE - 1);
            if (pos1 != pos2) {
                uint8_t tmp = formula->digits[pos1];
                formula->digits[pos1] = formula->digits[pos2];
                formula->digits[pos2] = tmp;
            }
            break;
        }

        case KET_MUTATION_INVERT: {
            /* Инверсия: переворачиваем сегмент */
            int start = evo_rand_int(trainer, 0, KET_FORMULA_SIZE - 2);
            int end = evo_rand_int(trainer, start + 1, KET_FORMULA_SIZE - 1);
            while (start < end) {
                uint8_t tmp = formula->digits[start];
                formula->digits[start] = formula->digits[end];
                formula->digits[end] = tmp;
                start++;
                end--;
            }
            break;
        }

        case KET_MUTATION_SCRAMBLE: {
            /* Перемешивание: случайно перемешиваем сегмент */
            int start = evo_rand_int(trainer, 0, KET_FORMULA_SIZE - 2);
            int end = evo_rand_int(trainer, start + 1, KET_FORMULA_SIZE - 1);
            for (int i = start; i < end; i++) {
                int j = evo_rand_int(trainer, i, end);
                uint8_t tmp = formula->digits[i];
                formula->digits[i] = formula->digits[j];
                formula->digits[j] = tmp;
            }
            break;
        }

        case KET_MUTATION_SHIFT: {
            /* Сдвиг: сдвигаем все цифры на случайное значение */
            int shift = evo_rand_int(trainer, 1, 9);
            for (int i = 0; i < KET_FORMULA_SIZE; i++) {
                formula->digits[i] = (formula->digits[i] + shift) % 10;
            }
            break;
        }

        default:
            return -1;
    }

    return 0;
}

/* ============================================================================
 * EVOLUTION STEP
 * ============================================================================ */

int kolibri_evo_step(KolibriEvoTrainer *trainer) {
    if (!trainer) return -1;

    int pop_size = trainer->config.population_size;
    int elitism_count = trainer->config.elitism_count;
    double crossover_rate = trainer->config.crossover_rate;
    double mutation_rate = trainer->config.mutation_rate;

    /* Сортируем популяцию по fitness (для elitism) */
    /* Простой bubble sort для небольших популяций */
    for (int i = 0; i < pop_size - 1; i++) {
        for (int j = 0; j < pop_size - i - 1; j++) {
            if (trainer->population[j].adjusted_fitness <
                trainer->population[j + 1].adjusted_fitness) {
                KolibriEvoFormula tmp = trainer->population[j];
                trainer->population[j] = trainer->population[j + 1];
                trainer->population[j + 1] = tmp;
            }
        }
    }

    /* Elitism: сохраняем лучших */
    for (int i = 0; i < elitism_count && i < pop_size; i++) {
        trainer->population[i].is_elite = 1;
    }

    /* Создаём новое поколение */
    int num_children = pop_size - elitism_count;
    int child_idx = elitism_count;

    while (child_idx < pop_size) {
        /* Tournament selection */
        int parent1 = kolibri_evo_tournament_select(trainer, 3);
        int parent2 = kolibri_evo_tournament_select(trainer, 3);

        /* Crossover */
        if (evo_rand_double(trainer) < crossover_rate && parent1 != parent2) {
            kolibri_evo_crossover(trainer, parent1, parent2, child_idx);
        } else {
            /* Копируем родителя */
            int parent = parent1;
            memcpy(trainer->population[child_idx].digits,
                   trainer->population[parent].digits,
                   KET_FORMULA_SIZE);
            trainer->population[child_idx].parent1_id = parent;
            trainer->population[child_idx].parent2_id = -1;
            trainer->population[child_idx].age = 0;
            trainer->population[child_idx].offspring_count = 0;
            trainer->population[child_idx].is_elite = 0;
        }

        /* Mutation */
        if (evo_rand_double(trainer) < mutation_rate) {
            int mutation_type_idx = evo_rand_int(trainer, 0,
                                                  trainer->config.num_mutation_types - 1);
            KolibriEvoMutationType mutation_type =
                trainer->config.mutation_types[mutation_type_idx];
            kolibri_evo_mutate(trainer, child_idx, mutation_type);
        }

        child_idx++;
    }

    /* Увеличиваем возраст */
    for (int i = 0; i < pop_size; i++) {
        trainer->population[i].age++;
    }

    trainer->current_generation++;

    return 0;
}

/* ============================================================================
 * MAIN EVOLUTION LOOP
 * ============================================================================ */

int kolibri_evo_run(KolibriEvoTrainer *trainer,
                    int generations,
                    KolibriEvoFitnessFunc fitness_func,
                    void *fitness_data,
                    KolibriEvoProgressCallback progress,
                    void *user_data) {
    if (!trainer || generations <= 0) return -1;

    /* Вычисляем initial fitness ПЕРЕД первым шагом */
    int pop_size = trainer->config.population_size;

    if (fitness_func) {
        /* Вычисляем fitness через callback */
        kolibri_evo_evaluate_fitness(trainer, fitness_func, fitness_data);
    } else if (trainer->population[0].fitness < -1e29) {
        /* Fitness не была вычислена - используем заглушку */
        for (int i = 0; i < pop_size; i++) {
            trainer->population[i].fitness = 0.0;
            trainer->population[i].adjusted_fitness = 0.0;
        }
    } else {
        /* Fitness была вычислена externally */
        for (int i = 0; i < pop_size; i++) {
            trainer->population[i].adjusted_fitness = trainer->population[i].fitness;
        }
    }

    double start_time = get_time_seconds();

    for (int gen = 0; gen < generations; gen++) {
        /* Speciation каждые 5 поколений */
        if (gen % 5 == 0) {
            kolibri_evo_speciate(trainer);
        }

        /* Один шаг эволюции */
        int ret = kolibri_evo_step(trainer);
        if (ret != 0) return ret;

        /* Вычисляем fitness для нового поколения */
        if (fitness_func) {
            kolibri_evo_evaluate_fitness(trainer, fitness_func, fitness_data);
        }

        /* Для текущей реализации: используем заглушку fitness если не вычислена */
        int pop_size = trainer->config.population_size;
        double total_fitness = 0.0;
        double best_fitness = -1e30;
        double worst_fitness = 1e30;

        for (int i = 0; i < pop_size; i++) {
            double f = trainer->population[i].fitness;
            /* Если fitness не вычислена (осталась -1e30), используем adjusted */
            if (f < -1e29) f = trainer->population[i].adjusted_fitness;
            total_fitness += f;
            if (f > best_fitness) best_fitness = f;
            if (f < worst_fitness) worst_fitness = f;
        }

        double avg_fitness = total_fitness / pop_size;

        /* Std deviation */
        double variance = 0.0;
        for (int i = 0; i < pop_size; i++) {
            double diff = trainer->population[i].fitness - avg_fitness;
            variance += diff * diff;
        }
        double std_fitness = sqrt(variance / pop_size);

        /* Genetic diversity */
        double diversity = 0.0;
        int sample_size = (pop_size < 50) ? pop_size : 50;
        for (int s = 0; s < sample_size; s++) {
            int i = evo_rand_int(trainer, 0, pop_size - 1);
            int j = evo_rand_int(trainer, 0, pop_size - 1);
            if (i != j) {
                diversity += kolibri_evo_formula_distance(
                    trainer->population[i].digits,
                    trainer->population[j].digits,
                    KET_FORMULA_SIZE
                );
            }
        }
        diversity /= (sample_size > 1) ? (sample_size - 1) : 1;

        /* Сохраняем статистику */
        trainer->stats.generation = trainer->current_generation;
        trainer->stats.best_fitness = best_fitness;
        trainer->stats.avg_fitness = avg_fitness;
        trainer->stats.worst_fitness = worst_fitness;
        trainer->stats.std_fitness = std_fitness;
        trainer->stats.num_species = trainer->num_species;
        trainer->stats.diversity = diversity;
        trainer->stats.elapsed_seconds = get_time_seconds() - start_time;

        /* History */
        if (trainer->history_size < 1000) {
            trainer->history[trainer->history_size] = trainer->stats;
            trainer->history_size++;
        }

        /* Callback */
        if (progress) {
            progress(trainer->current_generation, best_fitness, avg_fitness, user_data);
        }

        /* Logging */
        if (trainer->config.verbose &&
            (gen + 1) % trainer->config.log_every_n_generations == 0) {
            kolibri_evo_print_stats(trainer);
        }

        /* Early stopping */
        if (best_fitness >= trainer->config.target_fitness) {
            if (trainer->config.verbose) {
                printf("Target fitness reached at generation %d\n",
                       trainer->current_generation);
            }
            break;
        }

        if (trainer->generations_without_improvement >= trainer->config.stagnation_limit) {
            if (trainer->config.verbose) {
                printf("Stagnation detected at generation %d\n",
                       trainer->current_generation);
            }
            break;
        }
    }

    return 0;
}

/* ============================================================================
 * STATISTICS
 * ============================================================================ */

double kolibri_evo_get_best_formula(const KolibriEvoTrainer *trainer,
                                     uint8_t *formula) {
    if (!trainer || !formula) return -1e30;

    memcpy(formula,
           trainer->population[trainer->best_formula_index].digits,
           KET_FORMULA_SIZE);

    return trainer->best_fitness_ever;
}

void kolibri_evo_print_stats(const KolibriEvoTrainer *trainer) {
    if (!trainer) return;

    printf("Generation %d:\n", trainer->stats.generation);
    printf("  Best fitness:   %.4f\n", trainer->stats.best_fitness);
    printf("  Avg fitness:    %.4f\n", trainer->stats.avg_fitness);
    printf("  Worst fitness:  %.4f\n", trainer->stats.worst_fitness);
    printf("  Std fitness:    %.4f\n", trainer->stats.std_fitness);
    printf("  Species:        %d\n", trainer->stats.num_species);
    printf("  Diversity:      %.4f\n", trainer->stats.diversity);
    printf("  Best ever:      %.4f\n", trainer->best_fitness_ever);
    printf("  Time:           %.2fs\n", trainer->stats.elapsed_seconds);
    printf("\n");
}

int kolibri_evo_save_stats(const KolibriEvoTrainer *trainer,
                           const char *filepath) {
    if (!trainer || !filepath) return -1;

    FILE *f = fopen(filepath, "w");
    if (!f) return -2;

    /* Header */
    fprintf(f, "generation,best_fitness,avg_fitness,worst_fitness,"
               "std_fitness,num_species,diversity,elapsed_seconds\n");

    /* Data */
    for (int i = 0; i < trainer->history_size; i++) {
        const KolibriEvoGenerationStats *s = &trainer->history[i];
        fprintf(f, "%d,%.6f,%.6f,%.6f,%.6f,%d,%.6f,%.2f\n",
                s->generation, s->best_fitness, s->avg_fitness,
                s->worst_fitness, s->std_fitness, s->num_species,
                s->diversity, s->elapsed_seconds);
    }

    fclose(f);
    return 0;
}
