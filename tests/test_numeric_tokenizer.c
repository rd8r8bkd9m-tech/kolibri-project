/*
 * test_numeric_tokenizer.c
 *
 * Модульные тесты для Kolibri Numeric Tokenizer
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "kolibri/numeric_tokenizer.h"

/* ============================================================================
 * ТЕСТ ИНИЦИАЛИЗАЦИИ
 * ============================================================================ */

void test_tokenizer_init(void) {
    printf("Testing tokenizer initialization...\n");

    KolibriTokenizer tok;
    int ret = kolibri_tokenizer_init(&tok);
    assert(ret == 0);
    assert(tok.initialized == 1);

    KolibriTokenizerConfig config = {0};
    config.prefer_numeric_tokens = 1;
    config.encode_numbers_specially = 1;
    config.max_number_digits = 16;
    config.support_unicode_math = 1;

    KolibriTokenizer tok2;
    ret = kolibri_tokenizer_init_ex(&tok2, &config);
    assert(ret == 0);
    assert(tok2.initialized == 1);
    assert(tok2.config.prefer_numeric_tokens == 1);
    assert(tok2.config.support_unicode_math == 1);

    kolibri_tokenizer_free(&tok);
    kolibri_tokenizer_free(&tok2);

    printf("✓ Initialization test passed\n\n");
}

/* ============================================================================
 * ТЕСТ VOCAB SIZE
 * ============================================================================ */

void test_vocab_size(void) {
    printf("Testing vocab size...\n");

    int size = kolibri_vocab_size();
    assert(size == 2560);  /* KNT_VOCAB_EXTENDED */
    printf("  Vocab size: %d\n", size);

    printf("✓ Vocab size test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ТОКЕНИЗАЦИИ ПРОСТОГО ВЫРАЖЕНИЯ
 * ============================================================================ */

void test_tokenize_simple_expression(void) {
    printf("Testing simple expression tokenization...\n");

    KolibriTokenizer tok;
    kolibri_tokenizer_init(&tok);

    KolibriTokenizationResult result;
    int ret = kolibri_tokenize(&tok, "2+3", 3, &result);

    assert(ret == 0);
    assert(result.token_count > 0);
    assert(result.error_code == 0);

    printf("  Input: '2+3', Tokens: %d\n", result.token_count);

    kolibri_tokenizer_free(&tok);
    printf("✓ Simple expression test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ТОКЕНИЗАЦИИ МАТЕМАТИЧЕСКОГО ВЫРАЖЕНИЯ
 * ============================================================================ */

void test_tokenize_math_expression(void) {
    printf("Testing math expression tokenization...\n");

    KolibriTokenizer tok;
    kolibri_tokenizer_init(&tok);

    KolibriTokenizationResult result;
    int ret = kolibri_tokenize_math(&tok, "sin(x) + cos(y) = 1", &result);

    assert(ret == 0);
    assert(result.token_count > 0);
    assert(result.function_count >= 2);  /* sin и cos */

    printf("  Input: 'sin(x) + cos(y) = 1', Tokens: %d, Functions: %d\n",
           result.token_count, result.function_count);

    kolibri_tokenizer_free(&tok);
    printf("✓ Math expression test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ЦИФРОВЫХ ТОКЕНОВ
 * ============================================================================ */

void test_digit_tokens(void) {
    printf("Testing digit token recognition...\n");

    /* Проверяем что все цифровые токены имеют правильные ID */
    assert(kolibri_is_digit_token(KNT_TOKEN_DIGIT_0) == 1);
    assert(kolibri_is_digit_token(KNT_TOKEN_DIGIT_1) == 1);
    assert(kolibri_is_digit_token(KNT_TOKEN_DIGIT_5) == 1);
    assert(kolibri_is_digit_token(KNT_TOKEN_DIGIT_9) == 1);

    /* Проверяем что не-цифры не распознаются как цифры */
    assert(kolibri_is_digit_token(KNT_TOKEN_PLUS) == 0);
    assert(kolibri_is_digit_token(KNT_TOKEN_PI) == 0);

    printf("  Digit tokens: 0-9 recognized correctly\n");
    printf("✓ Digit tokens test passed\n\n");
}

/* ============================================================================
 * ТЕСТ МАТЕМАТИЧЕСКИХ ОПЕРАТОРОВ
 * ============================================================================ */

void test_math_operators(void) {
    printf("Testing math operator recognition...\n");

    /* Операторы */
    assert(kolibri_is_math_operator(KNT_TOKEN_PLUS) == 1);
    assert(kolibri_is_math_operator(KNT_TOKEN_MINUS) == 1);
    assert(kolibri_is_math_operator(KNT_TOKEN_MULTIPLY) == 1);
    assert(kolibri_is_math_operator(KNT_TOKEN_DIVIDE) == 1);
    assert(kolibri_is_math_operator(KNT_TOKEN_EQUALS) == 1);
    assert(kolibri_is_math_operator(KNT_TOKEN_SQRT) == 1);
    assert(kolibri_is_math_operator(KNT_TOKEN_INTEGRAL) == 1);
    assert(kolibri_is_math_operator(KNT_TOKEN_SUMMATION) == 1);

    /* Не операторы */
    assert(kolibri_is_math_operator(KNT_TOKEN_DIGIT_0) == 0);
    assert(kolibri_is_math_operator(KNT_TOKEN_SIN) == 0);
    assert(kolibri_is_math_operator(KNT_TOKEN_PI) == 0);

    printf("  +, -, ×, ÷, =, √, ∫, ∑ recognized as operators\n");
    printf("✓ Math operators test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ФУНКЦИЙ
 * ============================================================================ */

void test_functions(void) {
    printf("Testing function recognition...\n");

    assert(kolibri_is_function(KNT_TOKEN_SIN) == 1);
    assert(kolibri_is_function(KNT_TOKEN_COS) == 1);
    assert(kolibri_is_function(KNT_TOKEN_TAN) == 1);
    assert(kolibri_is_function(KNT_TOKEN_LOG) == 1);
    assert(kolibri_is_function(KNT_TOKEN_LN) == 1);
    assert(kolibri_is_function(KNT_TOKEN_ABS) == 1);
    assert(kolibri_is_function(KNT_TOKEN_EXP_FUNC) == 1);

    /* Не функции */
    assert(kolibri_is_function(KNT_TOKEN_PLUS) == 0);
    assert(kolibri_is_function(KNT_TOKEN_DIGIT_0) == 0);

    printf("  sin, cos, tan, log, ln, abs, exp recognized as functions\n");
    printf("✓ Functions test passed\n\n");
}

/* ============================================================================
 * ТЕСТ КОНСТАНТ
 * ============================================================================ */

void test_constants(void) {
    printf("Testing constant recognition and values...\n");

    assert(kolibri_is_constant(KNT_TOKEN_PI) == 1);
    assert(kolibri_is_constant(KNT_TOKEN_E) == 1);
    assert(kolibri_is_constant(KNT_TOKEN_PHI) == 1);

    /* Проверяем значения констант */
    double pi_val = kolibri_constant_value(KNT_TOKEN_PI);
    assert(fabs(pi_val - 3.14159265358979) < 1e-10);

    double e_val = kolibri_constant_value(KNT_TOKEN_E);
    assert(fabs(e_val - 2.71828182845904) < 1e-10);

    double phi_val = kolibri_constant_value(KNT_TOKEN_PHI);
    assert(fabs(phi_val - 1.61803398874989) < 1e-10);

    printf("  π = %.10f\n", pi_val);
    printf("  e = %.10f\n", e_val);
    printf("  φ = %.10f\n", phi_val);
    printf("✓ Constants test passed\n\n");
}

/* ============================================================================
 * ТЕСТ PARSE NUMBER
 * ============================================================================ */

void test_parse_number(void) {
    printf("Testing number parsing...\n");

    KolibriNumberInfo info;

    /* Целое положительное */
    uint16_t tok = kolibri_parse_number("42", 2, &info);
    assert(info.value == 42.0);
    assert(info.is_negative == 0);
    assert(info.num_digits == 2);
    printf("  '42' → value=%.0f, digits=%d\n", info.value, info.num_digits);

    /* Отрицательное */
    tok = kolibri_parse_number("-7", 2, &info);
    assert(info.value == -7.0);
    assert(info.is_negative == 1);
    printf("  '-7' → value=%.0f, negative=%d\n", info.value, info.is_negative);

    /* Десятичная дробь */
    tok = kolibri_parse_number("3.14", 4, &info);
    assert(info.value >= 3.13 && info.value <= 3.15);
    assert(info.decimal_places > 0);
    printf("  '3.14' → value=%.2f, decimal_places=%d\n", info.value, info.decimal_places);

    /* Экспоненциальная запись */
    tok = kolibri_parse_number("1e10", 4, &info);
    assert(info.has_exponent == 1);
    printf("  '1e10' → has_exponent=%d\n", info.has_exponent);

    printf("✓ Number parsing test passed\n\n");
}

/* ============================================================================
 * ТЕСТ DETOKENIZE
 * ============================================================================ */

void test_detokenize(void) {
    printf("Testing detokenization...\n");

    KolibriTokenizer tok;
    kolibri_tokenizer_init(&tok);

    /* Токенизируем и затем детокенизируем */
    KolibriTokenizationResult result;
    int ret = kolibri_tokenize(&tok, "2+2", 3, &result);
    assert(ret == 0);

    /* Собираем токены */
    uint16_t tokens[KNT_MAX_TOKENS];
    for (int i = 0; i < result.token_count && i < KNT_MAX_TOKENS; i++) {
        tokens[i] = result.tokens[i].token_id;
    }

    /* Детокенизируем */
    char output[1024];
    size_t out_len = kolibri_detokenize(&tok, tokens, result.token_count, output, sizeof(output));
    assert(out_len > 0);
    printf("  Detokenized: '%.*s' (len=%zu)\n", (int)out_len, output, out_len);

    kolibri_tokenizer_free(&tok);
    printf("✓ Detokenization test passed\n\n");
}

/* ============================================================================
 * ТЕСТ NUMERIC EMBEDDING
 * ============================================================================ */

void test_numeric_embedding(void) {
    printf("Testing numeric embedding...\n");

    KolibriNumberInfo info;
    info.value = 42.0;
    info.num_digits = 2;
    info.decimal_places = 0;
    info.is_negative = 0;
    info.has_exponent = 0;
    info.exponent_value = 0;

    float embedding[256];
    memset(embedding, 0, sizeof(embedding));

    kolibri_numeric_embedding(KNT_TOKEN_DIGIT_0, &info, embedding, 256);

    /* Embedding должен быть ненулевым */
    float norm = 0;
    for (int i = 0; i < 256; i++) {
        norm += embedding[i] * embedding[i];
    }
    norm = sqrtf(norm);

    assert(norm > 0.001f);  /* Должен быть ненулевой вектор */
    printf("  Embedding norm: %.6f\n", norm);

    printf("✓ Numeric embedding test passed\n\n");
}

/* ============================================================================
 * ТЕСТ TOKEN NAMES
 * ============================================================================ */

void test_token_names(void) {
    printf("Testing token names...\n");

    /* Проверяем что имена для ключевых токенов не NULL */
    const char *pi_name = kolibri_token_name(KNT_TOKEN_PI);
    assert(pi_name != NULL);
    assert(strlen(pi_name) > 0);
    printf("  π: '%s'\n", pi_name);

    const char *plus_name = kolibri_token_name(KNT_TOKEN_PLUS);
    assert(plus_name != NULL);
    assert(strlen(plus_name) > 0);
    printf("  +: '%s'\n", plus_name);

    const char *sin_name = kolibri_token_name(KNT_TOKEN_SIN);
    assert(sin_name != NULL);
    assert(strlen(sin_name) > 0);
    printf("  sin: '%s'\n", sin_name);

    printf("✓ Token names test passed\n\n");
}

/* ============================================================================
 * ТЕСТ UNICODE МАТЕМАТИКИ
 * ============================================================================ */

void test_unicode_math_tokens(void) {
    printf("Testing Unicode math token recognition...\n");

    KolibriTokenizer tok;
    kolibri_tokenizer_init(&tok);

    /* Токенизируем выражение с π */
    KolibriTokenizationResult result;
    int ret = kolibri_tokenize_math(&tok, "π * r^2", &result);
    assert(ret == 0);
    assert(result.token_count > 0);

    printf("  'π * r^2' → %d tokens\n", result.token_count);

    /* Токенизируем выражение с √ */
    ret = kolibri_tokenize_math(&tok, "√(16)", &result);
    assert(ret == 0);
    assert(result.token_count > 0);

    printf("  '√(16)' → %d tokens\n", result.token_count);

    kolibri_tokenizer_free(&tok);
    printf("✓ Unicode math tokens test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ПЕЧАТИ ТОКЕНИЗАЦИИ
 * ============================================================================ */

void test_print_tokenization(void) {
    printf("Testing tokenization printing...\n");

    KolibriTokenizer tok;
    kolibri_tokenizer_init(&tok);

    KolibriTokenizationResult result;
    int ret = kolibri_tokenize(&tok, "12 + 34 = 46", 12, &result);
    assert(ret == 0);

    /* Просто вызываем печать — проверяем что не крашится */
    kolibri_print_tokenization(&result);

    kolibri_tokenizer_free(&tok);
    printf("✓ Print tokenization test passed\n\n");
}

/* ============================================================================
 * ИНТЕГРАЦИОННЫЙ ТЕСТ
 * ============================================================================ */

void test_integrated_workflow(void) {
    printf("Testing integrated workflow...\n");

    KolibriTokenizer tok;
    kolibri_tokenizer_init(&tok);

    /* Полный цикл: tokenize → analyze → detokenize */
    const char *expr = "sin(π/2) + cos(0) = 2";
    KolibriTokenizationResult result;
    int ret = kolibri_tokenize_math(&tok, expr, &result);
    assert(ret == 0);
    assert(result.token_count > 0);
    assert(result.function_count >= 2);  /* sin и cos */
    assert(result.operator_count >= 1);  /* +, = */

    printf("  Expression: '%s'\n", expr);
    printf("  Tokens: %d, Functions: %d, Operators: %d\n",
           result.token_count, result.function_count, result.operator_count);

    /* Проверяем что цифровые токены корректны */
    int digit_count = 0;
    for (int i = 0; i < result.token_count; i++) {
        if (kolibri_is_digit_token(result.tokens[i].token_id)) {
            digit_count++;
        }
    }
    printf("  Digit tokens in expression: %d\n", digit_count);

    kolibri_tokenizer_free(&tok);
    printf("✓ Integrated workflow test passed\n\n");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("===========================================\n");
    printf("Kolibri Numeric Tokenizer Tests\n");
    printf("===========================================\n\n");

    /* Initialization */
    printf("--- Initialization ---\n\n");
    test_tokenizer_init();

    /* Vocab */
    printf("--- Vocab ---\n\n");
    test_vocab_size();

    /* Tokenization */
    printf("--- Tokenization ---\n\n");
    test_tokenize_simple_expression();
    test_tokenize_math_expression();

    /* Token types */
    printf("--- Token Types ---\n\n");
    test_digit_tokens();
    test_math_operators();
    test_functions();
    test_constants();

    /* Number parsing */
    printf("--- Number Parsing ---\n\n");
    test_parse_number();

    /* Detokenization */
    printf("--- Detokenization ---\n\n");
    test_detokenize();

    /* Embedding */
    printf("--- Embedding ---\n\n");
    test_numeric_embedding();

    /* Utilities */
    printf("--- Utilities ---\n\n");
    test_token_names();
    test_print_tokenization();

    /* Unicode */
    printf("--- Unicode Math ---\n\n");
    test_unicode_math_tokens();

    /* Integration */
    printf("--- Integration ---\n\n");
    test_integrated_workflow();

    printf("===========================================\n");
    printf("All tests passed! ✓\n");
    printf("===========================================\n");

    return 0;
}
