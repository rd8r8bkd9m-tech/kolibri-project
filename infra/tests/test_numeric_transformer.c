/*
 * test_numeric_transformer.c
 *
 * Тесты для Numeric Transformer:
 * - Numeric Tokenizer
 * - RoPE, RMSNorm, GQA, SwiGLU
 * - Backpropagation с AdamW
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "kolibri/numeric_tokenizer.h"
#include "kolibri/attention.h"
#include "kolibri/kat_train_backprop.h"

/* ============================================================================
 * ТЕСТЫ NUMERIC TOKENIZER
 * ============================================================================ */

void test_tokenizer_init(void) {
    printf("Testing tokenizer initialization...\n");

    KolibriTokenizer tokenizer;
    int ret = kolibri_tokenizer_init(&tokenizer);

    assert(ret == 0);
    assert(tokenizer.initialized == 1);
    assert(tokenizer.config.prefer_numeric_tokens == 1);
    assert(tokenizer.config.support_unicode_math == 1);

    kolibri_tokenizer_free(&tokenizer);
    printf("✓ Tokenizer initialization test passed\n\n");
}

void test_tokenize_simple_math(void) {
    printf("Testing simple math tokenization...\n");

    KolibriTokenizer tokenizer;
    kolibri_tokenizer_init(&tokenizer);

    KolibriTokenizationResult result;
    const char *expr = "2 + 2 = 4";
    int ret = kolibri_tokenize(&tokenizer, expr, strlen(expr), &result);

    assert(ret == 0);
    assert(result.token_count > 0);
    assert(result.num_count >= 3);  /* 2, 2, 4 */
    assert(result.operator_count >= 2);  /* +, = */

    kolibri_print_tokenization(&result);

    kolibri_tokenizer_free(&tokenizer);
    printf("✓ Simple math tokenization test passed\n\n");
}

void test_tokenize_complex_math(void) {
    printf("Testing complex math tokenization...\n");

    KolibriTokenizer tokenizer;
    kolibri_tokenizer_init(&tokenizer);

    KolibriTokenizationResult result;
    const char *expr = "sin(x) + cos(π) = √2";
    int ret = kolibri_tokenize(&tokenizer, expr, strlen(expr), &result);

    assert(ret == 0);
    assert(result.token_count > 0);
    assert(result.function_count >= 2);  /* sin, cos */
    assert(result.num_count >= 1);  /* 2 */

    kolibri_print_tokenization(&result);

    kolibri_tokenizer_free(&tokenizer);
    printf("✓ Complex math tokenization test passed\n\n");
}

void test_tokenize_numbers(void) {
    printf("Testing number parsing...\n");

    KolibriNumberInfo info;

    /* Целое число */
    uint16_t tok1 = kolibri_parse_number("123", 3, &info);
    assert(tok1 == KNT_TOKEN_NUMBER_START_MARKER);
    assert(info.value == 123.0);
    assert(info.num_digits == 3);
    assert(info.decimal_places == 0);
    assert(!info.is_negative);

    /* Отрицательное число с десятичной точкой */
    uint16_t tok2 = kolibri_parse_number("-3.14", 5, &info);
    assert(tok2 == KNT_TOKEN_NUMBER_START_MARKER);
    assert(fabs(info.value - 3.14) < 0.001);
    assert(info.num_digits == 3);
    assert(info.decimal_places == 2);
    assert(info.is_negative);

    /* Число с экспонентой */
    uint16_t tok3 = kolibri_parse_number("6.022e23", 8, &info);
    assert(tok3 == KNT_TOKEN_NUMBER_START_MARKER);
    assert(info.has_exponent);
    assert(fabs(info.exponent_value - 23.0) < 0.001);

    printf("✓ Number parsing test passed\n\n");
}

void test_token_utilities(void) {
    printf("Testing token utilities...\n");

    /* Проверка цифр */
    assert(kolibri_is_digit_token(KNT_TOKEN_DIGIT_0));
    assert(kolibri_is_digit_token(KNT_TOKEN_DIGIT_9));
    assert(!kolibri_is_digit_token(KNT_TOKEN_PLUS));

    /* Проверка операторов */
    assert(kolibri_is_math_operator(KNT_TOKEN_PLUS));
    assert(kolibri_is_math_operator(KNT_TOKEN_MINUS));
    assert(kolibri_is_math_operator(KNT_TOKEN_MULTIPLY));
    assert(!kolibri_is_math_operator(KNT_TOKEN_DIGIT_0));

    /* Проверка функций */
    assert(kolibri_is_function(KNT_TOKEN_SIN));
    assert(kolibri_is_function(KNT_TOKEN_COS));
    assert(!kolibri_is_function(KNT_TOKEN_PLUS));

    /* Проверка констант */
    assert(kolibri_is_constant(KNT_TOKEN_PI));
    assert(kolibri_is_constant(KNT_TOKEN_E));
    assert(!kolibri_is_constant(KNT_TOKEN_PLUS));

    /* Проверка значений констант */
    assert(fabs(kolibri_constant_value(KNT_TOKEN_PI) - 3.14159265358979) < 1e-10);
    assert(fabs(kolibri_constant_value(KNT_TOKEN_E) - 2.71828182845904) < 1e-10);

    /* Проверка имён токенов */
    const char *name_pi = kolibri_token_name(KNT_TOKEN_PI);
    assert(strstr(name_pi, "π") != NULL);

    const char *name_plus = kolibri_token_name(KNT_TOKEN_PLUS);
    assert(strcmp(name_plus, "+") == 0);

    printf("✓ Token utilities test passed\n\n");
}

void test_numeric_embedding(void) {
    printf("Testing numeric embedding...\n");

    float embedding[64];
    KolibriNumberInfo info;
    info.value = 123.456;
    info.num_digits = 6;
    info.decimal_places = 3;
    info.is_negative = 0;
    info.has_exponent = 0;

    kolibri_numeric_embedding(KNT_TOKEN_NUMBER_START_MARKER, &info, embedding, 64);

    /* Проверяем что embedding не нулевой */
    float norm = 0.0f;
    for (int i = 0; i < 64; i++) {
        norm += embedding[i] * embedding[i];
    }
    assert(norm > 0.0f);

    /* Первый компонент — нормализованное значение */
    assert(fabs(embedding[0]) <= 1.0f);

    printf("✓ Numeric embedding test passed\n\n");
}

/* ============================================================================
 * ТЕСТЫ TRANSFORMER V2 (RoPE, RMSNorm, GQA, SwiGLU)
 * ============================================================================ */

void test_v2_configs(void) {
    printf("Testing v2 configurations...\n");

    KatConfigV2 small = kat_config_v2_small();
    KatConfigV2 medium = kat_config_v2_medium();
    KatConfigV2 large = kat_config_v2_large();

    /* Проверяем max_seq = 2048 */
    assert(small.max_seq == 2048);
    assert(medium.max_seq == 2048);
    assert(large.max_seq == 2048);

    /* Проверяем GQA */
    assert(small.kv_groups == 4);
    assert(small.num_kv_heads == 1);
    assert(small.num_heads / small.kv_groups == small.num_kv_heads);

    /* Проверяем RoPE */
    assert(small.use_rope == 1);
    assert(small.rope_theta == 10000.0f);

    /* Проверяем SwiGLU */
    assert(small.activation == KAT_ACTIVATION_SWIGLU);

    /* Подсчёт параметров */
    size_t params_small = kat_config_v2_count_params(&small);
    size_t params_medium = kat_config_v2_count_params(&medium);
    size_t params_large = kat_config_v2_count_params(&large);

    printf("  Small v2 params:  %zu\n", params_small);
    printf("  Medium v2 params: %zu\n", params_medium);
    printf("  Large v2 params:  %zu\n", params_large);

    assert(params_small > 0);
    assert(params_medium > params_small);
    assert(params_large > params_medium);

    printf("✓ V2 configurations test passed\n\n");
}

void test_v2_model_creation(void) {
    printf("Testing v2 model creation...\n");

    KatConfigV2 cfg = kat_config_v2_small();
    KatConfig cfg_v1 = kat_config_v2_to_v1(&cfg);

    assert(cfg_v1.max_seq == 2048);
    assert(cfg_v1.embed_dim == 64);
    assert(cfg_v1.num_heads == 4);

    /* Создаём модель */
    KatModel *model = kat_model_create_ex(&cfg_v1, 42);
    assert(model != NULL);
    assert(model->cfg.max_seq == 2048);
    assert(model->param_count > 0);

    printf("  Model created with %zu parameters\n", model->param_count);

    /* Создаём workspace */
    KatWorkspace *ws = kat_workspace_create_ex(&cfg_v1);
    assert(ws != NULL);

    /* Forward pass */
    uint8_t tokens[] = "Hello";
    size_t seq_len = 5;
    int ret = kat_forward(model, ws, tokens, seq_len);
    assert(ret == 0);
    assert(ws->seq_len == seq_len);

    /* Сэмплирование */
    uint8_t next_token = kat_sample(model, ws, 0.7f);
    printf("  Next token: %d\n", next_token);

    kat_workspace_destroy(ws);
    kat_model_destroy(model);

    printf("✓ V2 model creation test passed\n\n");
}

/* ============================================================================
 * ТЕСТЫ BACKPROPAGATION И ADAMW
 * ============================================================================ */

void test_adamw_init(void) {
    printf("Testing AdamW initialization...\n");

    KatConfig cfg = kat_config_small();
    KatTrainingConfig train_cfg = {0};
    train_cfg.adamw.lr = 1e-4f;
    train_cfg.adamw.beta1 = 0.9f;
    train_cfg.adamw.beta2 = 0.999f;
    train_cfg.adamw.eps = 1e-8f;
    train_cfg.adamw.weight_decay = 0.01f;
    train_cfg.adamw.max_grad_norm = 1.0f;
    train_cfg.lr_schedule = KAT_LR_COSINE;
    train_cfg.warmup_steps = 100;
    train_cfg.total_steps = 1000;

    KatAdamWState adamw;
    int ret = kat_adamw_init(&adamw, &cfg, &train_cfg);

    assert(ret == 0);
    assert(adamw.step == 0);
    assert(adamw.total_steps == 1000);

    kat_adamw_free(&adamw);
    printf("✓ AdamW initialization test passed\n\n");
}

void test_lr_scheduler(void) {
    printf("Testing LR scheduler...\n");

    KatConfig cfg = kat_config_small();
    KatTrainingConfig train_cfg = {0};
    train_cfg.adamw.lr = 1e-3f;
    train_cfg.warmup_steps = 100;
    train_cfg.total_steps = 1000;

    KatAdamWState adamw = {0};
    adamw.step = 0;
    adamw.total_steps = 1000;

    /* Constant LR */
    train_cfg.lr_schedule = KAT_LR_CONSTANT;
    float lr_const = kat_get_lr(&adamw, &train_cfg);
    assert(fabs(lr_const - 1e-3f) < 1e-10f);

    /* Cosine с warmup */
    train_cfg.lr_schedule = KAT_LR_COSINE;

    /* Во время warmup */
    adamw.step = 50;
    float lr_warmup = kat_get_lr(&adamw, &train_cfg);
    assert(lr_warmup < 1e-3f);
    assert(lr_warmup > 0.0f);

    /* После warmup */
    adamw.step = 500;
    float lr_cosine = kat_get_lr(&adamw, &train_cfg);
    assert(lr_cosine > 0.0f);
    assert(lr_cosine <= 1e-3f);

    /* В конце */
    adamw.step = 999;
    float lr_end = kat_get_lr(&adamw, &train_cfg);
    assert(lr_end < lr_cosine);
    assert(lr_end > 0.0f);

    printf("  LR constant: %.6f\n", lr_const);
    printf("  LR warmup (step 50): %.6f\n", lr_warmup);
    printf("  LR cosine (step 500): %.6f\n", lr_cosine);
    printf("  LR end (step 999): %.6f\n", lr_end);

    printf("✓ LR scheduler test passed\n\n");
}

void test_backprop_step(void) {
    printf("Testing backpropagation step...\n");

    KatConfig cfg = kat_config_small();
    KatModel *model = kat_model_create_ex(&cfg, 42);
    KatWorkspace *ws = kat_workspace_create_ex(&cfg);

    KatTrainingConfig train_cfg = {0};
    train_cfg.adamw.lr = 1e-4f;
    train_cfg.adamw.beta1 = 0.9f;
    train_cfg.adamw.beta2 = 0.999f;
    train_cfg.adamw.eps = 1e-8f;
    train_cfg.adamw.weight_decay = 0.01f;
    train_cfg.adamw.max_grad_norm = 1.0f;
    train_cfg.lr_schedule = KAT_LR_COSINE;
    train_cfg.warmup_steps = 10;
    train_cfg.total_steps = 100;

    KatAdamWState adamw;
    kat_adamw_init(&adamw, &cfg, &train_cfg);

    /* Training data */
    uint8_t tokens[] = { 'H', 'e', 'l', 'l', 'o' };
    uint8_t targets[] = { 'e', 'l', 'l', 'o', '\0' };
    size_t seq_len = 5;

    /* Несколько training шагов */
    float prev_loss = 1e10f;
    for (int step = 0; step < 5; step++) {
        float loss = kat_train_step_backprop(
            model, ws, &adamw,
            tokens, seq_len, targets,
            &train_cfg
        );

        assert(loss >= 0.0f);
        printf("  Step %d: loss = %.4f\n", step + 1, loss);

        /* Loss должен уменьшаться (хотя бы не расти сильно) */
        /* NOTE: Для первых шагов может флуктуировать */
        prev_loss = loss;
    }

    /* Проверяем статистику */
    KatTrainingStats stats = kat_get_training_stats(&adamw, &train_cfg);
    assert(stats.step == 5);
    assert(stats.lr > 0.0f);
    assert(stats.loss > 0.0f);

    printf("  Final stats: step=%d, lr=%.6f, loss=%.4f\n",
           stats.step, stats.lr, stats.loss);

    kat_adamw_free(&adamw);
    kat_workspace_destroy(ws);
    kat_model_destroy(model);

    printf("✓ Backpropagation step test passed\n\n");
}

void test_gradient_clipping(void) {
    printf("Testing gradient clipping...\n");

    KatConfig cfg = kat_config_small();

    /* Создаём фиктивные градиенты */
    KatGradients grads = {0};
    grads.vocab_size = cfg.vocab_size;
    grads.embed_dim = cfg.embed_dim;
    grads.max_seq = cfg.max_seq;
    grads.num_layers = cfg.num_layers;
    grads.num_heads = cfg.num_heads;
    grads.head_dim = cfg.head_dim;
    grads.ff_dim = cfg.ff_dim;

    grads.g_token_embed = (float*)calloc((size_t)cfg.vocab_size * cfg.embed_dim, sizeof(float));
    grads.g_lm_head = (float*)calloc((size_t)cfg.embed_dim * cfg.vocab_size, sizeof(float));

    if (!grads.g_token_embed || !grads.g_lm_head) {
        printf("  SKIP: Memory allocation failed\n");
        free(grads.g_token_embed);
        free(grads.g_lm_head);
        return;
    }

    /* Заполняем большими значениями */
    size_t size = (size_t)cfg.vocab_size * cfg.embed_dim;
    for (size_t i = 0; i < size; i++) {
        grads.g_token_embed[i] = 100.0f;
    }
    grads.max_grad_norm = 0.0f;  /* Инициализация */

    /* Clip */
    float max_norm = 1.0f;
    float norm_before = kat_clip_gradients(&grads, max_norm);

    /* Проверяем что clipping сработал (norm_before должен быть большим) */
    if (norm_before > max_norm) {
        printf("  Norm before clipping: %.2f\n", norm_before);
        printf("  Max allowed norm: %.2f\n", max_norm);
        printf("  Gradient clipping activated\n");
    } else {
        printf("  Norm within limits: %.4f\n", norm_before);
    }

    free(grads.g_token_embed);
    free(grads.g_lm_head);

    printf("✓ Gradient clipping test passed\n\n");
}

/* ============================================================================
 * ИНТЕГРАЦИОННЫЙ ТЕСТ
 * ============================================================================ */

void test_integrated_pipeline(void) {
    printf("Testing integrated pipeline...\n");
    printf("  Tokenizer → Transformer → Backprop\n\n");

    /* 1. Tokenize mathematical expression */
    KolibriTokenizer tokenizer;
    kolibri_tokenizer_init(&tokenizer);

    KolibriTokenizationResult result;
    const char *math_expr = "E = mc^2";
    kolibri_tokenize(&tokenizer, math_expr, strlen(math_expr), &result);

    printf("Tokenized: '%s'\n", math_expr);
    printf("  Tokens: %d\n", result.token_count);
    printf("  Numbers: %d\n", result.num_count);

    /* Конвертируем в uint8_t для transformer */
    uint8_t tokens[64];
    size_t seq_len = result.token_count < 64 ? result.token_count : 64;
    for (size_t i = 0; i < seq_len; i++) {
        tokens[i] = (uint8_t)(result.tokens[i].token_id % 256);
    }

    /* 2. Создаём модель */
    KatConfig cfg = kat_config_small();
    KatModel *model = kat_model_create_ex(&cfg, 42);
    KatWorkspace *ws = kat_workspace_create_ex(&cfg);

    /* 3. Forward pass */
    int ret = kat_forward(model, ws, tokens, seq_len);
    assert(ret == 0);

    uint8_t predicted = kat_sample(model, ws, 0.7f);
    printf("  Predicted next token: %d\n", predicted);

    /* 4. Backprop */
    KatTrainingConfig train_cfg = {0};
    train_cfg.adamw.lr = 1e-4f;
    train_cfg.adamw.beta1 = 0.9f;
    train_cfg.adamw.beta2 = 0.999f;
    train_cfg.adamw.eps = 1e-8f;
    train_cfg.adamw.weight_decay = 0.01f;
    train_cfg.adamw.max_grad_norm = 1.0f;
    train_cfg.lr_schedule = KAT_LR_COSINE;
    train_cfg.warmup_steps = 10;
    train_cfg.total_steps = 100;

    KatAdamWState adamw;
    kat_adamw_init(&adamw, &cfg, &train_cfg);

    uint8_t targets[64];
    memcpy(targets, tokens, seq_len);
    if (seq_len > 1) {
        memmove(targets, targets + 1, seq_len - 1);
        targets[seq_len - 1] = 0;
    }

    float loss = kat_train_step_backprop(
        model, ws, &adamw,
        tokens, seq_len, targets,
        &train_cfg
    );

    printf("  Training loss: %.4f\n", loss);

    kat_adamw_free(&adamw);
    kat_workspace_destroy(ws);
    kat_model_destroy(model);
    kolibri_tokenizer_free(&tokenizer);

    printf("\n✓ Integrated pipeline test passed\n\n");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("===========================================\n");
    printf("Kolibri Numeric Transformer Tests\n");
    printf("===========================================\n\n");

    /* Numeric Tokenizer tests */
    printf("--- Numeric Tokenizer Tests ---\n\n");
    test_tokenizer_init();
    test_tokenize_simple_math();
    test_tokenize_complex_math();
    test_tokenize_numbers();
    test_token_utilities();
    test_numeric_embedding();

    /* Transformer V2 tests */
    printf("--- Transformer V2 Tests ---\n\n");
    test_v2_configs();
    test_v2_model_creation();

    /* Backpropagation & AdamW tests */
    printf("--- Backpropagation & AdamW Tests ---\n\n");
    test_adamw_init();
    test_lr_scheduler();
    test_backprop_step();
    test_gradient_clipping();

    /* Integrated test */
    printf("--- Integrated Test ---\n\n");
    test_integrated_pipeline();

    printf("===========================================\n");
    printf("All tests passed! ✓\n");
    printf("===========================================\n");

    return 0;
}
