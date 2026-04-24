/**
 * Kolibri v0.5 - KGEN Parser Header
 * 
 * Заголовочный файл для модуля глубокого анализа файлов .kgen
 * Определяет структуры данных, константы ошибок и публичный API парсера.
 * 
 * Этап разработки: v0.5 (Гибридная архитектура)
 * Компонент: C. Парсер .kgen файлов
 */

#ifndef KGEN_PARSER_H
#define KGEN_PARSER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Константы формата
#define KGEN_NAME_MAX 64
#define KGEN_PATH_MAX 256
#define KGEN_MAX_LEVELS 5

// Коды возврата
typedef enum {
    KGEN_OK = 0,
    KGEN_ERR_INVALID_PARAM = -1,
    KGEN_ERR_MEMORY = -2,
    KGEN_ERR_FILE_NOT_FOUND = -3,
    KGEN_ERR_READ_FAILED = -4,
    KGEN_ERR_INVALID_FORMAT = -5,
    KGEN_ERR_INVALID_LEVEL = -6,
    KGEN_ERR_INVALID_STATE = -7,
    KGEN_ERR_CHECKSUM_MISMATCH = -8
} kgen_error_t;

// Флаги паттерна
typedef enum {
    KGEN_FLAG_NONE = 0x00,
    KGEN_FLAG_ENCRYPTED = 0x01,
    KGEN_FLAG_COMPRESSED = 0x02,
    KGEN_FLAG_SEMANTIC = 0x04,
    KGEN_FLAG_LOGICAL = 0x08,
    KGEN_FLAG_EXTERNAL_REFS = 0x10
} kgen_flags_t;

// Структура заголовка файла .kgen
typedef struct {
    uint32_t magic;              // Магическое число "KGEN"
    uint16_t version;            // Версия формата
    uint8_t flags;               // Флаги (kgen_flags_t)
    uint8_t compression_level;   // Уровень сжатия (1-5)
    uint16_t reserved;           // Зарезервировано
    uint64_t data_size;          // Размер сжатых данных
    uint32_t checksum;           // Контрольная сумма
    char pattern_name[KGEN_NAME_MAX]; // Имя паттерна
} kgen_header_t;

// Метаданные извлеченного паттерна
typedef struct {
    const char* name;
    uint16_t version;
    int compression_level;
    uint64_t original_size;
    uint64_t decompressed_size;
    uint8_t flags;
} kgen_metadata_t;

// Основная структура парсера (непрозрачный тип)
typedef struct {
    char filename[KGEN_PATH_MAX];
    void* internal_ctx;  // Скрытая реализация контекста
} kgen_parser_t;

/**
 * @brief Инициализация парсера и открытие файла .kgen
 * @param parser Указатель на структуру парсера
 * @param filename Путь к файлу .kgen
 * @return KGEN_OK при успехе, код ошибки иначе
 */
int kgen_parser_init(kgen_parser_t* parser, const char* filename);

/**
 * @brief Полная декомпрессия данных до уровня L1 (сырые данные)
 * @param parser Инициализированный парсер
 * @return KGEN_OK при успехе, код ошибки иначе
 */
int kgen_parser_decompress_all(kgen_parser_t* parser);

/**
 * @brief Извлечение метаданных паттерна
 * @param parser Инициализированный парсер
 * @param meta Указатель на структуру для заполнения метаданными
 * @return KGEN_OK при успехе, код ошибки иначе
 */
int kgen_parser_get_metadata(kgen_parser_t* parser, kgen_metadata_t* meta);

/**
 * @brief Получение указателя на сырые данные после декомпрессии
 * @param parser Инициализированный и декомпрессированный парсер
 * @param size Указатель для сохранения размера данных
 * @return Указатель на буфер данных или NULL при ошибке
 */
const uint8_t* kgen_parser_get_data(kgen_parser_t* parser, size_t* size);

/**
 * @brief Очистка ресурсов и закрытие парсера
 * @param parser Указатель на парсер
 */
void kgen_parser_destroy(kgen_parser_t* parser);

#ifdef __cplusplus
}
#endif

#endif // KGEN_PARSER_H
