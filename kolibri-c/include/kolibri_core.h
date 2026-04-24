#ifndef KOLIBRI_CORE_H
#define KOLIBRI_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Версия ядра
#define KOLIBRI_VERSION_MAJOR 0
#define KOLIBRI_VERSION_MINOR 1
#define KOLIBRI_VERSION_PATCH 0

// Максимальный размер блока для сжатия
#define KOLIBRI_MAX_BLOCK_SIZE (1024 * 1024) // 1MB

// Типы операций
typedef enum {
    KOLIBRI_OP_COMPRESS = 0,
    KOLIBRI_OP_DECOMPRESS = 1,
    KOLIBRI_OP_ANALYZE = 2,
    KOLIBRI_OP_OPTIMIZE = 3
} KolibriOperation;

// Статус выполнения
typedef enum {
    KOLIBRI_STATUS_OK = 0,
    KOLIBRI_STATUS_ERROR = -1,
    KOLIBRI_STATUS_INVALID_INPUT = -2,
    KOLIBRI_STATUS_OUT_OF_MEMORY = -3,
    KOLIBRI_STATUS_NOT_SUPPORTED = -4
} KolibriStatus;

// Структура результата операции
typedef struct {
    KolibriStatus status;
    uint8_t* data;
    size_t size;
    size_t original_size;
    float compression_ratio;
    double processing_time_ms;
    char* error_message;
} KolibriResult;

// Структура статистики
typedef struct {
    size_t total_operations;
    size_t total_compressed;
    size_t total_decompressed;
    size_t total_bytes_saved;
    double avg_compression_ratio;
    double total_processing_time_ms;
} KolibriStats;

// Основное состояние движка
typedef struct {
    bool initialized;
    int optimization_level;
    bool use_multithreading;
    size_t thread_count;
    KolibriStats stats;
    void* internal_state; // Для будущих расширений (AGI, нейросети)
} KolibriEngine;

// Инициализация движка
KolibriStatus kolibri_init(KolibriEngine* engine, int optimization_level);

// Очистка ресурсов
void kolibri_cleanup(KolibriEngine* engine);

// Основная функция обработки
KolibriResult kolibri_process(KolibriEngine* engine, 
                              const uint8_t* input_data, 
                              size_t input_size, 
                              KolibriOperation operation);

// Получение статистики
KolibriStats kolibri_get_stats(KolibriEngine* engine);

// Сброс статистики
void kolibri_reset_stats(KolibriEngine* engine);

// Установка количества потоков
KolibriStatus kolibri_set_thread_count(KolibriEngine* engine, size_t count);

// Вспомогательные функции для работы с результатами
void kolibri_free_result(KolibriResult* result);
const char* kolibri_status_to_string(KolibriStatus status);

#endif // KOLIBRI_CORE_H
