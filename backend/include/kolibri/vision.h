/*
 * Kolibri Vision Module — Фаза 3.1
 *
 * Image → decimal genome через DCT (Discrete Cosine Transform)
 * Пиксели → числовые паттерны → формулы для распознавания объектов
 */

#ifndef KOLIBRI_VISION_H
#define KOLIBRI_VISION_H

#include "kolibri/formula.h"
#include "kolibri/decimal.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Максимальный размер изображения для обработки */
#define KOLIBRI_VISION_MAX_WIDTH 512
#define KOLIBRI_VISION_MAX_HEIGHT 512
#define KOLIBRI_VISION_MAX_CHANNELS 3  /* RGB */

/* DCT block size для feature extraction */
#define KOLIBRI_VISION_DCT_BLOCK_SIZE 8

/* Размер визуального генома (decimal digits) */
#define KOLIBRI_VISION_GENOME_SIZE 4096

/* Типы визуальных паттернов */
typedef enum {
    KOLIBRI_VISION_PATTERN_UNKNOWN = 0,
    KOLIBRI_VISION_PATTERN_EDGE,       /* Края/границы */
    KOLIBRI_VISION_PATTERN_CORNER,     /* Углы */
    KOLIBRI_VISION_PATTERN_TEXTURE,    /* Текстуры */
    KOLIBRI_VISION_PATTERN_BLOB,       /* Пятна/объекты */
    KOLIBRI_VISION_PATTERN_LINE,       /* Линии */
    KOLIBRI_VISION_PATTERN_CIRCLE,     /* Круги */
    KOLIBRI_VISION_PATTERN_FACE,       /* Лица (базовое) */
    KOLIBRI_VISION_PATTERN_TEXT,       /* Текст */
    KOLIBRI_VISION_PATTERN_COUNT
} KolibriVisionPatternType;

/* Визуальный паттерн */
typedef struct {
    KolibriVisionPatternType type;
    double confidence;
    uint16_t x, y;           /* Позиция */
    uint16_t width, height;  /* Размер */
    uint8_t dct_coeffs[KOLIBRI_VISION_DCT_BLOCK_SIZE * KOLIBRI_VISION_DCT_BLOCK_SIZE];
} KolibriVisionPattern;

/* Визуальный геном — decimal представление изображения */
typedef struct {
    uint8_t digits[KOLIBRI_VISION_GENOME_SIZE];
    size_t length;
    uint32_t image_hash;  /* Perceptual hash изображения */
    uint16_t width, height;
    uint8_t channels;
} KolibriVisionGenome;

/* Визуальная формула — связывает изображение с ответом */
typedef struct {
    KolibriVisionGenome genome;
    KolibriFormula *formula;
    char label[256];
    double confidence;
} KolibriVisionFormula;

/* Визуальная память — пул визуальных формул */
typedef struct {
    KolibriVisionFormula *formulas;
    size_t count;
    size_t capacity;
    KolibriRng rng;
} KolibriVisionMemory;

/* ============================================================================
 * API
 * ============================================================================ */

/* Инициализация/освобождение */
int kolibri_vision_memory_init(KolibriVisionMemory *vmem, uint64_t seed);
void kolibri_vision_memory_destroy(KolibriVisionMemory *vmem);

/* Обработка изображения */
int kolibri_vision_process_image(KolibriVisionMemory *vmem,
                                  const uint8_t *pixels,
                                  uint16_t width, uint16_t height, uint8_t channels,
                                  KolibriVisionGenome *out_genome);

/* Извлечение визуальных паттернов */
int kolibri_vision_extract_patterns(const uint8_t *pixels,
                                     uint16_t width, uint16_t height, uint8_t channels,
                                     KolibriVisionPattern *out_patterns,
                                     size_t max_patterns,
                                     size_t *out_count);

/* DCT feature extraction */
int kolibri_vision_dct_features(const uint8_t *block,
                                 uint8_t *out_coeffs,
                                 size_t block_size);

/* Perceptual hash изображения */
uint32_t kolibri_vision_perceptual_hash(const uint8_t *pixels,
                                         uint16_t width, uint16_t height, uint8_t channels);

/* Поиск похожих изображений в памяти */
const KolibriVisionFormula *kolibri_vision_memory_find_similar(
    KolibriVisionMemory *vmem,
    const KolibriVisionGenome *query_genome,
    double *out_similarity);

/* Добавление визуальной формулы в память */
int kolibri_vision_memory_add_formula(KolibriVisionMemory *vmem,
                                       const KolibriVisionGenome *genome,
                                       const char *label,
                                       const KolibriFormula *formula);

/* Классификация изображения */
int kolibri_vision_classify(KolibriVisionMemory *vmem,
                             const KolibriVisionGenome *genome,
                             char *out_label, size_t label_size,
                             double *out_confidence);

/* Vision-language fusion: объединение визуальных и текстовых формул */
int kolibri_vision_language_fusion(KolibriVisionMemory *vmem,
                                    KolibriFormulaPool *formula_pool,
                                    const KolibriVisionGenome *vision_genome,
                                    const char *text_query,
                                    char *out_response, size_t response_size);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_VISION_H */
