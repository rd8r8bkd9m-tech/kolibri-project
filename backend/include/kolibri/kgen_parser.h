/**
 * @file kgen_parser.h
 * @brief Парсер файлов формата .kgen для Kolibri v0.5
 * 
 * Глубокий анализ и обработка файлов .kgen для извлечения знаний,
 * мета-данных и сжатых паттернов.
 */

#ifndef KOLIBRI_KGEN_PARSER_H
#define KOLIBRI_KGEN_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include "formula.h"
#include "semantic.h"
#include "compress.h"
#include "knowledge.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Магическое число формата .kgen
 */
#define KGEN_MAGIC 0x4B47454E  /* "KGEN" */

/**
 * @brief Версия формата .kgen
 */
#define KGEN_VERSION_MAJOR 1
#define KGEN_VERSION_MINOR 0

/**
 * @brief Уровни сжатия в файле .kgen
 */
typedef enum {
    KGEN_LEVEL_RAW = 0,      // Исходные данные
    KGEN_LEVEL_L1 = 1,       // Базовое сжатие
    KGEN_LEVEL_L2 = 2,       // Семантическое сжатие
    KGEN_LEVEL_L3 = 3,       // Формульное представление
    KGEN_LEVEL_L4 = 4,       // Паттерны высокого уровня
    KGEN_LEVEL_L5 = 5        // Максимальное сжатие (хеш + параметры)
} KolibriKgenLevel;

/**
 * @brief Заголовок файла .kgen
 */
typedef struct {
    uint32_t magic;              // 0x4B47454E "KGEN"
    uint32_t version;            // Версия формата
    uint32_t original_size;      // Размер исходных данных
    uint32_t compressed_size;    // Размер сжатых данных
    uint32_t l5_hash;            // Хеш уровня L5
    uint16_t l5_params;          // Параметры уровня L5
    uint16_t flags;              // Флаги формата
    uint64_t timestamp;          // Время создания
    uint32_t checksum;           // Контрольная сумма заголовка
} KolibriKgenHeader;

/**
 * @brief Метаданные файла .kgen
 */
typedef struct {
    char format_version[16];     // Строка версии формата
    uint32_t original_size;
    uint32_t compressed_size;
    double compression_ratio;    // Коэффициент сжатия
    
    uint32_t num_formulas;       // Количество формул
    uint32_t num_patterns;       // Количество семантических паттернов
    uint32_t num_associations;   // Количество ассоциаций текст↔хеш
    
    KolibriCompressStats compression_stats;
} KolibriKgenMetadata;

/**
 * @brief Документ .kgen (результат парсинга)
 */
typedef struct {
    KolibriKgenHeader header;
    KolibriKgenMetadata metadata;
    
    // Извлечённые данные
    KolibriFormula *formulas;         // Массив формул
    size_t num_formulas;
    size_t formulas_capacity;
    
    KolibriSemanticPattern *patterns; // Массив семантических паттернов
    size_t num_patterns;
    size_t patterns_capacity;
    
    KolibriTextHashAssoc *associations; // Ассоциации текст↔хеш
    size_t num_associations;
    size_t associations_capacity;
    
    // Восстановленные данные
    void *raw_data;              // Исходные данные (после декомпрессии)
    size_t raw_data_size;
    
    // Граф знаний (опционально)
    void *knowledge_graph;       // KolibriKnowledgeGraph *
    
    // Состояние парсера
    int is_valid;                // Флаг валидности документа
    int error_code;              // Код ошибки при парсинге
    char error_message[256];     // Сообщение об ошибке
} KolibriKgenDocument;

/**
 * @brief Контекст для потокового парсинга больших файлов
 */
typedef struct {
    FILE *file;
    KolibriKgenDocument *doc;
    KolibriKgenLevel current_level;
    size_t bytes_processed;
    size_t total_bytes;
    int state;                   // Состояние машины состояний парсера
} KolibriKgenParserContext;

/**
 * @brief Коды ошибок парсера
 */
typedef enum {
    KGEN_OK = 0,
    KGEN_ERR_INVALID_MAGIC = -1,
    KGEN_ERR_INVALID_VERSION = -2,
    KGEN_ERR_CHECKSUM_MISMATCH = -3,
    KGEN_ERR_CORRUPTED_DATA = -4,
    KGEN_ERR_OUT_OF_MEMORY = -5,
    KGEN_ERR_FILE_NOT_FOUND = -6,
    KGEN_ERR_INVALID_LEVEL = -7,
    KGEN_ERR_DECOMPRESSION_FAILED = -8,
    KGEN_ERR_UNSUPPORTED_FORMAT = -9
} KolibriKgenError;

/**
 * @brief Основной API парсера
 */

/**
 * @brief Парсинг файла .kgen
 * @param filename Путь к файлу .kgen
 * @param doc Указатель на структуру документа для заполнения
 * @return Код ошибки (KGEN_OK при успехе)
 */
int k_kgen_parse(const char *filename, KolibriKgenDocument *doc);

/**
 * @brief Парсинг файла .kgen из памяти
 * @param data Указатель на данные в памяти
 * @param size Размер данных
 * @param doc Указатель на структуру документа для заполнения
 * @return Код ошибки (KGEN_OK при успехе)
 */
int k_kgen_parse_memory(const void *data, size_t size, KolibriKgenDocument *doc);

/**
 * @brief Инициализация потокового парсера
 * @param filename Путь к файлу
 * @param context Контекст парсера
 * @return Код ошибки
 */
int k_kgen_parser_init(const char *filename, KolibriKgenParserContext *context);

/**
 * @brief Чтение следующего чанка данных при потоковом парсинге
 * @param context Контекст парсера
 * @return Код ошибки
 */
int k_kgen_parser_read_chunk(KolibriKgenParserContext *context);

/**
 * @brief Завершение потокового парсинга
 * @param context Контекст парсера
 * @return Код ошибки
 */
int k_kgen_parser_finalize(KolibriKgenParserContext *context);

/**
 * @brief Декомпрессия файла .kgen с восстановлением исходных данных
 * @param doc Документ .kgen
 * @param output_buffer Буфер для выходных данных (может быть NULL для получения размера)
 * @param output_size Размер буфера / фактический размер данных
 * @return Код ошибки
 */
int k_kgen_decompress(KolibriKgenDocument *doc, void *output_buffer, size_t *output_size);

/**
 * @brief Извлечение знаний из документа .kgen в корпус
 * @param doc Документ .kgen
 * @param corpus Контекст корпуса для добавления знаний
 * @return Код ошибки
 */
int k_kgen_extract_knowledge(KolibriKgenDocument *doc, void *corpus);

/**
 * @brief Построение графа знаний из документа .kgen
 * @param doc Документ .kgen
 * @return Указатель на граф знаний или NULL при ошибке
 */
void *k_kgen_build_knowledge_graph(KolibriKgenDocument *doc);

/**
 * @brief Поиск паттернов в документе .kgen
 * @param doc Документ .kgen
 * @param query Поисковый запрос
 * @param results Массив результатов поиска
 * @param max_results Максимальное количество результатов
 * @return Количество найденных результатов
 */
int k_kgen_search(KolibriKgenDocument *doc, const char *query, 
                  void **results, int max_results);

/**
 * @brief Быстрый предпросмотр метаданных без полной декомпрессии
 * @param filename Путь к файлу .kgen
 * @param metadata Структура для заполнения метаданными
 * @return Код ошибки
 */
int k_kgen_preview(const char *filename, KolibriKgenMetadata *metadata);

/**
 * @brief Валидация контрольных сумм файла .kgen
 * @param filename Путь к файлу
 * @return KGEN_OK если валиден, иначе код ошибки
 */
int k_kgen_validate_checksums(const char *filename);

/**
 * @brief Получение строкового описания ошибки
 * @param error_code Код ошибки
 * @return Строковое описание
 */
const char *k_kgen_strerror(int error_code);

/**
 * @brief Освобождение ресурсов документа .kgen
 * @param doc Документ для освобождения
 */
void k_kgen_document_free(KolibriKgenDocument *doc);

/**
 * @brief Инструменты CLI (для kolibri_kgen утилиты)
 */

/**
 * @brief Инспекция файла .kgen (вывод метаданных)
 * @param filename Путь к файлу
 * @param verbose Подробный вывод
 * @return 0 при успехе, иначе код ошибки
 */
int k_kgen_cli_inspect(const char *filename, int verbose);

/**
 * @brief Извлечение содержимого файла .kgen
 * @param input_file Входной файл .kgen
 * @param output_file Выходной файл
 * @return 0 при успехе, иначе код ошибки
 */
int k_kgen_cli_extract(const char *input_file, const char *output_file);

/**
 * @brief Слияние нескольких файлов .kgen
 * @param files Массив входных файлов
 * @param num_files Количество файлов
 * @param output_file Выходной файл
 * @return 0 при успехе, иначе код ошибки
 */
int k_kgen_cli_merge(const char **files, int num_files, const char *output_file);

/**
 * @brief Поиск по директории с файлами .kgen
 * @param pattern Шаблон поиска
 * @param directory Директория для поиска
 * @return 0 при успехе, иначе код ошибки
 */
int k_kgen_cli_search(const char *pattern, const char *directory);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_KGEN_PARSER_H */
