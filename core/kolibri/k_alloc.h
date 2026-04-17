#ifndef KOLIBRI_K_ALLOC_H
#define KOLIBRI_K_ALLOC_H

#include <stddef.h>
#include <stdint.h>
#include "decimal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* === Phase 1.1: Triplet-Aware Memory Allocator === */

/** Основная структура блока памяти с поддержкой сжатия */
typedef struct k_mem_block {
    size_t original_size;
    size_t packed_size;
    int has_triplets;
    uint8_t *data;
} k_mem_block;

/** Выделяет память и опционально переводит текст в цифровой поток с упаковкой триплетов */
void* k_malloc(size_t size);
void k_free(void* ptr);

/** Создает блок памяти, сразу конвертируя текст в Decimal Cognition и сжимая триплеты */
k_mem_block* k_alloc_decimal_block(const char *text);
void k_free_decimal_block(k_mem_block *block);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_K_ALLOC_H */
