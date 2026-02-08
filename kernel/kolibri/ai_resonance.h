/*
 * ai_resonance.h
 * 
 * Публичный API резонансного ядра рассуждений Kolibri AI.
 * Реализует Δ-оркестрацию: генеративные формулы + residual-дельты.
 */

#ifndef KOLIBRI_AI_RESONANCE_H
#define KOLIBRI_AI_RESONANCE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Константы --- */
#define RESONANCE_MAX_FORMULA_SIZE 256
#define RESONANCE_MAX_POOL_SIZE 1024
#define RESONANCE_MAX_RESIDUAL_SIZE 4096

/* --- Опаковые типы --- */
typedef struct ResonanceCore ResonanceCore;

/* Генеративная формула (публичная часть) */
typedef struct {
    uint64_t id;
    const uint8_t *digits;
    size_t length;
    double fitness;
    uint32_t generation;
    uint64_t created_at;
} ResonanceFormula;

/* --- Создание/уничтожение --- */

/**
 * Создаёт резонансное ядро.
 * @return Указатель на ядро или NULL при ошибке.
 */
ResonanceCore* resonance_create(void);

/**
 * Уничтожает резонансное ядро.
 * @param core Указатель на ядро.
 */
void resonance_destroy(ResonanceCore *core);

/* --- Управление формулами --- */

/**
 * Добавляет формулу в генеративный пул.
 * @param core Указатель на ядро.
 * @param digits Цифры формулы (0-9).
 * @param length Длина формулы.
 * @param initial_fitness Начальный fitness.
 * @return 0 при успехе, -1 при ошибке.
 */
int resonance_add_formula(
    ResonanceCore *core,
    const uint8_t *digits,
    size_t length,
    double initial_fitness
);

/**
 * Выполняет эволюционный шаг (мутации, кроссовер).
 * @param core Указатель на ядро.
 * @param iterations Количество итераций.
 * @return 0 при успехе, -1 при ошибке.
 */
int resonance_evolve(ResonanceCore *core, size_t iterations);

/**
 * Обновляет fitness всех формул по целевым данным.
 * @param core Указатель на ядро.
 * @param target Целевые данные для сравнения.
 * @param target_len Длина целевых данных.
 */
void resonance_update_fitness(
    ResonanceCore *core,
    const uint8_t *target,
    size_t target_len
);

/**
 * Получает лучшую формулу по fitness.
 * @param core Указатель на ядро.
 * @return Указатель на лучшую формулу или NULL.
 */
const void* resonance_get_best(ResonanceCore *core);

/* --- Residual-дельты --- */

/**
 * Вычисляет дельту между ожидаемым и реальным результатом.
 * @param core Указатель на ядро.
 * @param formula_id ID формулы.
 * @param expected Ожидаемые данные.
 * @param expected_len Длина ожидаемых данных.
 * @param actual Реальные данные.
 * @param actual_len Длина реальных данных.
 * @return 0 при успехе, -1 при ошибке.
 */
int resonance_compute_delta(
    ResonanceCore *core,
    uint64_t formula_id,
    const uint8_t *expected,
    size_t expected_len,
    const uint8_t *actual,
    size_t actual_len
);

/* --- Диагностика --- */

/**
 * Выводит статистику ядра.
 * @param core Указатель на ядро.
 */
void resonance_print_stats(const ResonanceCore *core);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_AI_RESONANCE_H */
