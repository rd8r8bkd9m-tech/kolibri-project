#ifndef KOLIBRI_DIGITAL_CORE_H
#define KOLIBRI_DIGITAL_CORE_H

#include "kolibri/decimal.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    K_FORMULA_LITERAL = 0,
    K_FORMULA_REPEAT = 1,
    K_FORMULA_SEQUENCE = 2,
    K_FORMULA_CONCAT = 3
} KDigitFormulaType;

typedef struct KDigitFormula KDigitFormula;

struct KDigitFormula {
    KDigitFormulaType type;
    size_t materialized_digits;
    union {
        struct {
            uint8_t *digits;
            size_t length;
        } literal;
        struct {
            KDigitFormula *pattern;
            size_t count;
        } repeat;
        struct {
            uint8_t start;
            uint8_t step;
            size_t count;
        } sequence;
        struct {
            KDigitFormula **items;
            size_t count;
        } concat;
    } data;
};

KDigitFormula *k_formula_literal(const uint8_t *digits, size_t length);
KDigitFormula *k_formula_repeat(KDigitFormula *pattern, size_t count);
KDigitFormula *k_formula_sequence(uint8_t start, uint8_t step, size_t count);
KDigitFormula *k_formula_concat(KDigitFormula **items, size_t count);
void k_formula_destroy(KDigitFormula *formula);

size_t k_formula_digit_length(const KDigitFormula *formula);
int k_formula_eval(const KDigitFormula *formula, k_digit_stream *out);
int k_formula_verify(const KDigitFormula *formula, const k_digit_stream *original);

typedef enum {
    K_META_GENERATE_LITERAL = 0,
    K_META_GENERATE_REPEAT = 1,
    K_META_GENERATE_SEQUENCE = 2,
    K_META_GENERATE_CONCAT = 3,
    K_META_COMPRESS_LOGIC = 7
} KMetaFormulaOperation;

typedef struct {
    KMetaFormulaOperation operation;
    uint8_t gene_digits[64];
    size_t gene_length;
    union {
        struct {
            uint8_t digits[16];
            size_t length;
        } literal;
        struct {
            uint8_t pattern[16];
            size_t pattern_length;
            size_t count;
        } repeat;
        struct {
            uint8_t start;
            uint8_t step;
            size_t count;
        } sequence;
    } params;
} KMetaFormula;

int k_meta_formula_decode(const uint8_t *gene_digits, size_t gene_length, KMetaFormula *out);
int k_meta_formula_encode(const KMetaFormula *meta, uint8_t *out_digits, size_t out_capacity, size_t *written);
KDigitFormula *k_meta_formula_execute(const KMetaFormula *meta);

#ifdef __cplusplus
}
#endif

#endif
