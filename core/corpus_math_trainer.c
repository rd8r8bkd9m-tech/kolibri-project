/*
 * corpus_math_trainer.c
 *
 * Training Pipeline для математического обучения Numeric Transformer
 *
 * Реализует:
 *   - Генерацию синтетических математических примеров
 *   - Batch training с gradient accumulation
 *   - Validation и early stopping
 *   - Сохранение/загрузка чекпоинтов
 *   - Benchmarking
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/corpus_math_trainer.h"
#include "kolibri/math_solver.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * ВНУТРЕННИЕ ФУНКЦИИ
 * ============================================================================ */

/** Простой PRNG */
static uint64_t trainer_rng_state = 12345;

static uint64_t trainer_rand(void) {
    trainer_rng_state ^= trainer_rng_state << 13;
    trainer_rng_state ^= trainer_rng_state >> 7;
    trainer_rng_state ^= trainer_rng_state << 17;
    return trainer_rng_state;
}

static int trainer_rand_int(int min, int max) {
    return min + (int)(trainer_rand() % (uint64_t)(max - min + 1));
}

static double trainer_rand_double(void) {
    return (double)(trainer_rand() & 0x7FFFFFFF) / (double)0x7FFFFFFF;
}

/** Получить текущее время в секундах */
static double get_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ============================================================================
 * ГЕНЕРАЦИЯ СИНТЕТИЧЕСКОГО ДАТАСЕТА
 * ============================================================================ */

int kolibri_generate_synthetic_dataset(
    KolibriMathDataset *dataset,
    int num_samples,
    int difficulty
) {
    if (!dataset || num_samples <= 0 || num_samples > KCMT_MAX_SAMPLES) {
        return -1;
    }

    memset(dataset, 0, sizeof(KolibriMathDataset));
    dataset->type = KCMT_DATASET_SYNTHETIC;
    snprintf(dataset->name, sizeof(dataset->name),
             "synthetic_d%d", difficulty);
    snprintf(dataset->description, sizeof(dataset->description),
             "Synthetic math dataset, difficulty %d", difficulty);

    double total_input_len = 0;
    double total_target_len = 0;
    double total_difficulty = 0;

    for (int i = 0; i < num_samples; i++) {
        KolibriMathSample *sample = &dataset->samples[i];
        sample->difficulty = difficulty;

        /* Генери разные типы примеров в зависимости от сложности */
        int example_type = trainer_rand_int(0, 3);

        if (example_type == 0) {
            /* Арифметика: a + b, a - b, a * b */
            int a = trainer_rand_int(1, 10 * difficulty);
            int b = trainer_rand_int(1, 10 * difficulty);
            int op = trainer_rand_int(0, 2);

            if (op == 0) {
                snprintf(sample->question, sizeof(sample->question),
                        "What is %d + %d?", a, b);
                snprintf(sample->answer, sizeof(sample->answer),
                        "%d + %d = %d", a, b, a + b);
            } else if (op == 1) {
                if (a < b) { int tmp = a; a = b; b = tmp; }
                snprintf(sample->question, sizeof(sample->question),
                        "What is %d - %d?", a, b);
                snprintf(sample->answer, sizeof(sample->answer),
                        "%d - %d = %d", a, b, a - b);
            } else {
                a = trainer_rand_int(1, 5 * difficulty);
                b = trainer_rand_int(1, 5 * difficulty);
                snprintf(sample->question, sizeof(sample->question),
                        "What is %d * %d?", a, b);
                snprintf(sample->answer, sizeof(sample->answer),
                        "%d * %d = %d", a, b, a * b);
            }

        } else if (example_type == 1) {
            /* Линейные уравнения: ax + b = c */
            int x = trainer_rand_int(1, 10);
            int a = trainer_rand_int(1, 5);
            int b = trainer_rand_int(1, 20);
            int c = a * x + b;

            snprintf(sample->question, sizeof(sample->question),
                    "Solve: %dx + %d = %d", a, b, c);
            snprintf(sample->answer, sizeof(sample->answer),
                    "%dx + %d = %d => x = %d", a, b, c, x);

        } else if (example_type == 2) {
            /* Квадратные уравнения: x² - (r1+r2)x + r1*r2 = 0 */
            int r1 = trainer_rand_int(1, 10);
            int r2 = trainer_rand_int(1, 10);
            int b_coef = -(r1 + r2);
            int c_coef = r1 * r2;

            snprintf(sample->question, sizeof(sample->question),
                    "Solve: x² %+dx %+d = 0", b_coef, c_coef);
            snprintf(sample->answer, sizeof(sample->answer),
                    "x² %+dx %+d = 0 => x₁ = %d, x₂ = %d",
                    b_coef, c_coef, r1, r2);

        } else {
            /* Проценты: X% от Y */
            int percent = trainer_rand_int(1, 100);
            int value = trainer_rand_int(10, 1000);
            double result = (double)percent * value / 100.0;

            snprintf(sample->question, sizeof(sample->question),
                    "What is %d%% of %d?", percent, value);
            snprintf(sample->answer, sizeof(sample->answer),
                    "%d%% of %d = %.2f", percent, value, result);
        }

        sample->input_len = (int)strlen(sample->question);
        sample->target_len = (int)strlen(sample->answer);
        total_input_len += sample->input_len;
        total_target_len += sample->target_len;
        total_difficulty += sample->difficulty;
    }

    dataset->num_samples = num_samples;
    dataset->avg_input_len = total_input_len / num_samples;
    dataset->avg_target_len = total_target_len / num_samples;
    dataset->avg_difficulty = total_difficulty / num_samples;
    dataset->num_train = num_samples;
    dataset->num_val = 0;

    return 0;
}

/* ============================================================================
 * ЗАГРУЗКА ДАТАСЕТА
 * ============================================================================ */

int kolibri_load_dataset(
    KolibriMathDataset *dataset,
    const char *filepath,
    KolibriDatasetType type
) {
    if (!dataset || !filepath) return -1;

    /* В полноценной версии здесь будет парсинг JSON */
    /* Сейчас используем заглушку */
    (void)type;
    (void)filepath;

    fprintf(stderr, "Warning: Dataset loading from file not yet implemented\n");
    fprintf(stderr, "Using synthetic dataset instead\n");

    return kolibri_generate_synthetic_dataset(dataset, 1000, 2);
}

void kolibri_split_dataset(KolibriMathDataset *dataset, double val_ratio) {
    if (!dataset || val_ratio < 0.0 || val_ratio > 1.0) return;

    int n = dataset->num_samples;
    int n_val = (int)(n * val_ratio);
    int n_train = n - n_val;

    /* Перемещаем validation примеры в конец */
    /* В простой реализации просто устанавливаем указатели */
    dataset->num_train = n_train;
    dataset->num_val = n_val;
}

void kolibri_shuffle_dataset(KolibriMathDataset *dataset, uint64_t seed) {
    if (!dataset) return;

    trainer_rng_state = seed;
    int n = dataset->num_samples;

    /* Fisher-Yates shuffle */
    for (int i = n - 1; i > 0; i--) {
        int j = trainer_rand_int(0, i);
        KolibriMathSample tmp = dataset->samples[i];
        dataset->samples[i] = dataset->samples[j];
        dataset->samples[j] = tmp;
    }
}

int kolibri_tokenize_dataset(
    KolibriMathDataset *dataset,
    KolibriTokenizer *tokenizer
) {
    if (!dataset || !tokenizer) return -1;

    int success_count = 0;

    for (int i = 0; i < dataset->num_samples; i++) {
        KolibriMathSample *sample = &dataset->samples[i];

        /* Токенизируем вопрос */
        KolibriTokenizationResult q_result;
        int ret_q = kolibri_tokenize(tokenizer,
                                     sample->question,
                                     strlen(sample->question),
                                     &q_result);

        /* Токенизируем ответ */
        KolibriTokenizationResult a_result;
        int ret_a = kolibri_tokenize(tokenizer,
                                     sample->answer,
                                     strlen(sample->answer),
                                     &a_result);

        if (ret_q == 0 && ret_a == 0 &&
            q_result.token_count > 0 && a_result.token_count > 0) {
            /* Копируем токены (конвертируем в uint8_t для transformer) */
            int len = q_result.token_count < KCMT_MAX_SEQ_LEN ?
                     q_result.token_count : KCMT_MAX_SEQ_LEN;
            for (int j = 0; j < len; j++) {
                sample->input_tokens[j] = (uint8_t)(q_result.tokens[j].token_id % 256);
            }
            sample->input_len = len;

            len = a_result.token_count < KCMT_MAX_SEQ_LEN ?
                 a_result.token_count : KCMT_MAX_SEQ_LEN;
            for (int j = 0; j < len; j++) {
                sample->target_tokens[j] = (uint8_t)(a_result.tokens[j].token_id % 256);
            }
            sample->target_len = len;

            success_count++;
        }
    }

    return success_count;
}

/* ============================================================================
 * TRAINER
 * ============================================================================ */

int kolibri_trainer_init(
    KolibriMathTrainer *trainer,
    KatModel *model,
    const KolibriTrainerConfig *config
) {
    if (!trainer || !model || !config) return -1;

    memset(trainer, 0, sizeof(KolibriMathTrainer));
    trainer->config = *config;
    trainer->model = model;

    /* Инициализируем workspace */
    trainer->workspace = kat_workspace_create_ex(&model->cfg);
    if (!trainer->workspace) return -2;

    /* Инициализируем AdamW */
    KatTrainingConfig train_cfg = {0};
    train_cfg.adamw.lr = config->max_lr;
    train_cfg.adamw.beta1 = 0.9f;
    train_cfg.adamw.beta2 = 0.999f;
    train_cfg.adamw.eps = 1e-8f;
    train_cfg.adamw.weight_decay = 0.01f;
    train_cfg.adamw.max_grad_norm = 1.0f;
    train_cfg.lr_schedule = KAT_LR_COSINE;
    train_cfg.warmup_steps = (int)(config->max_epochs * config->warmup_ratio *
                                   config->batch_size);
    train_cfg.total_steps = config->max_epochs * config->batch_size;
    train_cfg.gradient_accumulation = config->gradient_accumulation;

    int ret = kat_adamw_init(&trainer->adamw, &model->cfg, &train_cfg);
    if (ret != 0) {
        kat_workspace_destroy(trainer->workspace);
        return -3;
    }

    /* Инициализируем tokenizer */
    kolibri_tokenizer_init(&trainer->tokenizer);

    trainer->best_val_loss = 1e10f;
    trainer->start_time = get_time_seconds();

    return 0;
}

void kolibri_trainer_free(KolibriMathTrainer *trainer) {
    if (!trainer) return;

    kat_workspace_destroy(trainer->workspace);
    kat_adamw_free(&trainer->adamw);
    kolibri_tokenizer_free(&trainer->tokenizer);

    memset(trainer, 0, sizeof(KolibriMathTrainer));
}

/* ============================================================================
 * ОСНОВНОЙ ЦИКЛ ОБУЧЕНИЯ
 * ============================================================================ */

int kolibri_train(
    KolibriMathTrainer *trainer,
    const KolibriMathDataset *dataset,
    KolibriProgressCallback progress,
    void *user_data
) {
    if (!trainer || !dataset) return -1;

    const KolibriTrainerConfig *cfg = &trainer->config;
    int n_train = dataset->num_train > 0 ? dataset->num_train : dataset->num_samples;
    int batch_size = cfg->batch_size;

    if (batch_size <= 0 || batch_size > KCMT_MAX_BATCH_SIZE) {
        batch_size = 1;
    }

    printf("========================================\n");
    printf("Starting Training\n");
    printf("========================================\n");
    printf("Samples: %d\n", n_train);
    printf("Batch size: %d\n", batch_size);
    printf("Max epochs: %d\n", cfg->max_epochs);
    printf("Learning rate: %.6f\n", cfg->max_lr);
    printf("========================================\n\n");

    for (int epoch = 0; epoch < cfg->max_epochs; epoch++) {
        /* Shuffle данные для каждой эпохи */
        kolibri_shuffle_dataset((KolibriMathDataset*)dataset,
                               (uint64_t)epoch * 12345);

        float epoch_loss = 0.0f;
        int steps = 0;

        /* Batch training */
        for (int batch_start = 0; batch_start < n_train;
             batch_start += batch_size) {

            int batch_end = batch_start + batch_size;
            if (batch_end > n_train) batch_end = n_train;

            float batch_loss = 0.0f;
            int batch_count = 0;

            /* Gradient accumulation */
            for (int i = batch_start; i < batch_end; i++) {
                const KolibriMathSample *sample = &dataset->samples[i];

                if (sample->input_len <= 1 || sample->target_len <= 1) continue;

                /* Training step: input → predict target */
                KatTrainingConfig train_cfg;
                memset(&train_cfg, 0, sizeof(train_cfg));
                train_cfg.adamw.lr = cfg->max_lr;
                train_cfg.adamw.beta1 = 0.9f;
                train_cfg.adamw.beta2 = 0.999f;
                train_cfg.adamw.eps = 1e-8f;
                train_cfg.adamw.weight_decay = 0.01f;
                train_cfg.adamw.max_grad_norm = 1.0f;
                train_cfg.lr_schedule = KAT_LR_COSINE;
                train_cfg.warmup_steps = trainer->adamw.total_steps / 10;
                train_cfg.total_steps = trainer->adamw.total_steps;

                float lr = kat_get_lr(&trainer->adamw, &train_cfg);
                train_cfg.adamw.lr = lr;

                float loss = kat_train_step_backprop(
                    trainer->model,
                    trainer->workspace,
                    &trainer->adamw,
                    sample->input_tokens,
                    (size_t)sample->input_len,
                    sample->target_tokens,
                    &train_cfg
                );

                if (loss >= 0.0f) {
                    batch_loss += loss;
                    batch_count++;
                }
            }

            if (batch_count > 0) {
                epoch_loss += batch_loss / batch_count;
                steps++;
                trainer->total_steps++;
                trainer->total_samples += batch_count;
            }

            /* Logging */
            if (cfg->verbose && steps > 0 &&
                steps % cfg->log_every_n_steps == 0) {
                float avg_loss = epoch_loss / steps;
                printf("  Epoch %d, Step %d: loss = %.4f\n",
                       epoch + 1, steps, avg_loss);
            }

            /* Callback */
            if (progress && steps > 0) {
                progress(epoch + 1, steps, epoch_loss / steps, user_data);
            }
        }

        /* Эпоха завершена */
        float avg_epoch_loss = steps > 0 ? epoch_loss / steps : 0.0f;
        trainer->metrics.epoch = epoch + 1;
        trainer->metrics.step = steps;
        trainer->metrics.train_loss = avg_epoch_loss;

        KatTrainingConfig eval_cfg;
        memset(&eval_cfg, 0, sizeof(eval_cfg));
        eval_cfg.adamw.lr = cfg->max_lr;
        eval_cfg.lr_schedule = KAT_LR_COSINE;
        eval_cfg.warmup_steps = trainer->adamw.total_steps / 10;
        eval_cfg.total_steps = trainer->adamw.total_steps;

        trainer->metrics.lr = kat_get_lr(&trainer->adamw, &eval_cfg);
        trainer->metrics.elapsed_seconds = get_time_seconds() - trainer->start_time;
        trainer->metrics.samples_processed = trainer->total_samples;

        printf("Epoch %d/%d: loss = %.4f, lr = %.6f, time = %.1fs\n",
               epoch + 1, cfg->max_epochs,
               avg_epoch_loss,
               trainer->metrics.lr,
               trainer->metrics.elapsed_seconds);

        /* Early stopping check */
        if (cfg->early_stopping_enabled) {
            if (avg_epoch_loss < trainer->best_val_loss - cfg->early_stopping_min_delta) {
                trainer->best_val_loss = avg_epoch_loss;
                trainer->best_epoch = epoch + 1;
                trainer->epochs_without_improvement = 0;
            } else {
                trainer->epochs_without_improvement++;
                if (trainer->epochs_without_improvement >= cfg->early_stopping_patience) {
                    printf("\nEarly stopping at epoch %d\n", epoch + 1);
                    break;
                }
            }
        }

        /* Save checkpoint */
        if (cfg->save_every_n_epochs > 0 &&
            (epoch + 1) % cfg->save_every_n_epochs == 0) {
            char checkpoint_path[512];
            snprintf(checkpoint_path, sizeof(checkpoint_path),
                    "%s/epoch_%d.klm", cfg->checkpoint_dir, epoch + 1);
            kolibri_save_checkpoint(trainer, checkpoint_path);
        }
    }

    printf("\n========================================\n");
    printf("Training Complete\n");
    printf("Best epoch: %d (loss = %.4f)\n",
           trainer->best_epoch, trainer->best_val_loss);
    printf("Total steps: %ld\n", trainer->total_steps);
    printf("Total samples: %ld\n", trainer->total_samples);
    printf("========================================\n");

    return 0;
}

/* ============================================================================
 * EVALUATION
 * ============================================================================ */

int kolibri_evaluate(
    const KolibriMathTrainer *trainer,
    const KolibriMathDataset *dataset,
    float *val_loss
) {
    if (!trainer || !dataset || !val_loss) return -1;

    int n_val = dataset->num_val > 0 ? dataset->num_val :
               (dataset->num_samples / 10);

    if (n_val <= 0) n_val = dataset->num_samples / 10;
    if (n_val <= 0) return -2;

    float total_loss = 0.0f;
    int count = 0;

    /* Оцениваем на последних n_val примерах */
    int start_idx = dataset->num_samples - n_val;

    for (int i = start_idx; i < dataset->num_samples; i++) {
        const KolibriMathSample *sample = &dataset->samples[i];

        if (sample->input_len <= 1) continue;

        /* Forward pass */
        kat_forward(trainer->model, (KatWorkspace*)trainer->workspace,
                   sample->input_tokens, (size_t)sample->input_len);

        /* Вычисляем loss (cross-entropy) */
        /* Упрощённо: используем вероятность правильного токена */
        float prob = 0.0f;
        if (sample->target_len > 0 && trainer->workspace->probs) {
            uint8_t target = sample->target_tokens[0];
            prob = trainer->workspace->probs[target % 256];
        }

        /* Cross-entropy: -log(prob) */
        if (prob < 1e-10f) prob = 1e-10f;
        total_loss += -logf(prob);
        count++;
    }

    *val_loss = count > 0 ? total_loss / count : 1e10f;
    return 0;
}

/* ============================================================================
 * CHECKPOINTS
 * ============================================================================ */

int kolibri_save_checkpoint(
    const KolibriMathTrainer *trainer,
    const char *filepath
) {
    if (!trainer || !filepath) return -1;

    /* Сохраняем модель */
    size_t buf_size = kat_config_count_params(&trainer->model->cfg) * sizeof(float) + 1024;
    uint8_t *buf = (uint8_t*)malloc(buf_size);
    if (!buf) return -2;

    size_t written = kat_serialize(trainer->model, buf, buf_size);

    FILE *f = fopen(filepath, "wb");
    if (!f) {
        free(buf);
        return -3;
    }

    /* Записываем метаданные */
    fprintf(f, "# Kolibri Checkpoint\n");
    fprintf(f, "epoch: %d\n", trainer->metrics.epoch);
    fprintf(f, "loss: %.6f\n", trainer->metrics.train_loss);
    fprintf(f, "steps: %ld\n", trainer->total_steps);
    fprintf(f, "---\n");

    /* Записываем модель */
    fwrite(buf, 1, written, f);
    fclose(f);
    free(buf);

    if (trainer->config.verbose) {
        printf("  Saved checkpoint: %s (%zu bytes)\n", filepath, written);
    }

    return 0;
}

int kolibri_load_checkpoint(
    KolibriMathTrainer *trainer,
    const char *filepath
) {
    if (!trainer || !filepath) return -1;

    FILE *f = fopen(filepath, "rb");
    if (!f) return -2;

    /* Пропускаем метаданные */
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '-' && line[1] == '-' && line[2] == '-' && line[3] == '\n') {
            break;
        }
    }

    /* Загружаем модель */
    size_t buf_size = kat_config_count_params(&trainer->model->cfg) * sizeof(float) + 1024;
    uint8_t *buf = (uint8_t*)malloc(buf_size);
    if (!buf) {
        fclose(f);
        return -3;
    }

    size_t read = fread(buf, 1, buf_size, f);
    fclose(f);

    int ret = kat_deserialize(trainer->model, buf, read);
    free(buf);

    return ret;
}

void kolibri_print_training_status(const KolibriMathTrainer *trainer) {
    if (!trainer) return;

    printf("\n=== Training Status ===\n");
    printf("Epoch: %d\n", trainer->metrics.epoch);
    printf("Step: %d\n", trainer->metrics.step);
    printf("Train Loss: %.4f\n", trainer->metrics.train_loss);
    printf("Val Loss: %.4f\n", trainer->metrics.val_loss);
    printf("LR: %.6f\n", trainer->metrics.lr);
    printf("Best Epoch: %d (loss = %.4f)\n",
           trainer->best_epoch, trainer->best_val_loss);
    printf("Epochs without improvement: %d\n",
           trainer->epochs_without_improvement);
    printf("Total samples processed: %ld\n", trainer->total_samples);
    printf("Elapsed time: %.1fs\n", trainer->metrics.elapsed_seconds);
    printf("========================\n");
}

/* ============================================================================
 * BENCHMARK
 * ============================================================================ */

int kolibri_benchmark(
    const KatModel *model,
    const KolibriMathDataset *dataset,
    double *accuracy,
    double *avg_loss,
    double *elapsed
) {
    if (!model || !dataset) return -1;

    int n = dataset->num_samples;
    double total_loss = 0.0;
    int correct = 0;

    double start_time = get_time_seconds();

    KatWorkspace *ws = kat_workspace_create_ex(&model->cfg);
    if (!ws) return -2;

    for (int i = 0; i < n; i++) {
        const KolibriMathSample *sample = &dataset->samples[i];

        if (sample->input_len <= 1) continue;

        /* Forward pass */
        int ret = kat_forward(model, ws,
                             sample->input_tokens,
                             (size_t)sample->input_len);

        if (ret == 0 && sample->target_len > 0) {
            /* Сэмплируем следующий токен */
            uint8_t predicted = kat_sample((KatModel*)model, ws, 0.7f);
            uint8_t expected = sample->target_tokens[0];

            if (predicted == expected) {
                correct++;
            }

            /* Вычисляем loss */
            float prob = ws->probs[expected % 256];
            if (prob < 1e-10f) prob = 1e-10f;
            total_loss += -logf(prob);
        }
    }

    kat_workspace_destroy(ws);

    double end_time = get_time_seconds();

    int count = n;  /* Упрощённо */
    *accuracy = count > 0 ? (double)correct / count : 0.0;
    *avg_loss = count > 0 ? total_loss / count : 1e10;
    *elapsed = end_time - start_time;

    return 0;
}

void kolibri_compare_with_baseline(
    double accuracy, double baseline,
    char *output, size_t size
) {
    if (!output || size == 0) return;

    double diff = accuracy - baseline;
    double pct_improvement = baseline > 0 ? (diff / baseline) * 100.0 : 0.0;

    if (diff > 0.01) {
        snprintf(output, size,
                "✓ Better than baseline by %.2f%% (%.4f vs %.4f)",
                pct_improvement, accuracy, baseline);
    } else if (diff < -0.01) {
        snprintf(output, size,
                "✗ Worse than baseline by %.2f%% (%.4f vs %.4f)",
                -pct_improvement, accuracy, baseline);
    } else {
        snprintf(output, size,
                "≈ Similar to baseline (%.4f vs %.4f)",
                accuracy, baseline);
    }
}
