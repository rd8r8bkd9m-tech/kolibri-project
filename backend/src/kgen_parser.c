/**
 * @file kgen_parser.c
 * @brief Реализация парсера файлов формата .kgen для Kolibri v0.5
 * 
 * Компонент C: Парсер .kgen файлов
 * Этап C.2-C.3: Базовый парсер и декомпрессия уровней
 */

#include "kolibri/kgen_parser.h"
#include "kolibri/compress.h"
#include "kolibri/formula.h"
#include "kolibri/semantic.h"
#include "kolibri/knowledge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Внутренние функции */
static int k_kgen_read_header(FILE *file, KolibriKgenHeader *header);
static int k_kgen_validate_header(const KolibriKgenHeader *header);
static uint32_t k_kgen_calculate_checksum(const void *data, size_t size);
static int k_kgen_decompress_level(KolibriKgenDocument *doc, 
                                    KolibriKgenLevel from_level,
                                    KolibriKgenLevel to_level);

/**
 * @brief Получение строкового описания ошибки
 */
const char *k_kgen_strerror(int error_code) {
    switch (error_code) {
        case KGEN_OK:
            return "Успех";
        case KGEN_ERR_INVALID_MAGIC:
            return "Неверное магическое число";
        case KGEN_ERR_INVALID_VERSION:
            return "Неподдерживаемая версия формата";
        case KGEN_ERR_CHECKSUM_MISMATCH:
            return "Несоответствие контрольной суммы";
        case KGEN_ERR_CORRUPTED_DATA:
            return "Повреждённые данные";
        case KGEN_ERR_OUT_OF_MEMORY:
            return "Недостаточно памяти";
        case KGEN_ERR_FILE_NOT_FOUND:
            return "Файл не найден";
        case KGEN_ERR_INVALID_LEVEL:
            return "Неверный уровень сжатия";
        case KGEN_ERR_DECOMPRESSION_FAILED:
            return "Ошибка декомпрессии";
        case KGEN_ERR_UNSUPPORTED_FORMAT:
            return "Неподдерживаемый формат";
        default:
            return "Неизвестная ошибка";
    }
}

/**
 * @brief Чтение заголовка файла .kgen
 */
static int k_kgen_read_header(FILE *file, KolibriKgenHeader *header) {
    if (!file || !header) {
        return KGEN_ERR_INVALID_LEVEL;
    }

    /* Чтение базовых полей */
    if (fread(&header->magic, sizeof(uint32_t), 1, file) != 1) {
        return KGEN_ERR_CORRUPTED_DATA;
    }

    if (fread(&header->version, sizeof(uint32_t), 1, file) != 1) {
        return KGEN_ERR_CORRUPTED_DATA;
    }

    if (fread(&header->original_size, sizeof(uint32_t), 1, file) != 1) {
        return KGEN_ERR_CORRUPTED_DATA;
    }

    if (fread(&header->compressed_size, sizeof(uint32_t), 1, file) != 1) {
        return KGEN_ERR_CORRUPTED_DATA;
    }

    if (fread(&header->l5_hash, sizeof(uint32_t), 1, file) != 1) {
        return KGEN_ERR_CORRUPTED_DATA;
    }

    if (fread(&header->l5_params, sizeof(uint16_t), 1, file) != 1) {
        return KGEN_ERR_CORRUPTED_DATA;
    }

    if (fread(&header->flags, sizeof(uint16_t), 1, file) != 1) {
        return KGEN_ERR_CORRUPTED_DATA;
    }

    if (fread(&header->timestamp, sizeof(uint64_t), 1, file) != 1) {
        return KGEN_ERR_CORRUPTED_DATA;
    }

    if (fread(&header->checksum, sizeof(uint32_t), 1, file) != 1) {
        return KGEN_ERR_CORRUPTED_DATA;
    }

    return KGEN_OK;
}

/**
 * @brief Валидация заголовка
 */
static int k_kgen_validate_header(const KolibriKgenHeader *header) {
    if (!header) {
        return KGEN_ERR_INVALID_MAGIC;
    }

    /* Проверка магического числа */
    if (header->magic != KGEN_MAGIC) {
        return KGEN_ERR_INVALID_MAGIC;
    }

    /* Проверка версии */
    uint32_t major_version = (header->version >> 16) & 0xFFFF;
    if (major_version > KGEN_VERSION_MAJOR) {
        return KGEN_ERR_INVALID_VERSION;
    }

    /* Проверка контрольной суммы заголовка (без самого checksum поля) */
    uint32_t calculated = k_kgen_calculate_checksum(header, sizeof(KolibriKgenHeader) - sizeof(uint32_t));
    if (calculated != header->checksum) {
        return KGEN_ERR_CHECKSUM_MISMATCH;
    }

    return KGEN_OK;
}

/**
 * @brief Расчёт контрольной суммы (простой CRC32-like алгоритм)
 */
static uint32_t k_kgen_calculate_checksum(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t checksum = 0xFFFFFFFF;

    for (size_t i = 0; i < size; i++) {
        checksum ^= bytes[i];
        for (int j = 0; j < 8; j++) {
            checksum = (checksum >> 1) ^ ((checksum & 1) ? 0xEDB88320 : 0);
        }
    }

    return ~checksum;
}

/**
 * @brief Инициализация структуры документа
 */
static void k_kgen_document_init(KolibriKgenDocument *doc) {
    if (!doc) return;

    memset(doc, 0, sizeof(KolibriKgenDocument));
    
    /* Выделение начальной ёмкости для массивов */
    doc->formulas_capacity = 64;
    doc->formulas = (KolibriFormula *)calloc(doc->formulas_capacity, sizeof(KolibriFormula));
    
    doc->patterns_capacity = 128;
    doc->patterns = (KolibriSemanticPattern *)calloc(doc->patterns_capacity, sizeof(KolibriSemanticPattern));
    
    doc->associations_capacity = 256;
    doc->associations = (KolibriTextHashAssoc *)calloc(doc->associations_capacity, sizeof(KolibriTextHashAssoc));

    if (!doc->formulas || !doc->patterns || !doc->associations) {
        doc->error_code = KGEN_ERR_OUT_OF_MEMORY;
        snprintf(doc->error_message, sizeof(doc->error_message), 
                 "Недостаточно памяти для инициализации");
    }
}

/**
 * @brief Парсинг файла .kgen
 */
int k_kgen_parse(const char *filename, KolibriKgenDocument *doc) {
    if (!filename || !doc) {
        return KGEN_ERR_INVALID_LEVEL;
    }

    FILE *file = fopen(filename, "rb");
    if (!file) {
        doc->error_code = KGEN_ERR_FILE_NOT_FOUND;
        snprintf(doc->error_message, sizeof(doc->error_message),
                 "Не удалось открыть файл: %s", filename);
        return KGEN_ERR_FILE_NOT_FOUND;
    }

    /* Инициализация документа */
    k_kgen_document_init(doc);
    if (doc->error_code != 0) {
        fclose(file);
        return doc->error_code;
    }

    /* Чтение заголовка */
    int ret = k_kgen_read_header(file, &doc->header);
    if (ret != KGEN_OK) {
        doc->error_code = ret;
        snprintf(doc->error_message, sizeof(doc->error_message),
                 "Ошибка чтения заголовка: %s", k_kgen_strerror(ret));
        fclose(file);
        return ret;
    }

    /* Валидация заголовка */
    ret = k_kgen_validate_header(&doc->header);
    if (ret != KGEN_OK) {
        doc->error_code = ret;
        snprintf(doc->error_message, sizeof(doc->error_message),
                 "Невалидный заголовок: %s", k_kgen_strerror(ret));
        fclose(file);
        return ret;
    }

    /* Заполнение метаданных */
    snprintf(doc->metadata.format_version, sizeof(doc->metadata.format_version),
             "%d.%d", KGEN_VERSION_MAJOR, KGEN_VERSION_MINOR);
    doc->metadata.original_size = doc->header.original_size;
    doc->metadata.compressed_size = doc->header.compressed_size;
    doc->metadata.compression_ratio = (double)doc->header.original_size / 
                                       (double)(doc->header.compressed_size + 1);

    /* Чтение данных уровней сжатия */
    /* TODO: Реализация чтения уровней L5→L4→L3→L2→L1 */
    /* Пока заглушка для этапа C.2 */
    
    doc->is_valid = 1;
    doc->error_code = KGEN_OK;

    fclose(file);
    return KGEN_OK;
}

/**
 * @brief Парсинг файла .kgen из памяти
 */
int k_kgen_parse_memory(const void *data, size_t size, KolibriKgenDocument *doc) {
    if (!data || size == 0 || !doc) {
        return KGEN_ERR_INVALID_LEVEL;
    }

    /* Временный файл в памяти */
    /* TODO: Оптимизировать для работы напрямую с памятью */
    
    /* Пока используем существующую реализацию */
    doc->error_code = KGEN_ERR_UNSUPPORTED_FORMAT;
    snprintf(doc->error_message, sizeof(doc->error_message),
             "Парсинг из памяти ещё не реализован");
    
    return KGEN_ERR_UNSUPPORTED_FORMAT;
}

/**
 * @brief Декомпрессия файла .kgen
 */
int k_kgen_decompress(KolibriKgenDocument *doc, void *output_buffer, size_t *output_size) {
    if (!doc || !output_size) {
        return KGEN_ERR_INVALID_LEVEL;
    }

    if (!doc->is_valid) {
        return KGEN_ERR_CORRUPTED_DATA;
    }

    /* Если буфер NULL, возвращаем требуемый размер */
    if (!output_buffer) {
        *output_size = doc->metadata.original_size;
        return KGEN_OK;
    }

    /* TODO: Реализация полной декомпрессии L5→RAW */
    /* Этап C.3 */
    
    /* Заглушка: копирование размера */
    if (*output_size < doc->metadata.original_size) {
        return KGEN_ERR_OUT_OF_MEMORY;
    }

    *output_size = doc->metadata.original_size;
    
    return KGEN_OK;
}

/**
 * @brief Извлечение знаний из документа .kgen
 */
int k_kgen_extract_knowledge(KolibriKgenDocument *doc, void *corpus) {
    if (!doc || !corpus) {
        return KGEN_ERR_INVALID_LEVEL;
    }

    if (!doc->is_valid) {
        return KGEN_ERR_CORRUPTED_DATA;
    }

    /* TODO: Интеграция с KolibriCorpusContext */
    /* Копирование формул и паттернов в корпус */
    
    return KGEN_OK;
}

/**
 * @brief Быстрый предпросмотр метаданных
 */
int k_kgen_preview(const char *filename, KolibriKgenMetadata *metadata) {
    if (!filename || !metadata) {
        return KGEN_ERR_INVALID_LEVEL;
    }

    FILE *file = fopen(filename, "rb");
    if (!file) {
        return KGEN_ERR_FILE_NOT_FOUND;
    }

    KolibriKgenHeader header;
    int ret = k_kgen_read_header(file, &header);
    
    fclose(file);

    if (ret != KGEN_OK) {
        return ret;
    }

    ret = k_kgen_validate_header(&header);
    if (ret != KGEN_OK) {
        return ret;
    }

    /* Заполнение метаданных */
    snprintf(metadata->format_version, sizeof(metadata->format_version),
             "%d.%d", KGEN_VERSION_MAJOR, KGEN_VERSION_MINOR);
    metadata->original_size = header.original_size;
    metadata->compressed_size = header.compressed_size;
    metadata->compression_ratio = (double)header.original_size / 
                                   (double)(header.compressed_size + 1);
    metadata->num_formulas = 0;  /* TODO: Читать из файла */
    metadata->num_patterns = 0;
    metadata->num_associations = 0;

    return KGEN_OK;
}

/**
 * @brief Валидация контрольных сумм
 */
int k_kgen_validate_checksums(const char *filename) {
    if (!filename) {
        return KGEN_ERR_INVALID_LEVEL;
    }

    FILE *file = fopen(filename, "rb");
    if (!file) {
        return KGEN_ERR_FILE_NOT_FOUND;
    }

    KolibriKgenHeader header;
    int ret = k_kgen_read_header(file, &header);
    
    fclose(file);

    if (ret != KGEN_OK) {
        return ret;
    }

    return k_kgen_validate_header(&header);
}

/**
 * @brief Освобождение ресурсов документа
 */
void k_kgen_document_free(KolibriKgenDocument *doc) {
    if (!doc) return;

    /* Освобождение массивов формул */
    if (doc->formulas) {
        for (size_t i = 0; i < doc->num_formulas; i++) {
            /* TODO: Освобождение внутренних структур формул */
        }
        free(doc->formulas);
        doc->formulas = NULL;
    }

    /* Освобождение массивов паттернов */
    if (doc->patterns) {
        for (size_t i = 0; i < doc->num_patterns; i++) {
            /* TODO: Освобождение внутренних структур паттернов */
        }
        free(doc->patterns);
        doc->patterns = NULL;
    }

    /* Освобождение ассоциаций */
    if (doc->associations) {
        for (size_t i = 0; i < doc->num_associations; i++) {
            if (doc->associations[i].text) {
                free(doc->associations[i].text);
            }
        }
        free(doc->associations);
        doc->associations = NULL;
    }

    /* Освобождение сырых данных */
    if (doc->raw_data) {
        free(doc->raw_data);
        doc->raw_data = NULL;
    }

    /* Освобождение графа знаний */
    if (doc->knowledge_graph) {
        /* TODO: Освобождение графа знаний */
        doc->knowledge_graph = NULL;
    }

    memset(doc, 0, sizeof(KolibriKgenDocument));
}

/* ============================================================================
 * CLI инструменты (Компонент C.6)
 * ============================================================================ */

/**
 * @brief CLI: Инспекция файла .kgen
 */
int k_kgen_cli_inspect(const char *filename, int verbose) {
    if (!filename) {
        fprintf(stderr, "Ошибка: не указан файл\n");
        return 1;
    }

    KolibriKgenMetadata metadata;
    int ret = k_kgen_preview(filename, &metadata);
    
    if (ret != KGEN_OK) {
        fprintf(stderr, "Ошибка при инспекции: %s\n", k_kgen_strerror(ret));
        return 1;
    }

    printf("=== Метаданные файла .kgen ===\n");
    printf("Версия формата: %s\n", metadata.format_version);
    printf("Исходный размер: %u байт\n", metadata.original_size);
    printf("Сжатый размер: %u байт\n", metadata.compressed_size);
    printf("Коэффициент сжатия: %.2f:1\n", metadata.compression_ratio);
    
    if (verbose) {
        printf("\n=== Детальная информация ===\n");
        printf("Количество формул: %u\n", metadata.num_formulas);
        printf("Количество паттернов: %u\n", metadata.num_patterns);
        printf("Количество ассоциаций: %u\n", metadata.num_associations);
    }

    return 0;
}

/**
 * @brief CLI: Извлечение содержимого
 */
int k_kgen_cli_extract(const char *input_file, const char *output_file) {
    if (!input_file || !output_file) {
        fprintf(stderr, "Ошибка: не указаны файлы\n");
        return 1;
    }

    KolibriKgenDocument doc;
    int ret = k_kgen_parse(input_file, &doc);
    
    if (ret != KGEN_OK) {
        fprintf(stderr, "Ошибка парсинга: %s\n", k_kgen_strerror(ret));
        k_kgen_document_free(&doc);
        return 1;
    }

    /* Определение размера выходных данных */
    size_t output_size = 0;
    ret = k_kgen_decompress(&doc, NULL, &output_size);
    
    if (ret != KGEN_OK) {
        fprintf(stderr, "Ошибка определения размера: %s\n", k_kgen_strerror(ret));
        k_kgen_document_free(&doc);
        return 1;
    }

    /* Выделение буфера */
    void *buffer = malloc(output_size);
    if (!buffer) {
        fprintf(stderr, "Ошибка выделения памяти\n");
        k_kgen_document_free(&doc);
        return 1;
    }

    /* Декомпрессия */
    ret = k_kgen_decompress(&doc, buffer, &output_size);
    
    if (ret != KGEN_OK) {
        fprintf(stderr, "Ошибка декомпрессии: %s\n", k_kgen_strerror(ret));
        free(buffer);
        k_kgen_document_free(&doc);
        return 1;
    }

    /* Запись в файл */
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        fprintf(stderr, "Ошибка создания файла: %s\n", strerror(errno));
        free(buffer);
        k_kgen_document_free(&doc);
        return 1;
    }

    size_t written = fwrite(buffer, 1, output_size, out);
    fclose(out);
    free(buffer);
    k_kgen_document_free(&doc);

    if (written != output_size) {
        fprintf(stderr, "Ошибка записи: записано %zu из %zu байт\n", written, output_size);
        return 1;
    }

    printf("Успешно извлечено %zu байт в %s\n", output_size, output_file);
    return 0;
}

/**
 * @brief CLI: Слияние файлов
 */
int k_kgen_cli_merge(const char **files, int num_files, const char *output_file) {
    (void)files;
    (void)num_files;
    (void)output_file;
    
    fprintf(stderr, "Функция слияния ещё не реализована\n");
    return 1;
}

/**
 * @brief CLI: Поиск по директории
 */
int k_kgen_cli_search(const char *pattern, const char *directory) {
    (void)pattern;
    (void)directory;
    
    fprintf(stderr, "Функция поиска ещё не реализована\n");
    return 1;
}
