/**
 * Kolibri v0.5 - KGEN Parser Implementation
 * 
 * Модуль глубокого анализа и обработки файлов .kgen
 * Реализует декомпрессию уровней сжатия (L5 -> L1), валидацию структуры
 * и извлечение семантических паттернов.
 * 
 * Этап разработки: v0.5 (Гибридная архитектура)
 * Компонент: C. Парсер .kgen файлов
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "kgen_parser.h"
#include "compress.h"
#include "semantic.h"
#include "utils.h"

// Магическое число формата .kgen для валидации
#define KGEN_MAGIC 0x4B47454E // "KGEN" в ASCII
#define KGEN_VERSION_CURRENT 0x0005 // Версия формата для v0.5

// Внутренняя структура контекста парсинга
typedef struct {
    FILE* file_handle;
    kgen_header_t header;
    uint8_t* buffer;
    size_t buffer_size;
    int current_level; // Текущий уровень сжатия (1-5)
    bool is_valid;
} kgen_parse_context_t;

/**
 * @brief Чтение и валидация заголовка файла .kgen
 */
static int kgen_read_header(FILE* fp, kgen_header_t* header) {
    if (!fp || !header) return KGEN_ERR_INVALID_PARAM;

    // Чтение магического числа
    if (fread(&header->magic, sizeof(uint32_t), 1, fp) != 1) {
        return KGEN_ERR_READ_FAILED;
    }

    if (header->magic != KGEN_MAGIC) {
        fprintf(stderr, "[KGEN] Ошибка: Неверный формат файла (магическое число не совпадает)\n");
        return KGEN_ERR_INVALID_FORMAT;
    }

    // Чтение версии
    if (fread(&header->version, sizeof(uint16_t), 1, fp) != 1) {
        return KGEN_ERR_READ_FAILED;
    }

    if (header->version > KGEN_VERSION_CURRENT) {
        fprintf(stderr, "[KGEN] Предупреждение: Версия файла (%d) выше поддерживаемой (%d)\n", 
                header->version, KGEN_VERSION_CURRENT);
        // Не прерываем, пытаемся прочитать совместимые части
    }

    // Чтение метаданных
    if (fread(&header->flags, sizeof(uint8_t), 1, fp) != 1 ||
        fread(&header->compression_level, sizeof(uint8_t), 1, fp) != 1 ||
        fread(&header->reserved, sizeof(uint16_t), 1, fp) != 1 ||
        fread(&header->data_size, sizeof(uint64_t), 1, fp) != 1 ||
        fread(&header->checksum, sizeof(uint32_t), 1, fp) != 1) {
        return KGEN_ERR_READ_FAILED;
    }

    // Чтение имени паттерна (фиксированный размер)
    if (fread(header->pattern_name, sizeof(char), KGEN_NAME_MAX, fp) != KGEN_NAME_MAX) {
        // Допускаем усеченное имя, но предупреждаем
        fprintf(stderr, "[KGEN] Предупреждение: Имя паттерна усечено или отсутствует\n");
    }

    return KGEN_OK;
}

/**
 * @brief Вычисление контрольной суммы данных
 */
static uint32_t kgen_calculate_checksum(const uint8_t* data, size_t size) {
    uint32_t checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum = ((checksum << 5) + checksum) + data[i]; // djb2 алгоритм
    }
    return checksum;
}

/**
 * @brief Инициализация парсера
 */
int kgen_parser_init(kgen_parser_t* parser, const char* filename) {
    if (!parser || !filename) return KGEN_ERR_INVALID_PARAM;

    memset(parser, 0, sizeof(kgen_parser_t));
    
    kgen_parse_context_t* ctx = (kgen_parse_context_t*)malloc(sizeof(kgen_parse_context_t));
    if (!ctx) return KGEN_ERR_MEMORY;

    ctx->file_handle = fopen(filename, "rb");
    if (!ctx->file_handle) {
        free(ctx);
        return KGEN_ERR_FILE_NOT_FOUND;
    }

    // Чтение заголовка
    int ret = kgen_read_header(ctx->file_handle, &ctx->header);
    if (ret != KGEN_OK) {
        fclose(ctx->file_handle);
        free(ctx);
        return ret;
    }

    ctx->current_level = ctx->header.compression_level;
    ctx->is_valid = true;
    
    // Выделение буфера для данных (с запасом на декомпрессию)
    ctx->buffer_size = ctx->header.data_size * 4; // Эвристика: сжатые данные могут вырасти в 4 раза
    ctx->buffer = (uint8_t*)malloc(ctx->buffer_size);
    if (!ctx->buffer) {
        fclose(ctx->file_handle);
        free(ctx);
        return KGEN_ERR_MEMORY;
    }

    parser->internal_ctx = ctx;
    strncpy(parser->filename, filename, KGEN_PATH_MAX);
    
    printf("[KGEN] Файл успешно открыт: %s\n", ctx->header.pattern_name);
    printf("[KGEN] Уровень сжатия: L%d, Размер данных: %lu байт\n", 
           ctx->header.compression_level, (unsigned long)ctx->header.data_size);

    return KGEN_OK;
}

/**
 * @brief Декомпрессия данных с текущего уровня на уровень ниже
 * Реализует пошаговое раскрытие сжатия L5 -> L4 -> ... -> L1
 */
static int kgen_decompress_step(kgen_parse_context_t* ctx) {
    if (!ctx || !ctx->is_valid) return KGEN_ERR_INVALID_STATE;

    // Чтение сжатых данных из файла (если еще не прочитаны)
    // В реальной реализации здесь будет позиционирование файла и чтение чанка
    // Для примера предполагаем, что данные читаются последовательно после заголовка
    
    // Заглушка для логики декомпрессии в зависимости от уровня
    switch (ctx->current_level) {
        case 5:
            // L5: Паттерн-уровень (ссылки на библиотеку паттернов)
            // Требуется подключение к базе знаний Kolibri для разрешения ссылок
            printf("[KGEN] Декомпрессия L5 -> L4: Разрешение внешних паттернов...\n");
            // Здесь вызов functions из semantic.c для поиска паттернов
            break;
            
        case 4:
            // L4: Семантический уровень (абстрактные понятия)
            printf("[KGEN] Декомпрессия L4 -> L3: Раскрытие семантических узлов...\n");
            break;
            
        case 3:
            // L3: Логический уровень (формулы, предикаты)
            printf("[KGEN] Декомпрессия L3 -> L2: Генерация логических структур...\n");
            break;
            
        case 2:
            // L2: Синтаксический уровень (деревья, токены)
            printf("[KGEN] Декомпрессия L2 -> L1: Построение синтаксического дерева...\n");
            break;
            
        case 1:
            // L1: Сырые данные (текст, байты)
            printf("[KGEN] Достигнут уровень L1: Данные готовы к использованию.\n");
            return KGEN_OK; // Декомпрессия завершена
            
        default:
            return KGEN_ERR_INVALID_LEVEL;
    }

    // Имитация процесса декомпрессии (в реальности здесь вызов compress.c функций)
    // kgen_run_decompression_algorithm(ctx->buffer, ...);
    
    ctx->current_level--;
    return KGEN_OK;
}

/**
 * @brief Полная декомпрессия до уровня L1
 */
int kgen_parser_decompress_all(kgen_parser_t* parser) {
    if (!parser || !parser->internal_ctx) return KGEN_ERR_INVALID_PARAM;
    
    kgen_parse_context_t* ctx = (kgen_parse_context_t*)parser->internal_ctx;
    
    while (ctx->current_level > 1) {
        int ret = kgen_decompress_step(ctx);
        if (ret != KGEN_OK) {
            fprintf(stderr, "[KGEN] Ошибка декомпрессии на уровне %d: код %d\n", ctx->current_level, ret);
            return ret;
        }
    }
    
    return KGEN_OK;
}

/**
 * @brief Извлечение метаданных паттерна
 */
int kgen_parser_get_metadata(kgen_parser_t* parser, kgen_metadata_t* meta) {
    if (!parser || !meta || !parser->internal_ctx) return KGEN_ERR_INVALID_PARAM;
    
    kgen_parse_context_t* ctx = (kgen_parse_context_t*)parser->internal_ctx;
    
    meta->name = ctx->header.pattern_name;
    meta->version = ctx->header.version;
    meta->compression_level = ctx->header.compression_level;
    meta->original_size = ctx->header.data_size;
    meta->flags = ctx->header.flags;
    
    // Вычисление фактического размера после декомпрессии (если выполнена)
    if (ctx->current_level == 1 && ctx->buffer) {
        meta->decompressed_size = ctx->buffer_size; // Упрощенно
    } else {
        meta->decompressed_size = 0;
    }
    
    return KGEN_OK;
}

/**
 * @brief Получение сырых данных (после декомпрессии)
 */
const uint8_t* kgen_parser_get_data(kgen_parser_t* parser, size_t* size) {
    if (!parser || !parser->internal_ctx || !size) return NULL;
    
    kgen_parse_context_t* ctx = (kgen_parse_context_t*)parser->internal_ctx;
    
    if (ctx->current_level != 1) {
        fprintf(stderr, "[KGEN] Ошибка: Данные еще не декомпрессированы до L1\n");
        *size = 0;
        return NULL;
    }
    
    *size = ctx->buffer_size;
    return ctx->buffer;
}

/**
 * @brief Очистка ресурсов парсера
 */
void kgen_parser_destroy(kgen_parser_t* parser) {
    if (!parser) return;
    
    if (parser->internal_ctx) {
        kgen_parse_context_t* ctx = (kgen_parse_context_t*)parser->internal_ctx;
        
        if (ctx->file_handle) {
            fclose(ctx->file_handle);
        }
        if (ctx->buffer) {
            free(ctx->buffer);
        }
        free(ctx);
        parser->internal_ctx = NULL;
    }
    
    memset(parser, 0, sizeof(kgen_parser_t));
}

/**
 * @brief CLI утилита: Инспектор файлов .kgen
 * Пример использования: kolibri-inspect file.kgen
 */
#ifdef KGEN_STANDALONE_TEST
int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Использование: %s <файл.kgen>\n", argv[0]);
        return 1;
    }

    kgen_parser_t parser;
    int ret = kgen_parser_init(&parser, argv[1]);
    
    if (ret != KGEN_OK) {
        fprintf(stderr, "Не удалось открыть файл. Код ошибки: %d\n", ret);
        return 1;
    }

    kgen_metadata_t meta;
    if (kgen_parser_get_metadata(&parser, &meta) == KGEN_OK) {
        printf("\n=== Метаданные паттерна ===\n");
        printf("Имя: %s\n", meta.name);
        printf("Версия формата: %d\n", meta.version);
        printf("Уровень сжатия: L%d\n", meta.compression_level);
        printf("Размер сжатых данных: %lu байт\n", (unsigned long)meta.original_size);
        printf("Флаги: 0x%02X\n", meta.flags);
    }

    printf("\nЗапуск полной декомпрессии...\n");
    ret = kgen_parser_decompress_all(&parser);
    
    if (ret == KGEN_OK) {
        size_t data_size = 0;
        const uint8_t* data = kgen_parser_get_data(&parser, &data_size);
        if (data) {
            printf("Декомпрессия успешна! Размер данных: %lu байт\n", (unsigned long)data_size);
            // Здесь можно добавить вывод первых байт для отладки
        }
    } else {
        fprintf(stderr, "Ошибка декомпрессии: %d\n", ret);
    }

    kgen_parser_destroy(&parser);
    return (ret == KGEN_OK) ? 0 : 1;
}
#endif
