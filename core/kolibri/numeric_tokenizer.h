/*
 * numeric_tokenizer.h
 *
 * Числовой токенизатор для Kolibri Numeric Transformer
 *
 * Назначение:
 *   - Токенизация математических выражений и числовых данных
 *   - Специальные токены для чисел, операторов, функций
 *   - Поддержка Unicode математики
 *   - Нативное представление чисел (не как текстовых токенов)
 *
 * Архитектура токенизатора:
 *   1. Базовые токены (0-255): byte-level для обычного текста
 *   2. Числовые токены (256-1279): цифры 0-9 с позиционной информацией
 *   3. Математические операторы (1280-1535): +, -, ×, ÷, =, <, >, и т.д.
 *   4. Функции и константы (1536-2047): sin, cos, π, e, √, и т.д.
 *   5. Структурные токены (2048-2559): скобки, запятые, степени
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_NUMERIC_TOKENIZER_H
#define KOLIBRI_NUMERIC_TOKENIZER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНСТАНТЫ
 * ============================================================================ */

/** Максимальная длина последовательности токенов после токенизации */
#define KNT_MAX_TOKENS 4096

/** Максимальная длина входной строки (UTF-8) */
#define KNT_MAX_INPUT 8192

/** Максимальное количество цифр в числе для специального кодирования */
#define KNT_MAX_DIGITS 32

/** Размер расширенной словарной базы (byte + numeric + math) */
#define KNT_VOCAB_EXTENDED 2560

/* ============================================================================
 * ТИПЫ ТОКЕНОВ
 * ============================================================================ */

/** Диапазоны токенов в расширенном словаре */
typedef enum {
    KNT_TOKEN_BYTE_START       = 0,      /* 0-255: byte-level токены       */
    KNT_TOKEN_BYTE_END         = 255,
    KNT_TOKEN_DIGIT_START      = 256,    /* 256-265: цифры 0-9             */
    KNT_TOKEN_DIGIT_END        = 265,
    KNT_TOKEN_NUMBER_START     = 266,    /* 266-521: числа (256 позиций)   */
    KNT_TOKEN_NUMBER_END       = 521,
    KNT_TOKEN_OPERATOR_START   = 522,    /* 522-777: операторы             */
    KNT_TOKEN_OPERATOR_END     = 777,
    KNT_TOKEN_FUNCTION_START   = 778,    /* 778-1289: функции              */
    KNT_TOKEN_FUNCTION_END     = 1289,
    KNT_TOKEN_CONSTANT_START   = 1290,   /* 1290-1545: константы           */
    KNT_TOKEN_CONSTANT_END     = 1545,
    KNT_TOKEN_STRUCTURE_START  = 1546,   /* 1546-2047: структуры           */
    KNT_TOKEN_STRUCTURE_END    = 2047,
    KNT_TOKEN_UNKNOWN          = 2559    /* Токен для неизвестных символов  */
} KolibriTokenTypeRange;

/** Конкретные математические токены */
typedef enum {
    /* Цифры */
    KNT_TOKEN_DIGIT_0 = 256,
    KNT_TOKEN_DIGIT_1 = 257,
    KNT_TOKEN_DIGIT_2 = 258,
    KNT_TOKEN_DIGIT_3 = 259,
    KNT_TOKEN_DIGIT_4 = 260,
    KNT_TOKEN_DIGIT_5 = 261,
    KNT_TOKEN_DIGIT_6 = 262,
    KNT_TOKEN_DIGIT_7 = 263,
    KNT_TOKEN_DIGIT_8 = 264,
    KNT_TOKEN_DIGIT_9 = 265,

    /* Специальные числовые маркеры */
    KNT_TOKEN_NUMBER_START_MARKER = 266,
    KNT_TOKEN_NUMBER_END_MARKER   = 267,
    KNT_TOKEN_DECIMAL_POINT       = 268,
    KNT_TOKEN_NEGATIVE_SIGN       = 269,
    KNT_TOKEN_EXPONENT_MARKER     = 270,

    /* Операторы */
    KNT_TOKEN_PLUS                = 522,  /* + */
    KNT_TOKEN_MINUS               = 523,  /* - */
    KNT_TOKEN_MULTIPLY            = 524,  /* × */
    KNT_TOKEN_DIVIDE              = 525,  /* ÷ */
    KNT_TOKEN_EQUALS              = 526,  /* = */
    KNT_TOKEN_NOT_EQUALS          = 527,  /* ≠ */
    KNT_TOKEN_LESS_THAN           = 528,  /* < */
    KNT_TOKEN_GREATER_THAN        = 529,  /* > */
    KNT_TOKEN_LESS_EQUAL          = 530,  /* ≤ */
    KNT_TOKEN_GREATER_EQUAL       = 531,  /* ≥ */
    KNT_TOKEN_POWER               = 532,  /* ^ */
    KNT_TOKEN_SQRT                = 533,  /* √ */
    KNT_TOKEN_INTEGRAL            = 534,  /* ∫ */
    KNT_TOKEN_SUMMATION           = 535,  /* ∑ */
    KNT_TOKEN_PRODUCT             = 536,  /* ∏ */
    KNT_TOKEN_LIMIT               = 537,  /* lim */
    KNT_TOKEN_DERIVATIVE          = 538,  /* d/dx */
    KNT_TOKEN_PARTIAL_DERIVATIVE  = 539,  /* ∂ */
    KNT_TOKEN_INFINITY            = 540,  /* ∞ */
    KNT_TOKEN_APPROXIMATELY       = 541,  /* ≈ */
    KNT_TOKEN_PERCENT             = 542,  /* % */
    KNT_TOKEN_FACTORIAL           = 543,  /* ! */
    KNT_TOKEN_MODULO              = 544,  /* mod */
    KNT_TOKEN_DOT_PRODUCT         = 545,  /* · */
    KNT_TOKEN_CROSS_PRODUCT       = 546,  /* × (vector) */
    KNT_TOKEN_UNION               = 547,  /* ∪ */
    KNT_TOKEN_INTERSECTION        = 548,  /* ∩ */
    KNT_TOKEN_ELEMENT_OF          = 549,  /* ∈ */
    KNT_TOKEN_NOT_ELEMENT_OF      = 550,  /* ∉ */
    KNT_TOKEN_SUBSET              = 551,  /* ⊂ */
    KNT_TOKEN_SUPERSET            = 552,  /* ⊃ */

    /* Функции */
    KNT_TOKEN_SIN                 = 778,
    KNT_TOKEN_COS                 = 779,
    KNT_TOKEN_TAN                 = 780,
    KNT_TOKEN_COT                 = 781,
    KNT_TOKEN_SEC                 = 782,
    KNT_TOKEN_CSC                 = 783,
    KNT_TOKEN_ARCSIN              = 784,
    KNT_TOKEN_ARCCOS              = 785,
    KNT_TOKEN_ARCTAN              = 786,
    KNT_TOKEN_LOG                 = 787,
    KNT_TOKEN_LN                  = 788,
    KNT_TOKEN_LOG2                = 789,
    KNT_TOKEN_LOG10               = 790,
    KNT_TOKEN_EXP_FUNC            = 791,
    KNT_TOKEN_ABS                 = 792,
    KNT_TOKEN_FLOOR               = 793,
    KNT_TOKEN_CEIL                = 794,
    KNT_TOKEN_ROUND               = 795,
    KNT_TOKEN_MIN                 = 796,
    KNT_TOKEN_MAX                 = 797,
    KNT_TOKEN_AVG                 = 798,
    KNT_TOKEN_MEDIAN              = 799,
    KNT_TOKEN_STDDEV              = 800,

    /* Константы */
    KNT_TOKEN_PI                  = 1290,  /* π */
    KNT_TOKEN_E                   = 1291,  /* e */
    KNT_TOKEN_PHI                 = 1292,  /* φ (золотое сечение) */
    KNT_TOKEN_GOLDEN_RATIO        = 1292,
    KNT_TOKEN_SQRT2               = 1293,  /* √2 */
    KNT_TOKEN_SQRT3               = 1294,  /* √3 */
    KNT_TOKEN_SQRT5               = 1295,  /* √5 */
    KNT_TOKEN_EULER_GAMMA         = 1296,  /* γ (постоянная Эйлера) */
    KNT_TOKEN_CATALAN             = 1297,  /* G (постоянная Каталана) */

    /* Структурные токены */
    KNT_TOKEN_LPAREN              = 1546,  /* ( */
    KNT_TOKEN_RPAREN              = 1547,  /* ) */
    KNT_TOKEN_LBRACKET            = 1548,  /* [ */
    KNT_TOKEN_RBRACKET            = 1549,  /* ] */
    KNT_TOKEN_LBRACE              = 1550,  /* { */
    KNT_TOKEN_RBRACE              = 1551,  /* } */
    KNT_TOKEN_COMMA               = 1552,  /* , */
    KNT_TOKEN_COLON               = 1553,  /* : */
    KNT_TOKEN_SEMICOLON           = 1554,  /* ; */
    KNT_TOKEN_FRACTION_BAR        = 1555,  /* — (для дробей) */
    KNT_TOKEN_SUBSCRIPT_START     = 1556,
    KNT_TOKEN_SUBSCRIPT_END       = 1557,
    KNT_TOKEN_SUPERSCRIPT_START   = 1558,
    KNT_TOKEN_SUPERSCRIPT_END     = 1559,
    KNT_TOKEN_MATRIX_START        = 1560,
    KNT_TOKEN_MATRIX_END          = 1561,
    KNT_TOKEN_VECTOR_START        = 1562,
    KNT_TOKEN_VECTOR_END          = 1563,
    KNT_TOKEN_EQUATION_START      = 1564,
    KNT_TOKEN_EQUATION_END        = 1565
} KolibriMathToken;

/* ============================================================================
 * СТРУКТУРЫ ДАННЫХ
 * ============================================================================ */

/** Результат токенизации одного числа */
typedef struct {
    double value;                  /* Числовое значение */
    int num_digits;                /* Количество цифр */
    int decimal_places;            /* Позиции после запятой */
    int is_negative;               /* Отрицательное? */
    int has_exponent;              /* Есть экспонента? */
    double exponent_value;         /* Значение экспоненты */
} KolibriNumberInfo;

/** Структура токена */
typedef struct {
    uint16_t token_id;             /* ID токена в словаре */
    KolibriTokenTypeRange type;    /* Тип токена */
    int position;                  /* Позиция в последовательности */

    /* Для числовых токенов */
    int is_number;                 /* Это число? */
    KolibriNumberInfo number_info; /* Информация о числе */

    /* Для текстовых токенов */
    char text[8];                  /* Оригинальный текст (для отладки) */
} KolibriToken;

/** Результат полной токенизации */
typedef struct {
    KolibriToken tokens[KNT_MAX_TOKENS];
    int token_count;
    int num_count;                 /* Количество чисел найдено */
    int operator_count;            /* Количество операторов */
    int function_count;            /* Количество функций */

    /* Статистика */
    int input_length;              /* Длина входа */
    int compression_ratio;         /* Сжатие: input_length / token_count */

    /* Ошибки */
    int error_code;                /* 0 = успех */
    char error_msg[256];           /* Описание ошибки */
} KolibriTokenizationResult;

/** Конфигурация токенизатора */
typedef struct {
    int prefer_numeric_tokens;     /* Предпочитать числовые токены (1) или byte-level (0) */
    int encode_numbers_specially;  /* Кодировать числа specially (1) или как sequence цифр (0) */
    int max_number_digits;         /* Максимальное количество цифр для специального кодирования */
    int support_unicode_math;      /* Поддерживать Unicode математические символы */
} KolibriTokenizerConfig;

/** Состояние токенизатора */
typedef struct {
    KolibriTokenizerConfig config;
    int initialized;

    /* Lookup tables для быстрого преобразования */
    uint16_t char_to_token[65536]; /* Unicode → token_id (для известных символов) */

    /* Статистика */
    long total_tokenizations;
    long total_tokens_emitted;
    long total_numbers_parsed;
} KolibriTokenizer;

/* ============================================================================
 * API: ИНИЦИАЛИЗАЦИЯ
 * ============================================================================ */

/** Инициализировать токенизатор с конфигурацией по умолчанию */
int kolibri_tokenizer_init(KolibriTokenizer *tokenizer);

/** Инициализировать токенизатор с пользовательской конфигурацией */
int kolibri_tokenizer_init_ex(KolibriTokenizer *tokenizer,
                               const KolibriTokenizerConfig *config);

/** Освободить ресурсы токенизатора */
void kolibri_tokenizer_free(KolibriTokenizer *tokenizer);

/* ============================================================================
 * API: ТОКЕНИЗАЦИЯ
 * ============================================================================ */

/**
 * Токенизировать строку UTF-8 в последовательность токенов
 *
 * @param tokenizer   Состояние токенизатора
 * @param input       Входная строка UTF-8
 * @param input_len   Длина входной строки
 * @param result      Результат токенизации (output)
 * @return 0 на успех, код ошибки иначе
 */
int kolibri_tokenize(KolibriTokenizer *tokenizer,
                     const char *input, size_t input_len,
                     KolibriTokenizationResult *result);

/**
 * Токенизировать математическое выражение
 * Специальная обработка для формул, уравнений, функций
 *
 * @param tokenizer   Состояние токенизатора
 * @param expression  Математическое выражение
 * @param result      Результат (output)
 * @return 0 на успех
 */
int kolibri_tokenize_math(KolibriTokenizer *tokenizer,
                          const char *expression,
                          KolibriTokenizationResult *result);

/**
 * Распознать и закодировать число из строки
 *
 * @param text        Текст числа (например "3.14", "-2.5e10")
 * @param text_len    Длина текста
 * @param number_info Информация о числе (output)
 * @return token_id первого токена числа
 */
uint16_t kolibri_parse_number(const char *text, size_t text_len,
                              KolibriNumberInfo *number_info);

/* ============================================================================
 * API: ДЕТОКЕНИЗАЦИЯ
 * ============================================================================ */

/**
 * Преобразовать последовательность токенов обратно в текст
 *
 * @param tokenizer   Состояние токенизатора
 * @param tokens      Массив токенов
 * @param token_count Количество токенов
 * @param output      Выходной буфер
 * @param output_size Размер выходного буфера
 * @return Длина результата
 */
size_t kolibri_detokenize(KolibriTokenizer *tokenizer,
                          const uint16_t *tokens, int token_count,
                          char *output, size_t output_size);

/* ============================================================================
 * API: УТИЛИТЫ
 * ============================================================================ */

/**
 * Получить человекочитаемое имя токена
 */
const char* kolibri_token_name(uint16_t token_id);

/**
 * Проверить является ли токен числовым
 */
int kolibri_is_digit_token(uint16_t token_id);

/**
 * Проверить является ли токен математическим оператором
 */
int kolibri_is_math_operator(uint16_t token_id);

/**
 * Проверить является ли токен функцией
 */
int kolibri_is_function(uint16_t token_id);

/**
 * Проверить является ли токен константой
 */
int kolibri_is_constant(uint16_t token_id);

/**
 * Получить числовое значение токена константы
 */
double kolibri_constant_value(uint16_t token_id);

/**
 * Распечатать результат токенизации (для отладки)
 */
void kolibri_print_tokenization(const KolibriTokenizationResult *result);

/* ============================================================================
 * API: ИНТЕГРАЦИЯ С TRANSFORMER
 * ============================================================================ */

/**
 * Создать embedding для числового токена с позиционной информацией
 *
 * Для чисел Kolibri использует специальное представление:
 *   embedding = base_number_embedding + positional_encoding(digits)
 *
 * @param token_id    ID токена
 * @param number_info Информация о числе
 * @param embedding   Выходной вектор embedding
 * @param embed_dim   Размерность embedding
 */
void kolibri_numeric_embedding(uint16_t token_id,
                               const KolibriNumberInfo *number_info,
                               float *embedding, int embed_dim);

/**
 * Рассчитать размер словаря для конфигурации transformer
 */
int kolibri_vocab_size(void);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_NUMERIC_TOKENIZER_H */
