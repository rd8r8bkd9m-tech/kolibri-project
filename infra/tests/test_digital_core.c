#include "kolibri/digital_core.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void assert_roundtrip(const uint8_t *input, size_t len) {
    size_t digit_cap = k_digits_for_bytes(len);
    uint8_t *digits_buf = digit_cap ? (uint8_t *)malloc(digit_cap) : NULL;
    uint8_t *restored = len ? (uint8_t *)malloc(len) : NULL;
    assert((digit_cap == 0 || digits_buf != NULL) && (len == 0 || restored != NULL));

    k_digit_stream digits;
    k_digit_stream_init(&digits, digits_buf, digit_cap);
    assert(k_encode_bytes_to_digits(input, len, &digits) == 0);
    assert(digits.length == digit_cap);
    assert(k_validate_digit_stream(&digits) == 1);
    assert(k_validate_canonical_byte_digits(&digits) == 1);

    size_t written = 0;
    assert(k_decode_digits_to_bytes(&digits, restored, len, &written) == 0);
    assert(written == len);
    assert(len == 0 || memcmp(input, restored, len) == 0);

    free(digits_buf);
    free(restored);
}

static void test_roundtrips(void) {
    assert_roundtrip(NULL, 0);

    const uint8_t ascii[] = "Kolibri C23 digit core";
    assert_roundtrip(ascii, sizeof(ascii) - 1U);

    const uint8_t utf8_ru[] = "Колибри хранит все в цифрах";
    assert_roundtrip(utf8_ru, sizeof(utf8_ru) - 1U);

    uint8_t all_bytes[256];
    for (size_t i = 0; i < sizeof(all_bytes); ++i) all_bytes[i] = (uint8_t)i;
    assert_roundtrip(all_bytes, sizeof(all_bytes));

    uint8_t large[8192];
    for (size_t i = 0; i < sizeof(large); ++i) large[i] = (uint8_t)((i * 37U + 11U) & 0xFFU);
    assert_roundtrip(large, sizeof(large));

    uint8_t randomish[1024];
    uint32_t x = 0xC0FFEEu;
    for (size_t i = 0; i < sizeof(randomish); ++i) {
        x = x * 1664525u + 1013904223u;
        randomish[i] = (uint8_t)(x >> 24);
    }
    assert_roundtrip(randomish, sizeof(randomish));
}

static k_digit_stream stream_from_digits(uint8_t *digits, size_t len) {
    k_digit_stream stream;
    k_digit_stream_init(&stream, digits, len);
    stream.length = len;
    return stream;
}

static void test_formula_literal(void) {
    uint8_t original_digits[] = {0, 7, 5, 1, 2, 3};
    k_digit_stream original = stream_from_digits(original_digits, sizeof(original_digits));

    KDigitFormula *literal = k_formula_literal(original_digits, sizeof(original_digits));
    assert(literal != NULL);
    assert(k_formula_verify(literal, &original) == 1);

    uint8_t eval_buf[sizeof(original_digits)];
    k_digit_stream eval;
    k_digit_stream_init(&eval, eval_buf, sizeof(eval_buf));
    assert(k_formula_eval(literal, &eval) == 0);
    assert(eval.length == sizeof(original_digits));
    assert(memcmp(eval_buf, original_digits, sizeof(original_digits)) == 0);

    k_formula_destroy(literal);
}

static void test_formula_repeat(void) {
    uint8_t pattern_digits[] = {1, 2, 3};
    uint8_t expected_digits[] = {1, 2, 3, 1, 2, 3, 1, 2, 3};
    k_digit_stream expected = stream_from_digits(expected_digits, sizeof(expected_digits));

    KDigitFormula *pattern = k_formula_literal(pattern_digits, sizeof(pattern_digits));
    KDigitFormula *repeat = k_formula_repeat(pattern, 3);
    assert(repeat != NULL);
    assert(k_formula_verify(repeat, &expected) == 1);

    expected_digits[8] = 4;
    expected = stream_from_digits(expected_digits, sizeof(expected_digits));
    assert(k_formula_verify(repeat, &expected) == 0);

    k_formula_destroy(repeat);
}

static void test_formula_sequence(void) {
    uint8_t expected_digits[] = {1, 3, 5, 7, 9};
    k_digit_stream expected = stream_from_digits(expected_digits, sizeof(expected_digits));

    KDigitFormula *sequence = k_formula_sequence(1, 2, 5);
    assert(sequence != NULL);
    assert(k_formula_verify(sequence, &expected) == 1);

    expected_digits[4] = 8;
    expected = stream_from_digits(expected_digits, sizeof(expected_digits));
    assert(k_formula_verify(sequence, &expected) == 0);

    k_formula_destroy(sequence);
}

static void test_meta_formula_repeat(void) {
    uint8_t gene[32] = {0};
    gene[0] = 0;
    gene[1] = 1;  /* repeat */
    gene[2] = 3;  /* pattern length */
    gene[3] = 4;
    gene[4] = 5;
    gene[5] = 6;
    gene[11] = 0;
    gene[12] = 4; /* repeat count */

    KMetaFormula meta;
    assert(k_meta_formula_decode(gene, sizeof(gene), &meta) == 0);
    assert(meta.operation == K_META_GENERATE_REPEAT);
    assert(meta.params.repeat.pattern_length == 3);
    assert(meta.params.repeat.count == 4);

    KDigitFormula *formula = k_meta_formula_execute(&meta);
    assert(formula != NULL);

    uint8_t expected_digits[] = {4, 5, 6, 4, 5, 6, 4, 5, 6, 4, 5, 6};
    k_digit_stream expected = stream_from_digits(expected_digits, sizeof(expected_digits));
    assert(k_formula_verify(formula, &expected) == 1);

    uint8_t encoded_meta[32];
    size_t written = 0;
    assert(k_meta_formula_encode(&meta, encoded_meta, sizeof(encoded_meta), &written) == 0);
    assert(written == 32);
    assert(encoded_meta[1] == 1);
    assert(encoded_meta[2] == 3);
    assert(encoded_meta[3] == 4);
    assert(encoded_meta[4] == 5);
    assert(encoded_meta[5] == 6);

    k_formula_destroy(formula);
}

static void test_decode_rejects_non_canonical_byte_digits(void) {
    uint8_t bad_digits[] = {9, 9, 9};
    k_digit_stream bad = stream_from_digits(bad_digits, sizeof(bad_digits));
    uint8_t out[1];
    assert(k_validate_digit_stream(&bad) == 1);
    assert(k_validate_canonical_byte_digits(&bad) == 0);
    assert(k_decode_digits_to_bytes(&bad, out, sizeof(out), NULL) != 0);
}

int main(void) {
    test_roundtrips();
    test_formula_literal();
    test_formula_repeat();
    test_formula_sequence();
    test_meta_formula_repeat();
    test_decode_rejects_non_canonical_byte_digits();
    return 0;
}
