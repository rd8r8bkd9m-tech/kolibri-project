/*
 * test_corpus_math_trainer.c
 *
 * Тесты для Training Pipeline
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "kolibri/corpus_math_trainer.h"
#include "kolibri/attention.h"
#include "kolibri/math_solver.h"

/* ============================================================================
 * ТЕСТЫ ДАТАСЕТА
 * ============================================================================ */

void test_synthetic_dataset_generation(void) {
    printf("Testing synthetic dataset generation...\n");

    /* Уменьшаем размер для теста */
    KolibriMathDataset dataset;
    int ret = kolibri_generate_synthetic_dataset(&dataset, 50, 2);

    assert(ret == 0);
    assert(dataset.num_samples == 50);
    assert(dataset.type == KCMT_DATASET_SYNTHETIC);
    assert(dataset.avg_difficulty == 2.0);

    /* Проверяем что примеры разнообразны */
    int has_arithmetic = 0;
    int has_linear = 0;
    int has_quadratic = 0;
    int has_percentage = 0;

    for (int i = 0; i < 50; i++) {
        const char *q = dataset.samples[i].question;
        if (strstr(q, "What is") && (strstr(q, "+") || strstr(q, "-") || strstr(q, "*"))) {
            has_arithmetic = 1;
        }
        if (strstr(q, "Solve:") && strstr(q, "x")) {
            if (strstr(q, "x²") || strstr(q, "x^2")) {
                has_quadratic = 1;
            } else {
                has_linear = 1;
            }
        }
        if (strstr(q, "%")) {
            has_percentage = 1;
        }
    }

    printf("  Dataset stats:\n");
    printf("    Samples: %d\n", dataset.num_samples);
    printf("    Avg input len: %.1f\n", dataset.avg_input_len);
    printf("    Avg target len: %.1f\n", dataset.avg_target_len);
    printf("    Has arithmetic: %s\n", has_arithmetic ? "yes" : "no");
    printf("    Has linear: %s\n", has_linear ? "yes" : "no");
    printf("    Has quadratic: %s\n", has_quadratic ? "yes" : "no");
    printf("    Has percentage: %s\n", has_percentage ? "yes" : "no");

    /* Проверяем наличие разных типов */
    assert(has_arithmetic || has_linear || has_quadratic);

    printf("✓ Synthetic dataset generation test passed\n\n");
}

void test_dataset_tokenize(void) {
    printf("Testing dataset tokenization...\n");

    KolibriMathDataset dataset;
    kolibri_generate_synthetic_dataset(&dataset, 50, 2);

    KolibriTokenizer tokenizer;
    kolibri_tokenizer_init(&tokenizer);

    int tokenized = kolibri_tokenize_dataset(&dataset, &tokenizer);

    printf("  Tokenized: %d / %d\n", tokenized, dataset.num_samples);
    assert(tokenized > 0);

    /* Проверяем что токены корректны */
    for (int i = 0; i < tokenized && i < 10; i++) {
        assert(dataset.samples[i].input_len > 0);
        assert(dataset.samples[i].target_len > 0);
    }

    kolibri_tokenizer_free(&tokenizer);
    printf("✓ Dataset tokenization test passed\n\n");
}

void test_dataset_shuffle_split(void) {
    printf("Testing dataset shuffle and split...\n");

    KolibriMathDataset dataset;
    kolibri_generate_synthetic_dataset(&dataset, 100, 2);

    /* Сохраняем порядок до shuffle */
    char first_q_before[64];
    strncpy(first_q_before, dataset.samples[0].question, sizeof(first_q_before));

    /* Shuffle */
    kolibri_shuffle_dataset(&dataset, 42);

    /* Проверяем что порядок изменился */
    int order_changed = (strncmp(first_q_before, dataset.samples[0].question, 64) != 0);
    printf("  Order changed after shuffle: %s\n", order_changed ? "yes" : "no");

    /* Split */
    kolibri_split_dataset(&dataset, 0.2);
    printf("  Train: %d, Val: %d\n", dataset.num_train, dataset.num_val);

    assert(dataset.num_train + dataset.num_val == 100);
    assert(dataset.num_train > 0);
    assert(dataset.num_val > 0);

    printf("✓ Dataset shuffle and split test passed\n\n");
}

/* ============================================================================
 * ТЕСТЫ TRAINER
 * ============================================================================ */

void test_trainer_init(void) {
    printf("Testing trainer initialization...\n");

    KatModel *model = kat_model_create(42);
    KolibriTrainerConfig config = {0};
    config.batch_size = 4;
    config.gradient_accumulation = 1;
    config.max_epochs = 3;
    config.max_lr = 1e-4f;
    config.warmup_ratio = 0.1f;
    config.eval_every_n_steps = 10;
    config.save_every_n_epochs = 1;
    config.early_stopping_enabled = 0;
    config.verbose = 0;

    KolibriMathTrainer trainer;
    int ret = kolibri_trainer_init(&trainer, model, &config);

    assert(ret == 0);
    assert(trainer.model == model);
    assert(trainer.workspace != NULL);
    assert(trainer.best_val_loss > 1e9f);

    printf("✓ Trainer initialization test passed\n\n");

    kolibri_trainer_free(&trainer);
    kat_model_destroy(model);
}

/* Callback для training */
static void training_callback(int epoch, int step, float loss, void *user_data) {
    if (epoch == 1 && step == 1) {
        *(float*)user_data = loss;
    }
    printf("    [Callback] Epoch %d, Step %d: loss = %.4f\n", epoch, step, loss);
}

void test_training_loop(void) {
    printf("Testing training loop...\n");

    /* Создаём модель и датасет */
    KatModel *model = kat_model_create(42);
    KolibriMathDataset dataset;
    kolibri_generate_synthetic_dataset(&dataset, 32, 2);

    /* Токенизируем */
    KolibriTokenizer tokenizer;
    kolibri_tokenizer_init(&tokenizer);
    kolibri_tokenize_dataset(&dataset, &tokenizer);

    /* Инициализируем trainer */
    KolibriTrainerConfig config = {0};
    config.batch_size = 4;
    config.gradient_accumulation = 1;
    config.max_epochs = 3;
    config.max_lr = 1e-4f;
    config.warmup_ratio = 0.1f;
    config.log_every_n_steps = 1;
    config.early_stopping_enabled = 0;
    config.verbose = 1;

    KolibriMathTrainer trainer;
    int ret = kolibri_trainer_init(&trainer, model, &config);
    assert(ret == 0);

    /* Обучаем */
    float initial_loss = 0.0f;

    ret = kolibri_train(&trainer, &dataset, training_callback, &initial_loss);

    assert(ret == 0);
    assert(trainer.total_steps > 0);
    assert(trainer.total_samples > 0);

    printf("\n  Training summary:\n");
    printf("    Initial loss: %.4f\n", initial_loss);
    printf("    Final loss: %.4f\n", trainer.metrics.train_loss);
    printf("    Total steps: %ld\n", trainer.total_steps);
    printf("    Total samples: %ld\n", trainer.total_samples);

    kolibri_trainer_free(&trainer);
    kolibri_tokenizer_free(&tokenizer);
    kat_model_destroy(model);

    printf("✓ Training loop test passed\n\n");
}

void test_evaluate(void) {
    printf("Testing evaluation...\n");

    KatModel *model = kat_model_create(42);
    KolibriMathDataset dataset;
    kolibri_generate_synthetic_dataset(&dataset, 50, 2);

    /* Токенизируем */
    KolibriTokenizer tokenizer;
    kolibri_tokenizer_init(&tokenizer);
    kolibri_tokenize_dataset(&dataset, &tokenizer);

    kolibri_split_dataset(&dataset, 0.2);

    /* Инициализируем trainer */
    KolibriTrainerConfig config = {0};
    config.batch_size = 4;
    config.max_epochs = 1;
    config.max_lr = 1e-4f;
    config.verbose = 0;

    KolibriMathTrainer trainer;
    kolibri_trainer_init(&trainer, model, &config);

    /* Оцениваем */
    float val_loss = 0.0f;
    int ret = kolibri_evaluate(&trainer, &dataset, &val_loss);

    assert(ret == 0);
    assert(val_loss > 0.0f);

    printf("  Validation loss: %.4f\n", val_loss);

    kolibri_trainer_free(&trainer);
    kolibri_tokenizer_free(&tokenizer);
    kat_model_destroy(model);

    printf("✓ Evaluation test passed\n\n");
}

void test_checkpoint_save_load(void) {
    printf("Testing checkpoint save/load...\n");

    KatModel *model = kat_model_create(42);
    KolibriTrainerConfig config = {0};
    config.batch_size = 4;
    config.max_epochs = 1;
    config.max_lr = 1e-4f;
    config.verbose = 0;
    snprintf(config.checkpoint_dir, sizeof(config.checkpoint_dir), "/tmp");

    KolibriMathTrainer trainer;
    kolibri_trainer_init(&trainer, model, &config);

    /* Сохраняем чекпоинт */
    const char *checkpoint_path = "/tmp/kolibri_test_checkpoint.klm";
    int ret = kolibri_save_checkpoint(&trainer, checkpoint_path);
    assert(ret == 0);

    /* Проверяем что файл создан */
    FILE *f = fopen(checkpoint_path, "rb");
    assert(f != NULL);
    fclose(f);

    /* Загружаем чекпоинт */
    ret = kolibri_load_checkpoint(&trainer, checkpoint_path);
    assert(ret == 0);

    printf("  Checkpoint saved and loaded successfully\n");

    /* Удаляем временный файл */
    remove(checkpoint_path);

    kolibri_trainer_free(&trainer);
    kat_model_destroy(model);

    printf("✓ Checkpoint save/load test passed\n\n");
}

/* ============================================================================
 * ТЕСТЫ BENCHMARK
 * ============================================================================ */

void test_benchmark(void) {
    printf("Testing benchmark...\n");

    KatModel *model = kat_model_create(42);
    KolibriMathDataset dataset;
    kolibri_generate_synthetic_dataset(&dataset, 50, 2);

    /* Токенизируем */
    KolibriTokenizer tokenizer;
    kolibri_tokenizer_init(&tokenizer);
    kolibri_tokenize_dataset(&dataset, &tokenizer);

    /* Запускаем benchmark */
    double accuracy = 0.0;
    double avg_loss = 0.0;
    double elapsed = 0.0;

    int ret = kolibri_benchmark(model, &dataset, &accuracy, &avg_loss, &elapsed);

    assert(ret == 0);
    assert(accuracy >= 0.0 && accuracy <= 1.0);
    assert(avg_loss > 0.0);
    assert(elapsed >= 0.0);

    printf("  Benchmark results:\n");
    printf("    Accuracy: %.2f%%\n", accuracy * 100.0);
    printf("    Avg loss: %.4f\n", avg_loss);
    printf("    Elapsed: %.3fs\n", elapsed);
    printf("    Samples/sec: %.1f\n", 50.0 / elapsed);

    /* Сравниваем с baseline */
    char comparison[256];
    kolibri_compare_with_baseline(accuracy, 0.1, comparison, sizeof(comparison));
    printf("    vs baseline: %s\n", comparison);

    kolibri_tokenizer_free(&tokenizer);
    kat_model_destroy(model);

    printf("✓ Benchmark test passed\n\n");
}

/* ============================================================================
 * ИНТЕГРАЦИОННЫЙ ТЕСТ
 * ============================================================================ */

void test_integrated_training_pipeline(void) {
    printf("Testing integrated training pipeline...\n");

    /* 1. Генерируем датасет */
    printf("  1. Generating dataset...\n");
    KolibriMathDataset dataset;
    kolibri_generate_synthetic_dataset(&dataset, 100, 2);
    printf("    Generated %d samples\n", dataset.num_samples);

    /* 2. Токенизируем */
    printf("  2. Tokenizing...\n");
    KolibriTokenizer tokenizer;
    kolibri_tokenizer_init(&tokenizer);
    int tokenized = kolibri_tokenize_dataset(&dataset, &tokenizer);
    printf("    Tokenized %d / %d\n", tokenized, dataset.num_samples);

    /* 3. Создаём модель */
    printf("  3. Creating model...\n");
    KatModel *model = kat_model_create(42);
    printf("    Model params: %zu\n", model->param_count);

    /* 4. Инициализируем trainer */
    printf("  4. Initializing trainer...\n");
    KolibriTrainerConfig config = {0};
    config.batch_size = 8;
    config.max_epochs = 2;
    config.max_lr = 1e-4f;
    config.warmup_ratio = 0.1f;
    config.log_every_n_steps = 1;
    config.verbose = 1;

    KolibriMathTrainer trainer;
    kolibri_trainer_init(&trainer, model, &config);

    /* 5. Benchmark до обучения */
    printf("  5. Pre-training benchmark...\n");
    double acc_before, loss_before, time_before;
    kolibri_benchmark(model, &dataset, &acc_before, &loss_before, &time_before);
    printf("    Accuracy: %.2f%%, Loss: %.4f\n", acc_before * 100.0, loss_before);

    /* 6. Обучаем */
    printf("  6. Training...\n");
    kolibri_train(&trainer, &dataset, NULL, NULL);

    /* 7. Benchmark после обучения */
    printf("  7. Post-training benchmark...\n");
    double acc_after, loss_after, time_after;
    kolibri_benchmark(model, &dataset, &acc_after, &loss_after, &time_after);
    printf("    Accuracy: %.2f%%, Loss: %.4f\n", acc_after * 100.0, loss_after);

    /* 8. Сравнение */
    printf("  8. Comparison:\n");
    printf("    Accuracy: %.2f%% -> %.2f%% (%+.2f%%)\n",
           acc_before * 100.0, acc_after * 100.0,
           (acc_after - acc_before) * 100.0);
    printf("    Loss: %.4f -> %.4f (%+.4f)\n",
           loss_before, loss_after, loss_after - loss_before);

    char comparison[256];
    kolibri_compare_with_baseline(acc_after, acc_before, comparison, sizeof(comparison));
    printf("    Result: %s\n", comparison);

    /* 9. Сохраняем чекпоинт */
    printf("  9. Saving checkpoint...\n");
    kolibri_save_checkpoint(&trainer, "/tmp/kolibri_final.klm");

    kolibri_trainer_free(&trainer);
    kolibri_tokenizer_free(&tokenizer);
    kat_model_destroy(model);
    remove("/tmp/kolibri_final.klm");

    printf("\n✓ Integrated training pipeline test passed\n\n");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("===========================================\n");
    printf("Kolibri Training Pipeline Tests\n");
    printf("===========================================\n\n");

    /* Датасет тесты */
    printf("--- Dataset Tests ---\n\n");
    test_synthetic_dataset_generation();
    test_dataset_tokenize();
    test_dataset_shuffle_split();

    /* Trainer тесты */
    printf("--- Trainer Tests ---\n\n");
    test_trainer_init();
    test_training_loop();
    test_evaluate();
    test_checkpoint_save_load();

    /* Benchmark тесты */
    printf("--- Benchmark Tests ---\n\n");
    test_benchmark();

    /* Интеграционный тест */
    printf("--- Integrated Test ---\n\n");
    test_integrated_training_pipeline();

    printf("===========================================\n");
    printf("All tests passed! ✓\n");
    printf("===========================================\n");

    return 0;
}
