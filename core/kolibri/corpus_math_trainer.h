/*
 * corpus_math_trainer.h
 *
 * Training Pipeline для математического обучения Numeric Transformer
 *
 * Возможности:
 *   - Загрузка математических датасетов (GSM8K, MATH, синтетические)
 *   - Batch training с gradient accumulation
 *   - Validation и early stopping
 *   - Progress tracking и логирование
 *   - Сохранение/загрузка чекпоинтов
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_CORPUS_MATH_TRAINER_H
#define KOLIBRI_CORPUS_MATH_TRAINER_H

#include "kolibri/attention.h"
#include "kolibri/kat_train_backprop.h"
#include "kolibri/numeric_tokenizer.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНСТАНТЫ
 * ============================================================================ */

/** Максимальный размер датасета (для тестов) */
#define KCMT_MAX_SAMPLES 1000

/** Максимальная длина последовательности */
#define KCMT_MAX_SEQ_LEN 512

/** Максимальный размер батча */
#define KCMT_MAX_BATCH_SIZE 64

/** Максимальное количество эпох */
#define KCMT_MAX_EPOCHS 1000

/** Размер валидационной выборки (%) */
#define KCMT_VAL_SPLIT 10

/* ============================================================================
 * ТИПЫ ДАННЫХ
 * ============================================================================ */

/** Тип датасета */
typedef enum {
    KCMT_DATASET_GSM8K = 0,       /* Grade school math (8.5K) */
    KCMT_DATASET_MATH = 1,        /* Competition math (12.5K) */
    KCMT_DATASET_AIME = 2,        /* AIME problems */
    KCMT_DATASET_SYNTHETIC = 3,   /* Синтетические примеры */
    KCMT_DATASET_MIXED = 4,       /* Смешанный датасет */
    KCMT_DATASET_UNKNOWN = -1
} KolibriDatasetType;

/** Один пример для обучения */
typedef struct {
    char question[1024];      /* Вопрос/задача */
    char answer[1024];        /* Ответ с решением */
    uint8_t input_tokens[KCMT_MAX_SEQ_LEN];   /* Входные токены */
    uint8_t target_tokens[KCMT_MAX_SEQ_LEN];  /* Целевые токены */
    int input_len;            /* Длина входа */
    int target_len;           /* Длина выхода */
    int difficulty;           /* Сложность (1-5) */
} KolibriMathSample;

/** Полный датасет */
typedef struct {
    KolibriDatasetType type;
    KolibriMathSample samples[KCMT_MAX_SAMPLES];
    int num_samples;
    int num_train;           /* Размер training split */
    int num_val;             /* Размер validation split */

    /* Статистика */
    double avg_input_len;
    double avg_target_len;
    double avg_difficulty;

    /* Мета-информация */
    char name[256];
    char description[512];
} KolibriMathDataset;

/** Конфигурация training pipeline */
typedef struct {
    /* Training параметры */
    int batch_size;           /* Размер батча */
    int gradient_accumulation; /* Накопление градиентов */
    int max_epochs;           /* Максимальное количество эпох */
    int eval_every_n_steps;   /* Оценка каждые N шагов */
    int save_every_n_epochs;  /* Сохранение каждые N эпох */

    /* Early stopping */
    int early_stopping_patience;  /* Эпох без улучшения */
    double early_stopping_min_delta; /* Минимальное улучшение */
    int early_stopping_enabled;

    /* Learning rate */
    float warmup_ratio;       /* Доля warmup (0.0-1.0) */
    float max_lr;             /* Максимальный learning rate */
    float min_lr;             /* Минимальный learning rate */

    /* Logging */
    int log_every_n_steps;
    int verbose;

    /* Пути */
    char checkpoint_dir[512];
    char log_file[512];
} KolibriTrainerConfig;

/** Метрики обучения */
typedef struct {
    int epoch;
    int step;
    float train_loss;
    float val_loss;
    float train_accuracy;
    float val_accuracy;
    float lr;
    double elapsed_seconds;
    int samples_processed;
} KolibriTrainingMetrics;

/** Состояние trainer */
typedef struct {
    KolibriTrainerConfig config;
    KolibriTrainingMetrics metrics;

    /* Внутреннее состояние */
    KatModel *model;
    KatWorkspace *workspace;
    KatAdamWState adamw;
    KolibriTokenizer tokenizer;

    int best_epoch;
    float best_val_loss;
    int epochs_without_improvement;

    long total_steps;
    long total_samples;
    double start_time;
} KolibriMathTrainer;

/* Callback для progress */
typedef void (*KolibriProgressCallback)(
    int epoch, int step, float loss, void *user_data
);

/* ============================================================================
 * API: DATASET
 * ============================================================================ */

/**
 * Создать синтетический датасет математических примеров
 *
 * @param dataset   Датасет (output)
 * @param num_samples  Количество примеров
 * @param difficulty   Сложность (1-5)
 * @return 0 на успех
 */
int kolibri_generate_synthetic_dataset(
    KolibriMathDataset *dataset,
    int num_samples,
    int difficulty
);

/**
 * Загрузить датасет из JSON файла (GSM8K, MATH формат)
 *
 * @param dataset   Датасет (output)
 * @param filepath  Путь к файлу
 * @param type      Тип датасета
 * @return 0 на успех
 */
int kolibri_load_dataset(
    KolibriMathDataset *dataset,
    const char *filepath,
    KolibriDatasetType type
);

/**
 * Разделить датасет на train/val
 *
 * @param dataset   Датасет
 * @param val_ratio Доля validation (0.0-1.0)
 */
void kolibri_split_dataset(KolibriMathDataset *dataset, double val_ratio);

/**
 * Перемешать датасет
 *
 * @param dataset   Датасет
 * @param seed      Seed для shuffle
 */
void kolibri_shuffle_dataset(KolibriMathDataset *dataset, uint64_t seed);

/**
 * Токенизировать все примеры в датасете
 *
 * @param dataset    Датасет
 * @param tokenizer  Токенизатор
 * @return Количество успешно токенизированных примеров
 */
int kolibri_tokenize_dataset(
    KolibriMathDataset *dataset,
    KolibriTokenizer *tokenizer
);

/* ============================================================================
 * API: TRAINER
 * ============================================================================ */

/**
 * Инициализировать trainer
 *
 * @param trainer   Trainer (output)
 * @param model     Модель для обучения
 * @param config    Конфигурация
 * @return 0 на успех
 */
int kolibri_trainer_init(
    KolibriMathTrainer *trainer,
    KatModel *model,
    const KolibriTrainerConfig *config
);

/**
 * Освободить ресурсы trainer
 */
void kolibri_trainer_free(KolibriMathTrainer *trainer);

/**
 * Обучить модель на датасете
 *
 * @param trainer   Trainer
 * @param dataset   Датасет для обучения
 * @param progress  Callback для прогресса (опционально)
 * @param user_data Данные для callback
 * @return 0 на успех, код ошибки иначе
 */
int kolibri_train(
    KolibriMathTrainer *trainer,
    const KolibriMathDataset *dataset,
    KolibriProgressCallback progress,
    void *user_data
);

/**
 * Оценить модель на validation set
 *
 * @param trainer   Trainer
 * @param dataset   Validation датасет
 * @param val_loss  Result: validation loss
 * @return 0 на успех
 */
int kolibri_evaluate(
    const KolibriMathTrainer *trainer,
    const KolibriMathDataset *dataset,
    float *val_loss
);

/**
 * Сохранить чекпоинт
 *
 * @param trainer   Trainer
 * @param filepath  Путь к файлу
 * @return 0 на успех
 */
int kolibri_save_checkpoint(
    const KolibriMathTrainer *trainer,
    const char *filepath
);

/**
 * Загрузить чекпоинт
 *
 * @param trainer   Trainer
 * @param filepath  Путь к файлу
 * @return 0 на успех
 */
int kolibri_load_checkpoint(
    KolibriMathTrainer *trainer,
    const char *filepath
);

/**
 * Распечатать статус обучения
 */
void kolibri_print_training_status(const KolibriMathTrainer *trainer);

/* ============================================================================
 * API: BENCHMARK
 * ============================================================================ */

/**
 * Запустить benchmark на датасете
 *
 * @param model     Модель
 * @param dataset   Тестовый датасет
 * @param accuracy  Result: точность (0.0-1.0)
 * @param avg_loss  Result: средний loss
 * @param elapsed   Result: время в секундах
 * @return 0 на успех
 */
int kolibri_benchmark(
    const KatModel *model,
    const KolibriMathDataset *dataset,
    double *accuracy,
    double *avg_loss,
    double *elapsed
);

/**
 * Сравнить производительность с baseline
 *
 * @param accuracy  Текущая точность
 * @param baseline  Baseline точность
 * @param output    Буфер для вывода
 * @param size      Размер буфера
 */
void kolibri_compare_with_baseline(
    double accuracy, double baseline,
    char *output, size_t size
);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_CORPUS_MATH_TRAINER_H */
