/*
 * Kolibri Vision Module Implementation — Фаза 3.1
 *
 * Image → decimal genome через DCT
 */

#include "kolibri/vision.h"
#include "kolibri/decimal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * DCT (Discrete Cosine Transform) — упрощённая реализация
 * ============================================================================ */

static double dct_basis(int u, int x, int N) {
    return cos((M_PI * (double)(2 * x + 1) * (double)u) / (2.0 * (double)N));
}

int kolibri_vision_dct_features(const uint8_t *block,
                                 uint8_t *out_coeffs,
                                 size_t block_size) {
    if (!block || !out_coeffs || block_size < 8) return -1;

    int N = 8;
    double dct_matrix[64];

    /* 2D DCT */
    for (int v = 0; v < N; v++) {
        for (int u = 0; u < N; u++) {
            double sum = 0.0;
            for (int y = 0; y < N; y++) {
                for (int x = 0; x < N; x++) {
                    sum += (double)block[y * N + x] *
                           dct_basis(u, x, N) * dct_basis(v, y, N);
                }
            }
            /* Нормализация */
            double cu = (u == 0) ? 1.0 / sqrt(2.0) : 1.0;
            double cv = (v == 0) ? 1.0 / sqrt(2.0) : 1.0;
            dct_matrix[v * N + u] = 0.25 * cu * cv * sum;
        }
    }

    /* Квантизация и конвертация в decimal digits */
    /* Берём первые 16低频 коэффициентов (zigzag order) */
    static const int zigzag[16] = {
        0, 1, 8, 16, 9, 2, 3, 10,
        17, 24, 32, 25, 18, 11, 4, 5
    };

    for (int i = 0; i < 16; i++) {
        int idx = zigzag[i];
        double val = dct_matrix[idx];
        /* Квантизация: нормализуем к [0, 9] */
        int digit = (int)(fabs(val) / 10.0) % 10;
        if (digit < 0) digit = 0;
        if (digit > 9) digit = 9;
        out_coeffs[i] = (uint8_t)digit;
    }

    return 0;
}

/* ============================================================================
 * Perceptual Hash (pHash)
 * ============================================================================ */

uint32_t kolibri_vision_perceptual_hash(const uint8_t *pixels,
                                         uint16_t width, uint16_t height, uint8_t channels) {
    if (!pixels || width == 0 || height == 0) return 0;

    /* Упрощённый pHash: downsampling → grayscale → DCT → median threshold */
    uint8_t thumb[8 * 8];
    memset(thumb, 0, sizeof(thumb));

    /* Downsampling до 8×8 */
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int src_x = (x * width) / 8;
            int src_y = (y * height) / 8;
            int idx = (src_y * width + src_x) * channels;

            /* Grayscale: 0.299R + 0.587G + 0.114B */
            double gray = 0.299 * pixels[idx] +
                          0.587 * (channels >= 2 ? pixels[idx + 1] : pixels[idx]) +
                          0.114 * (channels >= 3 ? pixels[idx + 2] : pixels[idx]);
            thumb[y * 8 + x] = (uint8_t)gray;
        }
    }

    /* DCT */
    double dct[64];
    for (int v = 0; v < 8; v++) {
        for (int u = 0; u < 8; u++) {
            double sum = 0.0;
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    sum += (double)thumb[y * 8 + x] *
                           dct_basis(u, x, 8) * dct_basis(v, y, 8);
                }
            }
            double cu = (u == 0) ? 1.0 / sqrt(2.0) : 1.0;
            double cv = (v == 0) ? 1.0 / sqrt(2.0) : 1.0;
            dct[v * 8 + u] = 0.25 * cu * cv * sum;
        }
    }

    /* Median threshold (исключаем DC component) */
    double sum = 0.0;
    int count = 0;
    for (int i = 1; i < 64; i++) {
        sum += dct[i];
        count++;
    }
    double median = count > 0 ? sum / count : 0.0;

    /* Формируем 64-битный хеш */
    uint64_t hash = 0;
    for (int i = 1; i < 64; i++) {
        if (dct[i] > median) {
            hash |= (1ULL << (i - 1));
        }
    }

    return (uint32_t)(hash ^ (hash >> 32));
}

/* ============================================================================
 * Visual Pattern Extraction
 * ============================================================================ */

int kolibri_vision_extract_patterns(const uint8_t *pixels,
                                     uint16_t width, uint16_t height, uint8_t channels,
                                     KolibriVisionPattern *out_patterns,
                                     size_t max_patterns,
                                     size_t *out_count) {
    if (!pixels || !out_patterns || !out_count) return -1;

    *out_count = 0;

    /* Простой edge detection (Sobel) */
    for (int y = 1; y < height - 1 && *out_count < max_patterns; y += 8) {
        for (int x = 1; x < width - 1 && *out_count < max_patterns; x += 8) {
            /* Grayscale helper macro */
            #define GET_GRAY(px, py) \
                (0.299 * pixels[((py) * width + (px)) * channels] + \
                 0.587 * (channels >= 2 ? pixels[((py) * width + (px)) * channels + 1] : pixels[((py) * width + (px)) * channels]) + \
                 0.114 * (channels >= 3 ? pixels[((py) * width + (px)) * channels + 2] : pixels[((py) * width + (px)) * channels]))

            /* Sobel operators */
            double gx = -GET_GRAY(x-1, y-1) + GET_GRAY(x+1, y-1)
                       -2*GET_GRAY(x-1, y)   + 2*GET_GRAY(x+1, y)
                       -GET_GRAY(x-1, y+1)   + GET_GRAY(x+1, y+1);

            double gy = -GET_GRAY(x-1, y-1) - 2*GET_GRAY(x, y-1) - GET_GRAY(x+1, y-1)
                       +GET_GRAY(x-1, y+1) + 2*GET_GRAY(x, y+1) + GET_GRAY(x+1, y+1);

            #undef GET_GRAY

            double magnitude = sqrt(gx * gx + gy * gy);

            if (magnitude > 50.0) {
                KolibriVisionPattern *p = &out_patterns[*out_count];
                p->type = KOLIBRI_VISION_PATTERN_EDGE;
                p->confidence = magnitude / 255.0;
                p->x = x;
                p->y = y;
                p->width = 8;
                p->height = 8;

                /* DCT features */
                uint8_t block[64];
                for (int by = 0; by < 8; by++) {
                    for (int bx = 0; bx < 8; bx++) {
                        int si = ((y + by) * width + (x + bx)) * channels;
                        block[by * 8 + bx] = pixels[si];
                    }
                }
                kolibri_vision_dct_features(block, p->dct_coeffs, 64);

                (*out_count)++;
            }
        }
    }

    return 0;
}

/* ============================================================================
 * Image → Decimal Genome
 * ============================================================================ */

int kolibri_vision_process_image(KolibriVisionMemory *vmem,
                                  const uint8_t *pixels,
                                  uint16_t width, uint16_t height, uint8_t channels,
                                  KolibriVisionGenome *out_genome) {
    if (!pixels || !out_genome) return -1;

    memset(out_genome, 0, sizeof(*out_genome));
    out_genome->width = width;
    out_genome->height = height;
    out_genome->channels = channels;

    /* Perceptual hash */
    out_genome->image_hash = kolibri_vision_perceptual_hash(pixels, width, height, channels);

    /* DCT features по всему изображению (block-based) */
    size_t genome_pos = 0;
    int block_size = KOLIBRI_VISION_DCT_BLOCK_SIZE;

    for (int y = 0; y < height - block_size + 1 && genome_pos < KOLIBRI_VISION_GENOME_SIZE - 16;
         y += block_size) {
        for (int x = 0; x < width - block_size + 1 && genome_pos < KOLIBRI_VISION_GENOME_SIZE - 16;
             x += block_size) {

            uint8_t block[64];
            for (int by = 0; by < block_size; by++) {
                for (int bx = 0; bx < block_size; bx++) {
                    int si = ((y + by) * width + (x + bx)) * channels;
                    block[by * block_size + bx] = pixels[si];
                }
            }

            uint8_t coeffs[16];
            if (kolibri_vision_dct_features(block, coeffs, 64) == 0) {
                for (int i = 0; i < 16 && genome_pos < KOLIBRI_VISION_GENOME_SIZE; i++) {
                    out_genome->digits[genome_pos++] = coeffs[i];
                }
            }
        }
    }

    out_genome->length = genome_pos;

    return 0;
}

/* ============================================================================
 * Vision Memory
 * ============================================================================ */

int kolibri_vision_memory_init(KolibriVisionMemory *vmem, uint64_t seed) {
    if (!vmem) return -1;

    memset(vmem, 0, sizeof(*vmem));
    vmem->capacity = 1024;
    vmem->formulas = (KolibriVisionFormula *)calloc(vmem->capacity,
                                                     sizeof(KolibriVisionFormula));
    if (!vmem->formulas) return -1;

    k_rng_seed(&vmem->rng, seed);

    return 0;
}

void kolibri_vision_memory_destroy(KolibriVisionMemory *vmem) {
    if (!vmem) return;
    if (vmem->formulas) {
        free(vmem->formulas);
        vmem->formulas = NULL;
    }
    vmem->count = 0;
    vmem->capacity = 0;
}

int kolibri_vision_memory_add_formula(KolibriVisionMemory *vmem,
                                       const KolibriVisionGenome *genome,
                                       const char *label,
                                       const KolibriFormula *formula) {
    if (!vmem || !genome || !label) return -1;

    /* Resize если нужно */
    if (vmem->count >= vmem->capacity) {
        size_t new_capacity = vmem->capacity * 2;
        KolibriVisionFormula *new_formulas = (KolibriVisionFormula *)realloc(
            vmem->formulas, new_capacity * sizeof(KolibriVisionFormula));
        if (!new_formulas) return -1;
        vmem->formulas = new_formulas;
        vmem->capacity = new_capacity;
    }

    KolibriVisionFormula *vf = &vmem->formulas[vmem->count];
    vf->genome = *genome;
    if (formula) vf->formula = (KolibriFormula *)formula;
    else vf->formula = NULL;
    strncpy(vf->label, label, sizeof(vf->label) - 1);
    vf->label[sizeof(vf->label) - 1] = '\0';
    vf->confidence = 0.5;

    vmem->count++;
    return 0;
}

/* Similarity между визуальными геномами */
static double vision_genome_similarity(const KolibriVisionGenome *a,
                                        const KolibriVisionGenome *b) {
    if (!a || !b) return 0.0;

    /* Hash similarity */
    uint32_t hash_diff = a->image_hash ^ b->image_hash;
    int hash_same_bits = 0;
    for (int i = 0; i < 32; i++) {
        if (!(hash_diff & (1U << i))) hash_same_bits++;
    }
    double hash_sim = (double)hash_same_bits / 32.0;

    /* DCT genome similarity */
    size_t min_len = a->length < b->length ? a->length : b->length;
    if (min_len == 0) return hash_sim * 0.5;

    double dct_sim = 0.0;
    for (size_t i = 0; i < min_len; i++) {
        int diff = abs((int)a->digits[i] - (int)b->digits[i]);
        dct_sim += (9.0 - (double)diff) / 9.0;
    }
    dct_sim /= (double)min_len;

    return hash_sim * 0.4 + dct_sim * 0.6;
}

const KolibriVisionFormula *kolibri_vision_memory_find_similar(
    KolibriVisionMemory *vmem,
    const KolibriVisionGenome *query_genome,
    double *out_similarity) {

    if (!vmem || !query_genome || vmem->count == 0) {
        if (out_similarity) *out_similarity = 0.0;
        return NULL;
    }

    const KolibriVisionFormula *best = NULL;
    double best_sim = 0.0;

    for (size_t i = 0; i < vmem->count; i++) {
        double sim = vision_genome_similarity(query_genome, &vmem->formulas[i].genome);
        if (sim > best_sim) {
            best_sim = sim;
            best = &vmem->formulas[i];
        }
    }

    if (out_similarity) *out_similarity = best_sim;
    return best;
}

int kolibri_vision_classify(KolibriVisionMemory *vmem,
                             const KolibriVisionGenome *genome,
                             char *out_label, size_t label_size,
                             double *out_confidence) {
    if (!vmem || !genome || !out_label) return -1;

    double sim = 0.0;
    const KolibriVisionFormula *best = kolibri_vision_memory_find_similar(vmem, genome, &sim);

    if (best && sim > 0.5) {
        strncpy(out_label, best->label, label_size - 1);
        out_label[label_size - 1] = '\0';
        if (out_confidence) *out_confidence = sim;
        return 0;
    }

    strncpy(out_label, "unknown", label_size - 1);
    out_label[label_size - 1] = '\0';
    if (out_confidence) *out_confidence = sim;
    return -1;
}

/* ============================================================================
 * Vision-Language Fusion
 * ============================================================================ */

int kolibri_vision_language_fusion(KolibriVisionMemory *vmem,
                                    KolibriFormulaPool *formula_pool,
                                    const KolibriVisionGenome *vision_genome,
                                    const char *text_query,
                                    char *out_response, size_t response_size) {
    if (!vmem || !formula_pool || !vision_genome || !text_query || !out_response) return -1;

    /* Шаг 1: Классифицируем изображение */
    char vision_label[256] = {0};
    double vision_confidence = 0.0;
    kolibri_vision_classify(vmem, vision_genome, vision_label, sizeof(vision_label),
                             &vision_confidence);

    /* Шаг 2: Ищем текстовые формулы по label */
    const KolibriFormula *best_formula = NULL;
    double best_fitness = -1.0;

    for (size_t i = 0; i < formula_pool->count; i++) {
        /* Проверяем ассоциации формулы */
        for (size_t j = 0; j < formula_pool->association_count; j++) {
            if (strstr(formula_pool->associations[j].question, vision_label) != NULL) {
                if (formula_pool->formulas[i].fitness > best_fitness) {
                    best_fitness = formula_pool->formulas[i].fitness;
                    best_formula = &formula_pool->formulas[i];
                }
                break;
            }
        }
    }

    /* Шаг 3: Формируем ответ */
    if (best_formula && vision_confidence > 0.5) {
        snprintf(out_response, response_size,
                 "Изображение: %s (уверенность: %.0f%%). "
                 "Формульный ответ: %s",
                 vision_label, vision_confidence * 100.0,
                 best_formula->associations[0].answer);
    } else if (vision_confidence > 0.3) {
        snprintf(out_response, response_size,
                 "Изображение: %s (уверенность: %.0f%%). "
                 "Текстовый ответ не найден.",
                 vision_label, vision_confidence * 100.0);
    } else {
        snprintf(out_response, response_size,
                 "Не удалось распознать изображение.");
    }

    return 0;
}
