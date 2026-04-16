/*
 * ai_resonance.c
 * 
 * Резонансное ядро рассуждений Kolibri AI.
 * Реализует концепцию "Number-Thinking" через:
 * - Генеративные формулы (generative_pool)
 * - Residual-дельты (residual_pool)
 * - Δ-оркестрацию (объединение результатов)
 * 
 * Согласно kolibri_ai_masterplan.md (F3: Resonance Core)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

/* --- Типы данных --- */

#define MAX_FORMULA_SIZE 256
#define MAX_POOL_SIZE 1024
#define MAX_RESIDUAL_SIZE 4096

/* Генеративная формула */
typedef struct {
    uint64_t id;
    uint8_t digits[MAX_FORMULA_SIZE];   /* Цифры 0-9 */
    size_t length;
    double fitness;                      /* Показатель качества */
    uint32_t generation;                 /* Поколение эволюции */
    uint64_t created_at;                 /* Timestamp создания */
} GenerativeFormula;

/* Residual-дельта (остаток после генерации) */
typedef struct {
    uint64_t formula_id;                 /* Связанная формула */
    uint8_t delta[MAX_RESIDUAL_SIZE];    /* Байты разницы */
    size_t delta_size;
    uint32_t checksum;                   /* CRC32 для проверки */
} ResidualDelta;

/* Пул генеративных формул */
typedef struct {
    GenerativeFormula formulas[MAX_POOL_SIZE];
    size_t count;
    size_t elite_count;                  /* Элитные формулы (top N) */
} GenerativePool;

/* Пул residual-дельт */
typedef struct {
    ResidualDelta deltas[MAX_POOL_SIZE];
    size_t count;
} ResidualPool;

/* Контекст резонансного ядра */
typedef struct {
    GenerativePool *generative;
    ResidualPool *residual;
    uint64_t next_formula_id;
    uint32_t current_generation;
    double total_fitness;
} ResonanceCore;

/* --- Утилиты --- */

static uint32_t crc32_simple(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

static uint64_t get_timestamp(void) {
    return (uint64_t)time(NULL);
}

/* --- Создание/уничтожение --- */

ResonanceCore* resonance_create(void) {
    ResonanceCore *core = (ResonanceCore*)calloc(1, sizeof(ResonanceCore));
    if (!core) return NULL;
    
    core->generative = (GenerativePool*)calloc(1, sizeof(GenerativePool));
    core->residual = (ResidualPool*)calloc(1, sizeof(ResidualPool));
    
    if (!core->generative || !core->residual) {
        free(core->generative);
        free(core->residual);
        free(core);
        return NULL;
    }
    
    core->next_formula_id = 1;
    core->current_generation = 0;
    core->generative->elite_count = 10;  /* Топ-10 элитных */
    
    printf("[RESONANCE] Ядро инициализировано\n");
    return core;
}

void resonance_destroy(ResonanceCore *core) {
    if (!core) return;
    free(core->generative);
    free(core->residual);
    free(core);
}

/* --- Генеративные формулы --- */

/* Добавить формулу в пул */
int resonance_add_formula(
    ResonanceCore *core,
    const uint8_t *digits,
    size_t length,
    double initial_fitness
) {
    if (!core || !digits || length == 0 || length > MAX_FORMULA_SIZE) {
        return -1;
    }
    
    if (core->generative->count >= MAX_POOL_SIZE) {
        /* Пул переполнен — удаляем худшую формулу */
        size_t worst_idx = 0;
        double worst_fitness = core->generative->formulas[0].fitness;
        
        for (size_t i = 1; i < core->generative->count; i++) {
            if (core->generative->formulas[i].fitness < worst_fitness) {
                worst_fitness = core->generative->formulas[i].fitness;
                worst_idx = i;
            }
        }
        
        /* Сдвигаем */
        memmove(&core->generative->formulas[worst_idx],
                &core->generative->formulas[worst_idx + 1],
                (core->generative->count - worst_idx - 1) * sizeof(GenerativeFormula));
        core->generative->count--;
    }
    
    GenerativeFormula *f = &core->generative->formulas[core->generative->count];
    f->id = core->next_formula_id++;
    memcpy(f->digits, digits, length);
    f->length = length;
    f->fitness = initial_fitness;
    f->generation = core->current_generation;
    f->created_at = get_timestamp();
    
    core->generative->count++;
    core->total_fitness += initial_fitness;
    
    return 0;
}

/* Мутация формулы */
static void mutate_formula(GenerativeFormula *f, uint32_t seed) {
    if (f->length == 0) return;
    
    /* Простая мутация: меняем одну цифру */
    size_t pos = seed % f->length;
    f->digits[pos] = (f->digits[pos] + 1 + (seed / f->length)) % 10;
    f->generation++;
}

/* Кроссовер двух формул */
static void crossover_formulas(
    const GenerativeFormula *a,
    const GenerativeFormula *b,
    GenerativeFormula *child,
    uint32_t seed
) {
    size_t min_len = a->length < b->length ? a->length : b->length;
    size_t cut = seed % (min_len > 0 ? min_len : 1);
    
    /* Первая часть от a, вторая от b */
    memcpy(child->digits, a->digits, cut);
    memcpy(child->digits + cut, b->digits + cut, b->length - cut);
    
    child->length = b->length;
    child->fitness = (a->fitness + b->fitness) / 2.0;
}

/* Эволюционный шаг */
int resonance_evolve(ResonanceCore *core, size_t iterations) {
    if (!core || core->generative->count < 2) {
        return -1;
    }
    
    for (size_t iter = 0; iter < iterations; iter++) {
        uint32_t seed = (uint32_t)(get_timestamp() ^ iter);
        
        /* Выбираем две случайные формулы */
        size_t idx_a = seed % core->generative->count;
        size_t idx_b = (seed / 7) % core->generative->count;
        if (idx_a == idx_b) idx_b = (idx_b + 1) % core->generative->count;
        
        GenerativeFormula *a = &core->generative->formulas[idx_a];
        GenerativeFormula *b = &core->generative->formulas[idx_b];
        
        /* Мутация с вероятностью 30% */
        if ((seed % 10) < 3) {
            mutate_formula(a, seed);
        }
        
        /* Кроссовер с вероятностью 20% */
        if ((seed % 10) < 2 && core->generative->count < MAX_POOL_SIZE) {
            GenerativeFormula child = {0};
            crossover_formulas(a, b, &child, seed);
            child.id = core->next_formula_id++;
            child.generation = core->current_generation;
            child.created_at = get_timestamp();
            
            core->generative->formulas[core->generative->count++] = child;
        }
    }
    
    core->current_generation++;
    return 0;
}

/* --- Residual-дельты --- */

/* Вычислить дельту между ожидаемым и реальным */
int resonance_compute_delta(
    ResonanceCore *core,
    uint64_t formula_id,
    const uint8_t *expected,
    size_t expected_len,
    const uint8_t *actual,
    size_t actual_len
) {
    if (!core || !expected || !actual) {
        return -1;
    }
    
    if (core->residual->count >= MAX_POOL_SIZE) {
        /* Удаляем старейшую дельту */
        memmove(&core->residual->deltas[0],
                &core->residual->deltas[1],
                (core->residual->count - 1) * sizeof(ResidualDelta));
        core->residual->count--;
    }
    
    ResidualDelta *d = &core->residual->deltas[core->residual->count];
    d->formula_id = formula_id;
    
    /* Простая XOR-дельта */
    size_t max_len = expected_len > actual_len ? expected_len : actual_len;
    if (max_len > MAX_RESIDUAL_SIZE) max_len = MAX_RESIDUAL_SIZE;
    
    for (size_t i = 0; i < max_len; i++) {
        uint8_t e = i < expected_len ? expected[i] : 0;
        uint8_t a = i < actual_len ? actual[i] : 0;
        d->delta[i] = e ^ a;
    }
    d->delta_size = max_len;
    d->checksum = crc32_simple(d->delta, d->delta_size);
    
    core->residual->count++;
    return 0;
}

/* Применить дельту для восстановления */
int resonance_apply_delta(
    const ResidualDelta *delta,
    const uint8_t *generated,
    size_t generated_len,
    uint8_t *output,
    size_t output_size
) {
    if (!delta || !generated || !output) {
        return -1;
    }
    
    size_t len = delta->delta_size < generated_len ? delta->delta_size : generated_len;
    if (len > output_size) len = output_size;
    
    for (size_t i = 0; i < len; i++) {
        output[i] = generated[i] ^ delta->delta[i];
    }
    
    return (int)len;
}

/* --- Δ-Оркестрация --- */

/* Оценить формулу по цели */
double resonance_evaluate(
    ResonanceCore *core __attribute__((unused)),
    const GenerativeFormula *formula,
    const uint8_t *target,
    size_t target_len
) {
    if (!formula || !target) return 0.0;
    
    /* Считаем совпадающие цифры */
    size_t matches = 0;
    size_t min_len = formula->length < target_len ? formula->length : target_len;
    
    for (size_t i = 0; i < min_len; i++) {
        if (formula->digits[i] == target[i]) {
            matches++;
        }
    }
    
    /* Штраф за разницу в длине */
    size_t len_diff = formula->length > target_len 
        ? formula->length - target_len 
        : target_len - formula->length;
    
    double fitness = (double)matches / (double)target_len * 100.0;
    fitness -= len_diff * 0.5;  /* Штраф */
    
    return fitness > 0 ? fitness : 0.0;
}

/* Обновить fitness всех формул по цели */
void resonance_update_fitness(
    ResonanceCore *core,
    const uint8_t *target,
    size_t target_len
) {
    if (!core || !target) return;
    
    core->total_fitness = 0.0;
    
    for (size_t i = 0; i < core->generative->count; i++) {
        GenerativeFormula *f = &core->generative->formulas[i];
        f->fitness = resonance_evaluate(core, f, target, target_len);
        core->total_fitness += f->fitness;
    }
}

/* Получить лучшую формулу */
const GenerativeFormula* resonance_get_best(ResonanceCore *core) {
    if (!core || core->generative->count == 0) return NULL;
    
    const GenerativeFormula *best = &core->generative->formulas[0];
    
    for (size_t i = 1; i < core->generative->count; i++) {
        if (core->generative->formulas[i].fitness > best->fitness) {
            best = &core->generative->formulas[i];
        }
    }
    
    return best;
}

/* --- Статистика --- */

void resonance_print_stats(const ResonanceCore *core) {
    if (!core) return;
    
    printf("\n=== Resonance Core Stats ===\n");
    printf("Формул в пуле:    %zu\n", core->generative->count);
    printf("Residual-дельт:   %zu\n", core->residual->count);
    printf("Текущее поколение: %u\n", core->current_generation);
    printf("Суммарный fitness: %.2f\n", core->total_fitness);
    
    const GenerativeFormula *best = resonance_get_best((ResonanceCore*)core);
    if (best) {
        printf("Лучшая формула:   ID=%lu, fitness=%.2f, gen=%u\n",
               (unsigned long)best->id, best->fitness, best->generation);
    }
    printf("============================\n\n");
}
