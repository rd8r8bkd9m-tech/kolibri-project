/*
 * formula_logic.h
 * 
 * МЕТА-ФОРМУЛЫ: Формулы которые описывают логику других формул
 * 
 * Концепция: Вместо хранения логических выражений напрямую,
 * храним ФОРМУЛУ которая генерирует эти выражения!
 * 
 * Это как:
 * - Программа которая пишет программы
 * - Формула которая создаёт формулы
 * - ДНК которая кодирует ДНК
 */

#ifndef KOLIBRI_FORMULA_LOGIC_H
#define KOLIBRI_FORMULA_LOGIC_H

#include "kolibri/logical_memory.h"
#include "kolibri/formula.h"
#include <stddef.h>
#include <stdint.h>

/* ========== МЕТА-ФОРМУЛА ========== */

/* Тип мета-операции */
typedef enum {
    META_GENERATE_CONSTANT,   /* Генерирует constant() выражения */
    META_GENERATE_REPEAT,     /* Генерирует repeat() выражения */
    META_GENERATE_SEQUENCE,   /* Генерирует sequence() выражения */
    META_GENERATE_COMPOSE,    /* Генерирует композицию */
    META_TRANSFORM_LOGIC,     /* Трансформирует существующую логику */
    META_DERIVE_RELATION,     /* Выводит новые отношения */
    META_EVOLVE_PATTERN,      /* Эволюционирует паттерн */
    META_COMPRESS_LOGIC       /* Сжимает логику в более компактную форму */
} MetaOperation;

/* Мета-формула: формула для генерации логики */
typedef struct {
    MetaOperation operation;
    
    /* Параметры генерации */
    union {
        /* META_GENERATE_CONSTANT */
        struct {
            char* value;
        } generate_constant;

        /* META_GENERATE_REPEAT */
        struct {
            char pattern_formula[64];   /* Формула для паттерна */
            char count_formula[64];     /* Формула для count */
        } gen_repeat;
        
        /* META_GENERATE_SEQUENCE */
        struct {
            char start_formula[64];
            char step_formula[64];
            char count_formula[64];
        } gen_sequence;
        
        /* META_TRANSFORM_LOGIC */
        struct {
            char input_logic_id[64];    /* ID исходной логики */
            char transform_rule[128];   /* Правило трансформации */
        } transform;
        
        /* META_DERIVE_RELATION */
        struct {
            char left_logic_id[64];
            char right_logic_id[64];
            char inference_rule[128];   /* A→B, B→C ⇒ A→C */
        } derive;
        
        /* META_EVOLVE_PATTERN */
        struct {
            char source_pattern_id[64];
            double mutation_rate;
            int generations;
        } evolve;
        
        /* META_COMPRESS_LOGIC */
        struct {
            char target_logic_id[64];
            char compression_strategy[64];
        } compress;
    } params;
    
    /* Метаданные */
    uint64_t generation;        /* Поколение мета-формулы */
    double complexity_score;    /* Сложность генерируемой логики */
    size_t output_size_estimate; /* Ожидаемый размер выхода */
} MetaFormula;

/* Хранилище мета-формул */
typedef struct {
    MetaFormula formulas[256];
    size_t count;
    
    /* Кэш сгенерированной логики */
    LogicExpression *generated_cache[256];
    char cache_ids[256][64];
    size_t cache_count;
} MetaFormulaStore;

/* ========== API ========== */

/* Создать хранилище мета-формул */
MetaFormulaStore* mf_create_store(void);

/* Уничтожить хранилище */
void mf_destroy_store(MetaFormulaStore *store);

/* Создать мета-формулу для генерации repeat() */
MetaFormula* mf_create_repeat_generator(
    const char *pattern_formula, 
    const char *count_formula
);

/* Создать мета-формулу для генерации sequence() */
MetaFormula* mf_create_sequence_generator(
    const char *start_formula,
    const char *step_formula,
    const char *count_formula
);

/* Создать мета-формулу для трансформации логики */
MetaFormula* mf_create_transformer(
    const char *input_logic_id,
    const char *transform_rule
);

/* Создать мета-формулу для вывода отношений */
MetaFormula* mf_create_relation_deriver(
    const char *left_id,
    const char *right_id,
    const char *inference_rule
);

/* Создать пустую мета-формулу */
MetaFormula* mf_create_meta_formula(void);

/* ГЛАВНАЯ ФУНКЦИЯ: Выполнить мета-формулу → получить логику */
LogicExpression* mf_execute(
    MetaFormulaStore *store,
    const MetaFormula *meta,
    LogicalMemory *target_memory
);

/* Сохранить мета-формулу */
int mf_store_meta(MetaFormulaStore *store, const MetaFormula *meta, const char *id);

/* Загрузить мета-формулу по ID */
MetaFormula* mf_load_meta(MetaFormulaStore *store, const char *id);

/* Оптимизировать мета-формулу */
MetaFormula* mf_optimize_meta(const MetaFormula *meta);

/* Эволюционировать мета-формулу */
MetaFormula* mf_evolve_meta(const MetaFormula *meta, double mutation_rate);

/* Композиция мета-формул */
MetaFormula* mf_compose_meta(const MetaFormula *meta1, const MetaFormula *meta2);

/* Статистика */
typedef struct {
    size_t total_meta_formulas;
    size_t generated_logic_count;
    size_t meta_size_bytes;
    size_t logic_size_bytes;
    double meta_to_logic_ratio;
} MetaFormulaStats;

int mf_get_stats(MetaFormulaStore *store, MetaFormulaStats *stats);

/* Вывести мета-формулу как текст */
int mf_to_string(const MetaFormula *meta, char *output, size_t output_size);

/* ========== ПРОДВИНУТЫЕ ОПЕРАЦИИ ========== */

/* Автоматическое обнаружение паттернов и создание мета-формул */
int mf_auto_discover_patterns(
    LogicalMemory *memory,
    MetaFormulaStore *store
);

/* Применить мета-формулу к множеству логических ячеек */
int mf_batch_execute(
    MetaFormulaStore *store,
    const MetaFormula *meta,
    LogicalMemory *memory,
    const char **cell_ids,
    size_t cell_count
);

/* Инференс: вывести новую мета-формулу из существующих */
MetaFormula* mf_infer_meta(
    MetaFormulaStore *store,
    const char *rule,
    const MetaFormula **input_metas,
    size_t input_count
);

/* Уничтожить мета-формулу */
void mf_destroy_meta_formula(MetaFormula *meta);

#endif /* KOLIBRI_FORMULA_LOGIC_H */
