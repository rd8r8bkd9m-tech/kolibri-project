#ifndef KOLIBRI_FORMULA_H
#define KOLIBRI_FORMULA_H

#include "kolibri/random.h"
#include "kolibri/symbol_table.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t digits[4000];
    size_t length;
} KolibriGene;

#define KOLIBRI_ASSOC_QUESTION_MAX 256
#define KOLIBRI_ASSOC_ANSWER_MAX 512
#define KOLIBRI_ASSOC_DIGITS_MAX (KOLIBRI_ASSOC_ANSWER_MAX * KOLIBRI_SYMBOL_DIGITS)

typedef struct {
    int input_hash;
    int output_hash;
    char question[KOLIBRI_ASSOC_QUESTION_MAX];
    char answer[KOLIBRI_ASSOC_ANSWER_MAX];
    uint8_t question_digits[KOLIBRI_ASSOC_DIGITS_MAX];
    size_t question_digits_length;
    uint8_t answer_digits[KOLIBRI_ASSOC_DIGITS_MAX];
    size_t answer_digits_length;
    uint64_t timestamp;
    char source[64];
} KolibriAssociation;

#define KOLIBRI_FORMULA_MAX_ASSOCIATIONS 1000
#define KOLIBRI_POOL_MAX_ASSOCIATIONS 100000

/* Домены знаний — формулы специализируются по областям */
#define KOLIBRI_DOMAIN_NAME_MAX 64

typedef enum {
    KOLIBRI_DOMAIN_GENERAL = 0,    /* Общие знания */
    KOLIBRI_DOMAIN_MEDICINE,       /* Медицина */
    KOLIBRI_DOMAIN_IT,             /* Информационные технологии */
    KOLIBRI_DOMAIN_PHYSICS,        /* Физика */
    KOLIBRI_DOMAIN_MATH,           /* Математика */
    KOLIBRI_DOMAIN_CHEMISTRY,      /* Химия */
    KOLIBRI_DOMAIN_BIOLOGY,        /* Биология */
    KOLIBRI_DOMAIN_HISTORY,        /* История */
    KOLIBRI_DOMAIN_LAW,            /* Юриспруденция */
    KOLIBRI_DOMAIN_ECONOMICS,      /* Экономика */
    KOLIBRI_DOMAIN_CUSTOM = 255    /* Пользовательский домен */
} KolibriDomainType;

/* ============================================================================
 * Конфигурация эволюционного реактора
 * ============================================================================ */

typedef enum {
    KOLIBRI_MUTATION_POINT = 0,    /* Точечная мутация одной цифры */
    KOLIBRI_MUTATION_SWAP,         /* Обмен двух цифр местами */
    KOLIBRI_MUTATION_INVERT,       /* Инверсия сегмента */
    KOLIBRI_MUTATION_SCRAMBLE,     /* Перемешивание сегмента */
    KOLIBRI_MUTATION_SHIFT,        /* Сдвиг всех цифр */
    KOLIBRI_MUTATION_COUNT
} KolibriMutationType;

typedef enum {
    KOLIBRI_CROSSOVER_SINGLE_POINT = 0,  /* Одноточечный кроссовер */
    KOLIBRI_CROSSOVER_TWO_POINT,         /* Двухточечный кроссовер */
    KOLIBRI_CROSSOVER_UNIFORM,           /* Равномерный кроссовер */
    KOLIBRI_CROSSOVER_COUNT
} KolibriCrossoverType;

typedef struct {
    /* Параметры мутации */
    double mutation_rate;              /* Вероятность мутации [0.0-1.0] */
    double mutation_strength;          /* Сила мутации (кол-во изменений) */
    KolibriMutationType mutation_type; /* Тип мутации */
    
    /* Параметры кроссовера */
    double crossover_rate;             /* Вероятность кроссовера */
    KolibriCrossoverType crossover_type;
    
    /* Параметры селекции */
    double elite_ratio;                /* Доля элиты [0.1-0.5] */
    double tournament_size;            /* Размер турнира (как доля) */
    
    /* Параметры цикла */
    uint64_t generations_per_tick;     /* Поколений за один tick */
    int adaptive_mutation;             /* Адаптивная мутация? */
} KolibriEvolutionConfig;

/* Метрики эволюции */
typedef struct {
    uint64_t total_generations;        /* Всего поколений */
    uint64_t total_mutations;          /* Всего мутаций */
    uint64_t beneficial_mutations;     /* Полезных мутаций */
    uint64_t neutral_mutations;        /* Нейтральных мутаций */
    uint64_t harmful_mutations;        /* Вредных мутаций */
    double evolution_speed;            /* Скорость роста fitness */
    double mutation_energy;            /* Средняя "энергия" мутаций */
    double best_fitness;               /* Лучший fitness */
    double avg_fitness;                /* Средний fitness */
    double fitness_variance;           /* Дисперсия fitness */
    uint64_t stagnation_count;         /* Поколений без улучшения */
} KolibriEvolutionMetrics;

typedef struct {
    KolibriGene gene;
    double fitness;
    double feedback;
    KolibriAssociation associations[KOLIBRI_FORMULA_MAX_ASSOCIATIONS];
    size_t association_count;
    /* Доменная специализация */
    KolibriDomainType domain;
    char domain_name[KOLIBRI_DOMAIN_NAME_MAX];
} KolibriFormula;

/* Начальные размеры пула в браузере должны быть скромнее: одна формула уже занимает ~4 МБ. */
#if defined(__EMSCRIPTEN__)
#define KOLIBRI_FORMULA_INITIAL_CAPACITY 1
#define KOLIBRI_POOL_EXAMPLES_INITIAL_CAPACITY 128
#define KOLIBRI_POOL_ASSOC_INITIAL_CAPACITY 128
#else
#define KOLIBRI_FORMULA_INITIAL_CAPACITY 16
#define KOLIBRI_POOL_EXAMPLES_INITIAL_CAPACITY 1000
#define KOLIBRI_POOL_ASSOC_INITIAL_CAPACITY 1000
#endif

typedef struct {
    KolibriFormula *formulas;   /* Динамический массив формул (безлимитный) */
    size_t count;
    size_t capacity;            /* Текущая ёмкость (realloc при нехватке) */
    KolibriRng rng;
    int *inputs;
    int *targets;
    size_t examples;
    size_t examples_capacity;
    KolibriAssociation *associations;
    size_t association_count;
    size_t association_capacity;
    
    /* Эволюционный реактор */
    KolibriEvolutionConfig config;
    KolibriEvolutionMetrics metrics;
    double prev_best_fitness;
} KolibriFormulaPool;

/* --- Основные функции --- */
void kf_pool_init(KolibriFormulaPool *pool, uint64_t seed);
void kf_pool_free(KolibriFormulaPool *pool);
void kf_pool_clear_examples(KolibriFormulaPool *pool);
int kf_pool_add_example(KolibriFormulaPool *pool, int input, int target);
int kf_pool_ensure_association_capacity(KolibriFormulaPool *pool, size_t count);
int kf_pool_add_association(KolibriFormulaPool *pool,
                            KolibriSymbolTable *symbols,
                            const char *question,
                            const char *answer,
                            const char *source,
                            uint64_t timestamp);
void kf_pool_tick(KolibriFormulaPool *pool, size_t generations);
const KolibriFormula *kf_pool_best(const KolibriFormulaPool *pool);
int kf_formula_apply(const KolibriFormula *formula, int input, int *output);
size_t kf_formula_digits(const KolibriFormula *formula, uint8_t *out, size_t out_len);
int kf_formula_describe(const KolibriFormula *formula, char *buffer, size_t buffer_len);
int kf_pool_feedback(KolibriFormulaPool *pool, const KolibriGene *gene, double delta);
int kf_formula_lookup_answer(const KolibriFormula *formula, int input,
                             char *buffer, size_t buffer_len);
int kf_hash_from_text(const char *text);

/* --- Динамическое управление формулами --- */

/** Расширить пул: добавить новые слоты формул */
int kf_pool_grow(KolibriFormulaPool *pool, size_t new_capacity);

/** Добавить доменную формулу в пул. Пул растёт автоматически. */
int kf_pool_add_domain_formula(KolibriFormulaPool *pool,
                               KolibriDomainType domain,
                               const char *domain_name);

/** Найти лучшую формулу для домена */
const KolibriFormula *kf_pool_best_for_domain(const KolibriFormulaPool *pool,
                                              KolibriDomainType domain);

/** Получить количество формул для домена */
size_t kf_pool_domain_count(const KolibriFormulaPool *pool, KolibriDomainType domain);

/* --- Функции эволюционного реактора --- */

/** Инициализация конфигурации по умолчанию */
void kf_config_default(KolibriEvolutionConfig *config);

/** Установка конфигурации реактора */
int kf_pool_set_config(KolibriFormulaPool *pool, const KolibriEvolutionConfig *config);

/** Получение текущей конфигурации */
int kf_pool_get_config(const KolibriFormulaPool *pool, KolibriEvolutionConfig *config);

/** Получение метрик эволюции */
int kf_pool_get_metrics(const KolibriFormulaPool *pool, KolibriEvolutionMetrics *metrics);

/** Сброс метрик */
void kf_pool_reset_metrics(KolibriFormulaPool *pool);

/** Автономный эволюционный цикл (запуск реактора на N поколений) */
int kf_reactor_run(KolibriFormulaPool *pool, size_t max_generations,
                   double target_fitness);

/** Экспорт метрик в цифровом формате (для логирования в геном) */
int kf_metrics_to_digits(const KolibriEvolutionMetrics *metrics,
                         char *buffer, size_t buffer_len);

/** Адаптивная настройка параметров на основе метрик */
void kf_config_adapt(KolibriFormulaPool *pool);

#endif /* KOLIBRI_FORMULA_H */
