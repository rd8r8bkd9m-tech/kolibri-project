/*
 * evolutionary_trainer.h
 *
 * Эволюционное обучение формул для Kolibri
 *
 * Архитектура:
 *   Популяция из N формул эволюционирует через:
 *   1. Tournament selection — отбор лучших
 *   2. Crossover — скрещивание формул
 *   3. Mutation — мутации (5 типов)
 *   4. Speciation — разделение на виды для разнообразия
 *   5. Fitness sharing — совместная оценка
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_EVOLUTIONARY_TRAINER_H
#define KOLIBRI_EVOLUTIONARY_TRAINER_H

#include "kolibri/formula.h"
#include "kolibri/genome.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНСТАНТЫ
 * ============================================================================ */

/** Максимальный размер популяции */
#define KET_MAX_POPULATION 256

/** Максимальное количество поколений */
#define KET_MAX_GENERATIONS 10000

/** Максимальное количество видов */
#define KET_MAX_SPECIES 32

/** Размер формулы (цифры) */
#define KET_FORMULA_SIZE 4000

/* ============================================================================
 * ТИПЫ ДАННЫХ
 * ============================================================================ */

/** Тип мутации */
typedef enum {
    KET_MUTATION_POINT = 0,     /* Точечная мутация */
    KET_MUTATION_SWAP,          /* Swap двух цифр */
    KET_MUTATION_INVERT,        /* Инверсия сегмента */
    KET_MUTATION_SCRAMBLE,      /* Перемешивание сегмента */
    KET_MUTATION_SHIFT,         /* Сдвиг всех цифр */
    KET_MUTATION_COUNT
} KolibriEvoMutationType;

/** Тип кроссовера */
typedef enum {
    KET_CROSSOVER_SINGLE_POINT = 0,  /* Одноточечный */
    KET_CROSSOVER_TWO_POINT,         /* Двухточечный */
    KET_CROSSOVER_UNIFORM,           /* Равномерный */
    KET_CROSSOVER_COUNT
} KolibriEvoCrossoverType;

/** Формула в популяции */
typedef struct {
    uint8_t digits[KET_FORMULA_SIZE];  /* Цифры формулы */
    double fitness;                     /* Fitness score */
    double adjusted_fitness;            /* Adjusted для fitness sharing */
    int species_id;                     /* ID вида */
    int age;                            /* Возраст в поколениях */
    int offspring_count;                /* Количество потомков */
    int parent1_id;                     /* ID родителя 1 */
    int parent2_id;                     /* ID родителя 2 */
    int is_elite;                       /* Элитная формула? */
} KolibriEvoFormula;

/** Вид (species) */
typedef struct {
    int id;
    uint8_t representative[KET_FORMULA_SIZE];  /* Представитель вида */
    int member_count;                   /* Количество членов */
    double avg_fitness;                 /* Средний fitness */
    double best_fitness;                /* Лучший fitness */
    int no_improvement_generations;     /* Поколений без улучшения */
} KolibriEvoSpecies;

/** Конфигурация эволюции */
typedef struct {
    int population_size;                /* Размер популяции */
    int elitism_count;                  /* Количество элитных */
    double mutation_rate;               /* Вероятность мутации */
    double crossover_rate;              /* Вероятность кроссовера */
    KolibriEvoMutationType mutation_types[KET_MUTATION_COUNT];
    int num_mutation_types;
    KolibriEvoCrossoverType crossover_type;

    /* Speciation */
    double compatibility_threshold;     /* Порог совместимости для видов */
    int max_species;                    /* Максимум видов */

    /* Fitness sharing */
    double sharing_radius;              /* Радиус sharing */

    /* Остановка */
    int max_generations;                /* Максимум поколений */
    double target_fitness;              /* Целевой fitness */
    int stagnation_limit;               /* Поколений без улучшения для остановки */

    /* Logging */
    int log_every_n_generations;
    int verbose;
} KolibriEvoConfig;

/** Статистика поколения */
typedef struct {
    int generation;
    double best_fitness;
    double avg_fitness;
    double worst_fitness;
    double std_fitness;
    int num_species;
    double diversity;                   /* Генетическое разнообразие */
    double elapsed_seconds;
} KolibriEvoGenerationStats;

/** Состояние эволюции */
typedef struct {
    KolibriEvoConfig config;
    KolibriEvoFormula population[KET_MAX_POPULATION];
    KolibriEvoSpecies species[KET_MAX_SPECIES];
    int num_species;

    KolibriEvoGenerationStats stats;
    int current_generation;
    double best_fitness_ever;
    int best_formula_index;
    int generations_without_improvement;

    /* История */
    KolibriEvoGenerationStats history[1000];
    int history_size;

    /* RNG */
    uint64_t rng_state;

    /* Timing */
    double start_time;
} KolibriEvoTrainer;

/* Callback для прогресса */
typedef void (*KolibriEvoProgressCallback)(
    int generation, double best_fitness, double avg_fitness, void *user_data
);

/* Fitness function type */
typedef double (*KolibriEvoFitnessFunc)(
    const uint8_t *formula, int formula_size, void *data
);

/* ============================================================================
 * API: ИНИЦИАЛИЗАЦИЯ
 * ============================================================================ */

/**
 * Инициализировать эволюционный trainer
 *
 * @param trainer   Trainer (output)
 * @param config    Конфигурация
 * @param seed      RNG seed
 * @return 0 на успех
 */
int kolibri_evo_init(KolibriEvoTrainer *trainer,
                     const KolibriEvoConfig *config,
                     uint64_t seed);

/**
 * Освободить ресурсы trainer
 */
void kolibri_evo_free(KolibriEvoTrainer *trainer);

/* ============================================================================
 * API: ЭВОЛЮЦИЯ
 * ============================================================================ */

/**
 * Запустить эволюцию на N поколений
 *
 * @param trainer       Trainer
 * @param generations   Количество поколений
 * @param fitness_func  Функция fitness (опционально, если NULL - fitness не вычисляется)
 * @param fitness_data  Данные для fitness функции
 * @param progress      Callback (опционально)
 * @param user_data     Данные для callback
 * @return 0 на успех
 */
int kolibri_evo_run(KolibriEvoTrainer *trainer,
                    int generations,
                    KolibriEvoFitnessFunc fitness_func,
                    void *fitness_data,
                    KolibriEvoProgressCallback progress,
                    void *user_data);

/**
 * Один шаг эволюции (одно поколение)
 *
 * @param trainer   Trainer
 * @return 0 на успех
 */
int kolibri_evo_step(KolibriEvoTrainer *trainer);

/**
 * Вычислить fitness для всей популяции
 *
 * @param trainer   Trainer
 * @param evaluate  Функция оценки fitness (formula, data -> score)
 * @param data      Данные для оценки
 * @return 0 на успех
 */
int kolibri_evo_evaluate_fitness(KolibriEvoTrainer *trainer,
                                  KolibriEvoFitnessFunc func,
                                  void *data);

/* ============================================================================
 * API: ОПЕРАТОРЫ ЭВОЛЮЦИИ
 * ============================================================================ */

/**
 * Tournament selection
 *
 * @param trainer       Trainer
 * @param tournament_size  Размер турнира
 * @return Индекс выбранной формулы
 */
int kolibri_evo_tournament_select(KolibriEvoTrainer *trainer,
                                  int tournament_size);

/**
 * Crossover двух формул
 *
 * @param trainer   Trainer
 * @param parent1   Индекс родителя 1
 * @param parent2   Индекс родителя 2
 * @param child     Индекс ребёнка (output)
 * @return 0 на успех
 */
int kolibri_evo_crossover(KolibriEvoTrainer *trainer,
                          int parent1, int parent2,
                          int child);

/**
 * Мутация формулы
 *
 * @param trainer       Trainer
 * @param formula_idx   Индекс формулы
 * @param mutation_type Тип мутации
 * @return 0 на успех
 */
int kolibri_evo_mutate(KolibriEvoTrainer *trainer,
                       int formula_idx,
                       KolibriEvoMutationType mutation_type);

/* ============================================================================
 * API: SPECIATION
 * ============================================================================ */

/**
 * Разделить популяцию на виды
 *
 * @param trainer   Trainer
 * @return 0 на успех
 */
int kolibri_evo_speciate(KolibriEvoTrainer *trainer);

/**
 * Вычислить расстояние между двумя формулами
 *
 * @param f1    Формула 1
 * @param f2    Формула 2
 * @param size  Размер формулы
 * @return Нормализованное расстояние (0.0-1.0)
 */
double kolibri_evo_formula_distance(const uint8_t *f1,
                                    const uint8_t *f2,
                                    int size);

/* ============================================================================
 * API: СТАТИСТИКА
 * ============================================================================ */

/**
 * Получить лучшую формулу
 *
 * @param trainer   Trainer
 * @param formula   Буфер для формулы (KET_FORMULA_SIZE байт)
 * @return Fitness лучшей формулы
 */
double kolibri_evo_get_best_formula(const KolibriEvoTrainer *trainer,
                                     uint8_t *formula);

/**
 * Распечатать статистику
 */
void kolibri_evo_print_stats(const KolibriEvoTrainer *trainer);

/**
 * Сохранить статистику в файл
 *
 * @param trainer   Trainer
 * @param filepath  Путь к файлу
 * @return 0 на успех
 */
int kolibri_evo_save_stats(const KolibriEvoTrainer *trainer,
                           const char *filepath);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_EVOLUTIONARY_TRAINER_H */
