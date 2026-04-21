#include "kolibri/digital_core.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int checked_mul_size(size_t a, size_t b, size_t *out) {
    if (!out) return -1;
    if (a != 0 && b > SIZE_MAX / a) return -1;
    *out = a * b;
    return 0;
}

static int checked_add_size(size_t a, size_t b, size_t *out) {
    if (!out) return -1;
    if (b > SIZE_MAX - a) return -1;
    *out = a + b;
    return 0;
}

static KDigitFormula *alloc_formula(KDigitFormulaType type) {
    KDigitFormula *formula = (KDigitFormula *)calloc(1, sizeof(KDigitFormula));
    if (formula) formula->type = type;
    return formula;
}

KDigitFormula *k_formula_literal(const uint8_t *digits, size_t length) {
    if (!digits && length > 0) return NULL;

    KDigitFormula *formula = alloc_formula(K_FORMULA_LITERAL);
    if (!formula) return NULL;

    if (length > 0) {
        formula->data.literal.digits = (uint8_t *)malloc(length);
        if (!formula->data.literal.digits) {
            free(formula);
            return NULL;
        }
        for (size_t i = 0; i < length; ++i) {
            if (digits[i] > 9U) {
                k_formula_destroy(formula);
                return NULL;
            }
            formula->data.literal.digits[i] = digits[i];
        }
    }

    formula->data.literal.length = length;
    formula->materialized_digits = length;
    return formula;
}

KDigitFormula *k_formula_repeat(KDigitFormula *pattern, size_t count) {
    if (!pattern || count == 0) return NULL;

    size_t materialized = 0;
    if (checked_mul_size(k_formula_digit_length(pattern), count, &materialized) != 0) return NULL;

    KDigitFormula *formula = alloc_formula(K_FORMULA_REPEAT);
    if (!formula) return NULL;
    formula->data.repeat.pattern = pattern;
    formula->data.repeat.count = count;
    formula->materialized_digits = materialized;
    return formula;
}

KDigitFormula *k_formula_sequence(uint8_t start, uint8_t step, size_t count) {
    if (start > 9U || step > 9U || count == 0) return NULL;

    KDigitFormula *formula = alloc_formula(K_FORMULA_SEQUENCE);
    if (!formula) return NULL;
    formula->data.sequence.start = start;
    formula->data.sequence.step = step;
    formula->data.sequence.count = count;
    formula->materialized_digits = count;
    return formula;
}

KDigitFormula *k_formula_concat(KDigitFormula **items, size_t count) {
    if (!items || count == 0) return NULL;

    KDigitFormula **owned = (KDigitFormula **)calloc(count, sizeof(KDigitFormula *));
    if (!owned) return NULL;

    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!items[i]) {
            free(owned);
            return NULL;
        }
        size_t next = 0;
        if (checked_add_size(total, k_formula_digit_length(items[i]), &next) != 0) {
            free(owned);
            return NULL;
        }
        owned[i] = items[i];
        total = next;
    }

    KDigitFormula *formula = alloc_formula(K_FORMULA_CONCAT);
    if (!formula) {
        free(owned);
        return NULL;
    }
    formula->data.concat.items = owned;
    formula->data.concat.count = count;
    formula->materialized_digits = total;
    return formula;
}

void k_formula_destroy(KDigitFormula *formula) {
    if (!formula) return;

    switch (formula->type) {
        case K_FORMULA_LITERAL:
            free(formula->data.literal.digits);
            break;
        case K_FORMULA_REPEAT:
            k_formula_destroy(formula->data.repeat.pattern);
            break;
        case K_FORMULA_CONCAT:
            for (size_t i = 0; i < formula->data.concat.count; ++i) {
                k_formula_destroy(formula->data.concat.items[i]);
            }
            free(formula->data.concat.items);
            break;
        case K_FORMULA_SEQUENCE:
        default:
            break;
    }

    free(formula);
}

size_t k_formula_digit_length(const KDigitFormula *formula) {
    return formula ? formula->materialized_digits : 0;
}

int k_formula_eval(const KDigitFormula *formula, k_digit_stream *out) {
    if (!formula || !out) return -1;
    if (out->length > out->capacity) return -1;
    if (out->capacity - out->length < formula->materialized_digits) return -1;

    switch (formula->type) {
        case K_FORMULA_LITERAL:
            for (size_t i = 0; i < formula->data.literal.length; ++i) {
                if (k_digit_stream_push(out, formula->data.literal.digits[i]) != 0) return -1;
            }
            return 0;

        case K_FORMULA_REPEAT:
            for (size_t i = 0; i < formula->data.repeat.count; ++i) {
                if (k_formula_eval(formula->data.repeat.pattern, out) != 0) return -1;
            }
            return 0;

        case K_FORMULA_SEQUENCE:
            for (size_t i = 0; i < formula->data.sequence.count; ++i) {
                uint8_t digit = (uint8_t)((formula->data.sequence.start +
                                           (formula->data.sequence.step * (i % 10U))) % 10U);
                if (k_digit_stream_push(out, digit) != 0) return -1;
            }
            return 0;

        case K_FORMULA_CONCAT:
            for (size_t i = 0; i < formula->data.concat.count; ++i) {
                if (k_formula_eval(formula->data.concat.items[i], out) != 0) return -1;
            }
            return 0;

        default:
            return -1;
    }
}

int k_formula_verify(const KDigitFormula *formula, const k_digit_stream *original) {
    if (!formula || !original || !k_validate_digit_stream(original)) return 0;
    if (k_formula_digit_length(formula) != original->length) return 0;

    uint8_t *tmp = NULL;
    if (original->length > 0) {
        tmp = (uint8_t *)malloc(original->length);
        if (!tmp) return 0;
    }

    k_digit_stream eval;
    k_digit_stream_init(&eval, tmp, original->length);
    int rc = k_formula_eval(formula, &eval);
    int ok = (rc == 0 && eval.length == original->length &&
              (original->length == 0 || memcmp(eval.digits, original->digits, original->length) == 0));
    free(tmp);
    return ok;
}

int k_meta_formula_decode(const uint8_t *gene_digits, size_t gene_length, KMetaFormula *out) {
    if (!gene_digits || !out) return -1;
    if (gene_length != 32U && gene_length != 64U) return -1;

    k_digit_stream gene;
    k_digit_stream_init(&gene, (uint8_t *)gene_digits, gene_length);
    gene.length = gene_length;
    if (!k_validate_digit_stream(&gene)) return -1;

    memset(out, 0, sizeof(*out));
    memcpy(out->gene_digits, gene_digits, gene_length);
    out->gene_length = gene_length;

    uint8_t op = (uint8_t)((gene_digits[0] * 10U + gene_digits[1]) % 8U);
    out->operation = (KMetaFormulaOperation)op;

    switch (out->operation) {
        case K_META_GENERATE_LITERAL: {
            size_t length = gene_digits[2] % 16U;
            if (length == 0) length = 1;
            out->params.literal.length = length;
            for (size_t i = 0; i < length; ++i) {
                out->params.literal.digits[i] = gene_digits[3U + i];
            }
            return 0;
        }

        case K_META_GENERATE_REPEAT: {
            size_t pattern_length = gene_digits[2] % 8U;
            if (pattern_length == 0) pattern_length = 1;
            size_t count = gene_digits[11] * 10U + gene_digits[12];
            if (count == 0) count = 1;

            out->params.repeat.pattern_length = pattern_length;
            out->params.repeat.count = count;
            for (size_t i = 0; i < pattern_length; ++i) {
                out->params.repeat.pattern[i] = gene_digits[3U + i];
            }
            return 0;
        }

        case K_META_GENERATE_SEQUENCE: {
            size_t count = gene_digits[4] * 10U + gene_digits[5];
            if (count == 0) count = 1;
            out->params.sequence.start = gene_digits[2];
            out->params.sequence.step = gene_digits[3];
            out->params.sequence.count = count;
            return 0;
        }

        default:
            return -1;
    }
}

int k_meta_formula_encode(const KMetaFormula *meta, uint8_t *out_digits, size_t out_capacity, size_t *written) {
    if (written) *written = 0;
    if (!meta || !out_digits || out_capacity < 32U) return -1;

    memset(out_digits, 0, 32U);
    out_digits[0] = 0;
    out_digits[1] = (uint8_t)meta->operation;

    switch (meta->operation) {
        case K_META_GENERATE_LITERAL:
            if (meta->params.literal.length == 0 || meta->params.literal.length > 16U) return -1;
            out_digits[2] = (uint8_t)(meta->params.literal.length % 10U);
            for (size_t i = 0; i < meta->params.literal.length; ++i) {
                if (meta->params.literal.digits[i] > 9U) return -1;
                out_digits[3U + i] = meta->params.literal.digits[i];
            }
            break;

        case K_META_GENERATE_REPEAT:
            if (meta->params.repeat.pattern_length == 0 || meta->params.repeat.pattern_length > 8U) return -1;
            if (meta->params.repeat.count == 0 || meta->params.repeat.count > 99U) return -1;
            out_digits[2] = (uint8_t)(meta->params.repeat.pattern_length % 10U);
            for (size_t i = 0; i < meta->params.repeat.pattern_length; ++i) {
                if (meta->params.repeat.pattern[i] > 9U) return -1;
                out_digits[3U + i] = meta->params.repeat.pattern[i];
            }
            out_digits[11] = (uint8_t)(meta->params.repeat.count / 10U);
            out_digits[12] = (uint8_t)(meta->params.repeat.count % 10U);
            break;

        case K_META_GENERATE_SEQUENCE:
            if (meta->params.sequence.start > 9U || meta->params.sequence.step > 9U) return -1;
            if (meta->params.sequence.count == 0 || meta->params.sequence.count > 99U) return -1;
            out_digits[2] = meta->params.sequence.start;
            out_digits[3] = meta->params.sequence.step;
            out_digits[4] = (uint8_t)(meta->params.sequence.count / 10U);
            out_digits[5] = (uint8_t)(meta->params.sequence.count % 10U);
            break;

        default:
            return -1;
    }

    if (written) *written = 32U;
    return 0;
}

KDigitFormula *k_meta_formula_execute(const KMetaFormula *meta) {
    if (!meta) return NULL;

    switch (meta->operation) {
        case K_META_GENERATE_LITERAL:
            return k_formula_literal(meta->params.literal.digits, meta->params.literal.length);

        case K_META_GENERATE_REPEAT: {
            KDigitFormula *pattern = k_formula_literal(meta->params.repeat.pattern,
                                                       meta->params.repeat.pattern_length);
            if (!pattern) return NULL;
            KDigitFormula *repeat = k_formula_repeat(pattern, meta->params.repeat.count);
            if (!repeat) k_formula_destroy(pattern);
            return repeat;
        }

        case K_META_GENERATE_SEQUENCE:
            return k_formula_sequence(meta->params.sequence.start,
                                      meta->params.sequence.step,
                                      meta->params.sequence.count);

        default:
            return NULL;
    }
}
