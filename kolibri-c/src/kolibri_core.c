#include "kolibri_core.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

// Простая реализация алгоритма сжатия (заглушка для демонстрации)
// В реальной версии здесь будет оптимизированный алгоритм Kolibri
static uint8_t* simple_compress(const uint8_t* input, size_t input_size, size_t* output_size) {
    // Для демонстрации: просто копируем данные с небольшим заголовком
    // В реальности здесь будет сложный алгоритм сжатия
    *output_size = input_size + 16; // Добавляем место под заголовок
    uint8_t* output = (uint8_t*)malloc(*output_size);
    if (!output) return NULL;
    
    // Заголовок: "KGEN" + размер оригинала
    memcpy(output, "KGEN", 4);
    uint32_t orig_size = (uint32_t)input_size;
    memcpy(output + 4, &orig_size, sizeof(uint32_t));
    memset(output + 8, 0, 8); // Резерв
    
    // Копируем данные (в реальности - сжатые)
    memcpy(output + 16, input, input_size);
    
    return output;
}

static uint8_t* simple_decompress(const uint8_t* input, size_t input_size, size_t* output_size) {
    // Проверка заголовка
    if (input_size < 16 || memcmp(input, "KGEN", 4) != 0) {
        return NULL;
    }
    
    // Читаем оригинальный размер
    uint32_t orig_size;
    memcpy(&orig_size, input + 4, sizeof(uint32_t));
    
    *output_size = orig_size;
    uint8_t* output = (uint8_t*)malloc(orig_size);
    if (!output) return NULL;
    
    // Копируем данные (в реальности - распаковываем)
    memcpy(output, input + 16, orig_size);
    
    return output;
}

KolibriStatus kolibri_init(KolibriEngine* engine, int optimization_level) {
    if (!engine) return KOLIBRI_STATUS_INVALID_INPUT;
    
    engine->initialized = true;
    engine->optimization_level = optimization_level;
    engine->use_multithreading = false; // Пока отключено
    engine->thread_count = 1;
    engine->internal_state = NULL;
    
    // Инициализация статистики
    memset(&engine->stats, 0, sizeof(KolibriStats));
    
    return KOLIBRI_STATUS_OK;
}

void kolibri_cleanup(KolibriEngine* engine) {
    if (!engine || !engine->initialized) return;
    
    engine->initialized = false;
    if (engine->internal_state) {
        free(engine->internal_state);
        engine->internal_state = NULL;
    }
}

KolibriResult kolibri_process(KolibriEngine* engine, 
                              const uint8_t* input_data, 
                              size_t input_size, 
                              KolibriOperation operation) {
    KolibriResult result = {0};
    
    if (!engine || !engine->initialized) {
        result.status = KOLIBRI_STATUS_ERROR;
        result.error_message = "Engine not initialized";
        return result;
    }
    
    if (!input_data || input_size == 0) {
        result.status = KOLIBRI_STATUS_INVALID_INPUT;
        result.error_message = "Invalid input data";
        return result;
    }
    
    clock_t start = clock();
    
    switch (operation) {
        case KOLIBRI_OP_COMPRESS: {
            result.data = simple_compress(input_data, input_size, &result.size);
            if (!result.data) {
                result.status = KOLIBRI_STATUS_OUT_OF_MEMORY;
                result.error_message = "Failed to allocate memory for compression";
                return result;
            }
            result.original_size = input_size;
            result.compression_ratio = (float)input_size / (float)result.size;
            engine->stats.total_compressed++;
            engine->stats.total_bytes_saved += (input_size > result.size) ? (input_size - result.size) : 0;
            break;
        }
        
        case KOLIBRI_OP_DECOMPRESS: {
            result.data = simple_decompress(input_data, input_size, &result.size);
            if (!result.data) {
                result.status = KOLIBRI_STATUS_INVALID_INPUT;
                result.error_message = "Invalid compressed data or decompression failed";
                return result;
            }
            result.original_size = input_size;
            result.compression_ratio = (float)result.size / (float)input_size;
            engine->stats.total_decompressed++;
            break;
        }
        
        case KOLIBRI_OP_ANALYZE:
        case KOLIBRI_OP_OPTIMIZE:
            result.status = KOLIBRI_STATUS_NOT_SUPPORTED;
            result.error_message = "Operation not supported in this version";
            return result;
            
        default:
            result.status = KOLIBRI_STATUS_INVALID_INPUT;
            result.error_message = "Unknown operation";
            return result;
    }
    
    clock_t end = clock();
    result.processing_time_ms = ((double)(end - start)) / CLOCKS_PER_SEC * 1000.0;
    
    // Обновление статистики
    engine->stats.total_operations++;
    engine->stats.total_processing_time_ms += result.processing_time_ms;
    
    // Пересчет среднего коэффициента
    if (engine->stats.total_operations > 0) {
        engine->stats.avg_compression_ratio = 
            (engine->stats.avg_compression_ratio * (engine->stats.total_operations - 1) + 
             result.compression_ratio) / engine->stats.total_operations;
    }
    
    result.status = KOLIBRI_STATUS_OK;
    return result;
}

KolibriStats kolibri_get_stats(KolibriEngine* engine) {
    if (!engine || !engine->initialized) {
        KolibriStats empty = {0};
        return empty;
    }
    return engine->stats;
}

void kolibri_reset_stats(KolibriEngine* engine) {
    if (!engine || !engine->initialized) return;
    memset(&engine->stats, 0, sizeof(KolibriStats));
}

KolibriStatus kolibri_set_thread_count(KolibriEngine* engine, size_t count) {
    if (!engine || !engine->initialized) return KOLIBRI_STATUS_ERROR;
    if (count == 0) return KOLIBRI_STATUS_INVALID_INPUT;
    
    engine->thread_count = count;
    engine->use_multithreading = (count > 1);
    
    return KOLIBRI_STATUS_OK;
}

void kolibri_free_result(KolibriResult* result) {
    if (!result) return;
    if (result->data) {
        free(result->data);
        result->data = NULL;
    }
    if (result->error_message) {
        // error_message может быть строковым литералом, не освобождаем
    }
}

const char* kolibri_status_to_string(KolibriStatus status) {
    switch (status) {
        case KOLIBRI_STATUS_OK: return "OK";
        case KOLIBRI_STATUS_ERROR: return "Error";
        case KOLIBRI_STATUS_INVALID_INPUT: return "Invalid Input";
        case KOLIBRI_STATUS_OUT_OF_MEMORY: return "Out of Memory";
        case KOLIBRI_STATUS_NOT_SUPPORTED: return "Not Supported";
        default: return "Unknown Status";
    }
}
