/*
 * numeric_tokenizer.c
 *
 * Реализация числового токенизатора для Kolibri Numeric Transformer
 *
 * Поддерживает:
 *   - Базовые byte-level токены (0-255)
 *   - Цифровые токены (256-265) для 0-9
 *   - Математические операторы (522-552)
 *   - Функции (778-800)
 *   - Константы (1290-1297): π, e, φ, и т.д.
 *   - Структурные токены (1546-1565)
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/numeric_tokenizer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* ============================================================================
 * ВНУТРЕННИЕ ФУНКЦИИ
 * ============================================================================ */

/** Проверка является ли символ цифрой */
static int is_digit_char(char c) {
    return c >= '0' && c <= '9';
}

/** Проверка является ли символ пробелом */
static int is_space_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/** Распознать число из строки */
uint16_t kolibri_parse_number(const char *text, size_t text_len,
                              KolibriNumberInfo *number_info) {
    if (!text || text_len == 0 || !number_info) {
        return KNT_TOKEN_UNKNOWN;
    }

    memset(number_info, 0, sizeof(KolibriNumberInfo));

    /* Буфер для парсинга */
    char buf[64];
    if (text_len >= sizeof(buf)) text_len = sizeof(buf) - 1;
    memcpy(buf, text, text_len);
    buf[text_len] = '\0';

    /* Парсим число */
    char *endptr = NULL;
    number_info->value = strtod(buf, &endptr);

    if (endptr == buf) {
        /* Не число */
        return KNT_TOKEN_UNKNOWN;
    }

    /* Анализируем структуру числа */
    number_info->is_negative = (number_info->value < 0);
    if (number_info->is_negative) {
        number_info->value = -number_info->value;
    }

    /* Считаем цифры */
    number_info->num_digits = 0;
    number_info->decimal_places = 0;
    int after_decimal = 0;

    for (size_t i = 0; i < text_len; i++) {
        char c = buf[i];
        if (is_digit_char(c)) {
            if (after_decimal) {
                number_info->decimal_places++;
            }
            number_info->num_digits++;
        } else if (c == '.' || c == ',') {
            after_decimal = 1;
        } else if (c == 'e' || c == 'E') {
            number_info->has_exponent = 1;
            number_info->exponent_value = strtod(&buf[i+1], NULL);
            break;
        }
    }

    return KNT_TOKEN_NUMBER_START_MARKER;
}

/* ============================================================================
 * ИНИЦИАЛИЗАЦИЯ
 * ============================================================================ */

int kolibri_tokenizer_init(KolibriTokenizer *tokenizer) {
    if (!tokenizer) return -1;

    KolibriTokenizerConfig config = {
        .prefer_numeric_tokens = 1,
        .encode_numbers_specially = 1,
        .max_number_digits = 16,
        .support_unicode_math = 1
    };

    return kolibri_tokenizer_init_ex(tokenizer, &config);
}

int kolibri_tokenizer_init_ex(KolibriTokenizer *tokenizer,
                               const KolibriTokenizerConfig *config) {
    if (!tokenizer || !config) return -1;

    memset(tokenizer, 0, sizeof(KolibriTokenizer));
    tokenizer->config = *config;
    tokenizer->initialized = 1;

    /* Инициализируем lookup table */
    memset(tokenizer->char_to_token, 0, sizeof(tokenizer->char_to_token));

    /* Цифры ASCII */
    for (int i = 0; i <= 9; i++) {
        tokenizer->char_to_token['0' + i] = (uint16_t)(KNT_TOKEN_DIGIT_0 + i);
    }

    /* Математические операторы ASCII */
    tokenizer->char_to_token['+'] = KNT_TOKEN_PLUS;
    tokenizer->char_to_token['-'] = KNT_TOKEN_MINUS;
    tokenizer->char_to_token['*'] = KNT_TOKEN_MULTIPLY;
    tokenizer->char_to_token['/'] = KNT_TOKEN_DIVIDE;
    tokenizer->char_to_token['='] = KNT_TOKEN_EQUALS;
    tokenizer->char_to_token['<'] = KNT_TOKEN_LESS_THAN;
    tokenizer->char_to_token['>'] = KNT_TOKEN_GREATER_THAN;
    tokenizer->char_to_token['!'] = KNT_TOKEN_FACTORIAL;
    tokenizer->char_to_token['%'] = KNT_TOKEN_PERCENT;
    tokenizer->char_to_token['^'] = KNT_TOKEN_POWER;

    /* Структурные символы */
    tokenizer->char_to_token['('] = KNT_TOKEN_LPAREN;
    tokenizer->char_to_token[')'] = KNT_TOKEN_RPAREN;
    tokenizer->char_to_token['['] = KNT_TOKEN_LBRACKET;
    tokenizer->char_to_token[']'] = KNT_TOKEN_RBRACKET;
    tokenizer->char_to_token['{'] = KNT_TOKEN_LBRACE;
    tokenizer->char_to_token['}'] = KNT_TOKEN_RBRACE;
    tokenizer->char_to_token[','] = KNT_TOKEN_COMMA;
    tokenizer->char_to_token[':'] = KNT_TOKEN_COLON;
    tokenizer->char_to_token[';'] = KNT_TOKEN_SEMICOLON;
    tokenizer->char_to_token['.'] = KNT_TOKEN_DECIMAL_POINT;

    /* Unicode математические символы */
    if (config->support_unicode_math) {
        tokenizer->char_to_token[0x00D7] = KNT_TOKEN_MULTIPLY;   /* × */
        tokenizer->char_to_token[0x00F7] = KNT_TOKEN_DIVIDE;     /* ÷ */
        tokenizer->char_to_token[0x221A] = KNT_TOKEN_SQRT;       /* √ */
        tokenizer->char_to_token[0x221E] = KNT_TOKEN_INFINITY;   /* ∞ */
        tokenizer->char_to_token[0x222B] = KNT_TOKEN_INTEGRAL;   /* ∫ */
        tokenizer->char_to_token[0x2211] = KNT_TOKEN_SUMMATION;  /* ∑ */
        tokenizer->char_to_token[0x220F] = KNT_TOKEN_PRODUCT;    /* ∏ */
        tokenizer->char_to_token[0x03C0] = KNT_TOKEN_PI;         /* π */
        tokenizer->char_to_token[0x2260] = KNT_TOKEN_NOT_EQUALS; /* ≠ */
        tokenizer->char_to_token[0x2264] = KNT_TOKEN_LESS_EQUAL; /* ≤ */
        tokenizer->char_to_token[0x2265] = KNT_TOKEN_GREATER_EQUAL; /* ≥ */
        tokenizer->char_to_token[0x2248] = KNT_TOKEN_APPROXIMATELY; /* ≈ */
        tokenizer->char_to_token[0x2208] = KNT_TOKEN_ELEMENT_OF; /* ∈ */
        tokenizer->char_to_token[0x2209] = KNT_TOKEN_NOT_ELEMENT_OF; /* ∉ */
        tokenizer->char_to_token[0x2282] = KNT_TOKEN_SUBSET;     /* ⊂ */
        tokenizer->char_to_token[0x2283] = KNT_TOKEN_SUPERSET;   /* ⊃ */
        tokenizer->char_to_token[0x222A] = KNT_TOKEN_UNION;      /* ∪ */
        tokenizer->char_to_token[0x2229] = KNT_TOKEN_INTERSECTION; /* ∩ */
        tokenizer->char_to_token[0x2219] = KNT_TOKEN_DOT_PRODUCT; /* · */
        tokenizer->char_to_token[0x03C6] = KNT_TOKEN_PHI;        /* φ */
        tokenizer->char_to_token[0x2202] = KNT_TOKEN_PARTIAL_DERIVATIVE; /* ∂ */
    }

    return 0;
}

void kolibri_tokenizer_free(KolibriTokenizer *tokenizer) {
    if (!tokenizer) return;
    tokenizer->initialized = 0;
    memset(tokenizer, 0, sizeof(KolibriTokenizer));
}

/* ============================================================================
 * ТОКЕНИЗАЦИЯ
 * ============================================================================ */

/** Распознать математическую функцию из текста */
static uint16_t recognize_function(const char *text, size_t len) {
    if (len >= 3 && strncmp(text, "sin", 3) == 0) return KNT_TOKEN_SIN;
    if (len >= 3 && strncmp(text, "cos", 3) == 0) return KNT_TOKEN_COS;
    if (len >= 3 && strncmp(text, "tan", 3) == 0) return KNT_TOKEN_TAN;
    if (len >= 3 && strncmp(text, "cot", 3) == 0) return KNT_TOKEN_COT;
    if (len >= 3 && strncmp(text, "sec", 3) == 0) return KNT_TOKEN_SEC;
    if (len >= 3 && strncmp(text, "csc", 3) == 0) return KNT_TOKEN_CSC;
    if (len >= 4 && strncmp(text, "arcsin", 6) == 0) return KNT_TOKEN_ARCSIN;
    if (len >= 4 && strncmp(text, "arccos", 6) == 0) return KNT_TOKEN_ARCCOS;
    if (len >= 4 && strncmp(text, "arctan", 6) == 0) return KNT_TOKEN_ARCTAN;
    if (len >= 3 && strncmp(text, "log", 3) == 0) return KNT_TOKEN_LOG;
    if (len >= 2 && strncmp(text, "ln", 2) == 0) return KNT_TOKEN_LN;
    if (len >= 3 && strncmp(text, "exp", 3) == 0) return KNT_TOKEN_EXP_FUNC;
    if (len >= 3 && strncmp(text, "abs", 3) == 0) return KNT_TOKEN_ABS;
    if (len >= 4 && strncmp(text, "floor", 5) == 0) return KNT_TOKEN_FLOOR;
    if (len >= 4 && strncmp(text, "ceil", 4) == 0) return KNT_TOKEN_CEIL;
    if (len >= 5 && strncmp(text, "round", 5) == 0) return KNT_TOKEN_ROUND;
    if (len >= 3 && strncmp(text, "min", 3) == 0) return KNT_TOKEN_MIN;
    if (len >= 3 && strncmp(text, "max", 3) == 0) return KNT_TOKEN_MAX;
    if (len >= 3 && strncmp(text, "avg", 3) == 0) return KNT_TOKEN_AVG;
    if (len >= 4 && strncmp(text, "lim", 3) == 0) return KNT_TOKEN_LIMIT;

    return 0;
}

int kolibri_tokenize(KolibriTokenizer *tokenizer,
                     const char *input, size_t input_len,
                     KolibriTokenizationResult *result) {
    if (!tokenizer || !tokenizer->initialized || !input || !result) {
        return -1;
    }

    memset(result, 0, sizeof(KolibriTokenizationResult));
    result->input_length = (int)input_len;

    if (input_len == 0) {
        return 0;
    }

    if (input_len >= KNT_MAX_INPUT) {
        result->error_code = -2;
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Input too long: %zu > %d", input_len, KNT_MAX_INPUT);
        return -2;
    }

    int pos = 0;
    int token_idx = 0;

    while (pos < (int)input_len && token_idx < KNT_MAX_TOKENS) {
        char c = input[pos];

        /* Пропускаем пробелы */
        if (is_space_char(c)) {
            pos++;
            continue;
        }

        /* Проверяем Unicode (простая проверка для 2-byte UTF-8) */
        if ((unsigned char)c >= 0xC0 && pos + 1 < (int)input_len) {
            uint16_t unicode = ((uint16_t)(c & 0x1F) << 6) |
                              ((uint16_t)(input[pos+1] & 0x3F));

            /* Проверяем lookup table */
            uint16_t token_id = tokenizer->char_to_token[unicode];
            if (token_id != 0) {
                KolibriToken *token = &result->tokens[token_idx];
                token->token_id = token_id;
                token->position = token_idx;
                token->type = KNT_TOKEN_OPERATOR_START;

                /* Копируем UTF-8 байты */
                token->text[0] = c;
                token->text[1] = input[pos+1];
                token->text[2] = '\0';

                /* Определяем тип */
                if (token_id == KNT_TOKEN_PI || token_id == KNT_TOKEN_PHI ||
                    token_id == KNT_TOKEN_INFINITY) {
                    token->type = KNT_TOKEN_CONSTANT_START;
                    result->num_count++;
                }

                token_idx++;
                pos += 2;
                continue;
            }
        }

        /* Проверяем числа */
        if (is_digit_char(c) || (c == '-' && pos + 1 < (int)input_len &&
                                 is_digit_char(input[pos+1]))) {
            /* Нашли число */
            int num_start = pos;
            if (c == '-') pos++;  /* Пропускаем минус для отрицательных */

            /* Считаем цифры */
            while (pos < (int)input_len && is_digit_char(input[pos])) {
                pos++;
            }

            /* Проверяем десятичную точку */
            if (pos < (int)input_len && (input[pos] == '.' || input[pos] == ',')) {
                pos++;
                while (pos < (int)input_len && is_digit_char(input[pos])) {
                    pos++;
                }
            }

            /* Проверяем экспоненту */
            if (pos < (int)input_len && (input[pos] == 'e' || input[pos] == 'E')) {
                pos++;
                if (pos < (int)input_len && (input[pos] == '+' || input[pos] == '-')) {
                    pos++;
                }
                while (pos < (int)input_len && is_digit_char(input[pos])) {
                    pos++;
                }
            }

            /* Создаём числовой токен */
            KolibriToken *token = &result->tokens[token_idx];
            KolibriNumberInfo num_info;

            token->token_id = kolibri_parse_number(&input[num_start],
                                                   (size_t)(pos - num_start),
                                                   &num_info);
            token->is_number = 1;
            token->number_info = num_info;
            token->position = token_idx;
            token->type = KNT_TOKEN_NUMBER_START;

            /* Копируем текст числа */
            int text_len = pos - num_start;
            if (text_len >= 8) text_len = 7;
            memcpy(token->text, &input[num_start], text_len);
            token->text[text_len] = '\0';

            token_idx++;
            result->num_count++;
            tokenizer->total_numbers_parsed++;
            continue;
        }

        /* Проверяем математические функции */
        if (isalpha(c)) {
            int func_start = pos;
            while (pos < (int)input_len && isalpha(input[pos])) {
                pos++;
            }

            uint16_t func_token = recognize_function(&input[func_start],
                                                     (size_t)(pos - func_start));
            if (func_token != 0) {
                KolibriToken *token = &result->tokens[token_idx];
                token->token_id = func_token;
                token->position = token_idx;
                token->type = KNT_TOKEN_FUNCTION_START;

                int text_len = pos - func_start;
                if (text_len >= 8) text_len = 7;
                memcpy(token->text, &input[func_start], text_len);
                token->text[text_len] = '\0';

                token_idx++;
                result->function_count++;
                continue;
            } else {
                /* Не функция, кодируем как byte-level */
                pos = func_start;  /* Откатываемся */
            }
        }

        /* Байт-уровень токен (по умолчанию) */
        KolibriToken *token = &result->tokens[token_idx];
        uint16_t token_id = tokenizer->char_to_token[(unsigned char)c];

        if (token_id != 0 && tokenizer->config.prefer_numeric_tokens) {
            token->token_id = token_id;
        } else {
            token->token_id = (uint16_t)(unsigned char)c;
        }

        token->position = token_idx;
        token->text[0] = c;
        token->text[1] = '\0';

        /* Определяем тип */
        if (token->token_id >= KNT_TOKEN_OPERATOR_START &&
            token->token_id <= KNT_TOKEN_OPERATOR_END) {
            token->type = KNT_TOKEN_OPERATOR_START;
            result->operator_count++;
        } else if (token->token_id >= KNT_TOKEN_STRUCTURE_START &&
                   token->token_id <= KNT_TOKEN_STRUCTURE_END) {
            token->type = KNT_TOKEN_STRUCTURE_START;
        } else {
            token->type = KNT_TOKEN_BYTE_START;
        }

        token_idx++;
        pos++;
    }

    result->token_count = token_idx;
    if (token_idx > 0) {
        result->compression_ratio = (result->input_length * 100) / token_idx;
    }

    tokenizer->total_tokenizations++;
    tokenizer->total_tokens_emitted += token_idx;

    return 0;
}

int kolibri_tokenize_math(KolibriTokenizer *tokenizer,
                          const char *expression,
                          KolibriTokenizationResult *result) {
    /* Пока использу стандартную токенизацию */
    /* В будущем здесь будет специфичная обработка математических структур */
    return kolibri_tokenize(tokenizer, expression, strlen(expression), result);
}

/* ============================================================================
 * ДЕТОКЕНИЗАЦИЯ
 * ============================================================================ */

size_t kolibri_detokenize(KolibriTokenizer *tokenizer,
                          const uint16_t *tokens, int token_count,
                          char *output, size_t output_size) {
    if (!tokens || !output || output_size == 0) return 0;

    size_t pos = 0;

    for (int i = 0; i < token_count && pos < output_size - 1; i++) {
        uint16_t token_id = tokens[i];

        if (token_id <= 255) {
            /* Byte-level токен */
            output[pos++] = (char)token_id;
        } else if (token_id >= KNT_TOKEN_DIGIT_0 && token_id <= KNT_TOKEN_DIGIT_9) {
            /* Цифра */
            output[pos++] = (char)('0' + (token_id - KNT_TOKEN_DIGIT_0));
        } else {
            /* Специальный токен — используем имя */
            const char *name = kolibri_token_name(token_id);
            size_t name_len = strlen(name);
            if (pos + name_len < output_size) {
                memcpy(&output[pos], name, name_len);
                pos += name_len;
            } else {
                break;
            }
        }
    }

    output[pos] = '\0';
    return pos;
}

/* ============================================================================
 * УТИЛИТЫ
 * ============================================================================ */

const char* kolibri_token_name(uint16_t token_id) {
    static char buf[32];

    if (token_id <= 255) {
        if (isprint(token_id)) {
            snprintf(buf, sizeof(buf), "'%c'", (char)token_id);
        } else {
            snprintf(buf, sizeof(buf), "BYTE[%d]", token_id);
        }
        return buf;
    }

    if (token_id >= KNT_TOKEN_DIGIT_0 && token_id <= KNT_TOKEN_DIGIT_9) {
        snprintf(buf, sizeof(buf), "DIGIT_%d", token_id - KNT_TOKEN_DIGIT_0);
        return buf;
    }

    switch (token_id) {
        case KNT_TOKEN_NUMBER_START_MARKER: return "NUM_START";
        case KNT_TOKEN_NUMBER_END_MARKER:   return "NUM_END";
        case KNT_TOKEN_DECIMAL_POINT:       return ".";
        case KNT_TOKEN_NEGATIVE_SIGN:       return "NEG";
        case KNT_TOKEN_EXPONENT_MARKER:     return "EXP";

        case KNT_TOKEN_PLUS:                return "+";
        case KNT_TOKEN_MINUS:               return "-";
        case KNT_TOKEN_MULTIPLY:            return "×";
        case KNT_TOKEN_DIVIDE:              return "÷";
        case KNT_TOKEN_EQUALS:              return "=";
        case KNT_TOKEN_NOT_EQUALS:          return "≠";
        case KNT_TOKEN_LESS_THAN:           return "<";
        case KNT_TOKEN_GREATER_THAN:        return ">";
        case KNT_TOKEN_LESS_EQUAL:          return "≤";
        case KNT_TOKEN_GREATER_EQUAL:       return "≥";
        case KNT_TOKEN_POWER:               return "^";
        case KNT_TOKEN_SQRT:                return "√";
        case KNT_TOKEN_INTEGRAL:            return "∫";
        case KNT_TOKEN_SUMMATION:           return "∑";
        case KNT_TOKEN_PRODUCT:             return "∏";
        case KNT_TOKEN_LIMIT:               return "lim";
        case KNT_TOKEN_DERIVATIVE:          return "d/dx";
        case KNT_TOKEN_PARTIAL_DERIVATIVE:  return "∂";
        case KNT_TOKEN_INFINITY:            return "∞";
        case KNT_TOKEN_APPROXIMATELY:       return "≈";
        case KNT_TOKEN_PERCENT:             return "%";
        case KNT_TOKEN_FACTORIAL:           return "!";
        case KNT_TOKEN_MODULO:              return "mod";
        case KNT_TOKEN_DOT_PRODUCT:         return "·";
        case KNT_TOKEN_UNION:               return "∪";
        case KNT_TOKEN_INTERSECTION:        return "∩";
        case KNT_TOKEN_ELEMENT_OF:          return "∈";
        case KNT_TOKEN_NOT_ELEMENT_OF:      return "∉";
        case KNT_TOKEN_SUBSET:              return "⊂";
        case KNT_TOKEN_SUPERSET:            return "⊃";

        case KNT_TOKEN_SIN:                 return "sin";
        case KNT_TOKEN_COS:                 return "cos";
        case KNT_TOKEN_TAN:                 return "tan";
        case KNT_TOKEN_COT:                 return "cot";
        case KNT_TOKEN_ARCSIN:              return "arcsin";
        case KNT_TOKEN_ARCCOS:              return "arccos";
        case KNT_TOKEN_ARCTAN:              return "arctan";
        case KNT_TOKEN_LOG:                 return "log";
        case KNT_TOKEN_LN:                  return "ln";
        case KNT_TOKEN_EXP_FUNC:            return "exp";
        case KNT_TOKEN_ABS:                 return "abs";
        case KNT_TOKEN_FLOOR:               return "floor";
        case KNT_TOKEN_CEIL:                return "ceil";
        case KNT_TOKEN_MIN:                 return "min";
        case KNT_TOKEN_MAX:                 return "max";
        case KNT_TOKEN_AVG:                 return "avg";

        case KNT_TOKEN_PI:                  return "π";
        case KNT_TOKEN_E:                   return "e";
        case KNT_TOKEN_PHI:                 return "φ";
        case KNT_TOKEN_SQRT2:               return "√2";
        case KNT_TOKEN_SQRT3:               return "√3";
        case KNT_TOKEN_SQRT5:               return "√5";
        case KNT_TOKEN_EULER_GAMMA:         return "γ";

        case KNT_TOKEN_LPAREN:              return "(";
        case KNT_TOKEN_RPAREN:              return ")";
        case KNT_TOKEN_LBRACKET:            return "[";
        case KNT_TOKEN_RBRACKET:            return "]";
        case KNT_TOKEN_LBRACE:              return "{";
        case KNT_TOKEN_RBRACE:              return "}";
        case KNT_TOKEN_COMMA:               return ",";
        case KNT_TOKEN_COLON:               return ":";
        case KNT_TOKEN_SEMICOLON:           return ";";
        case KNT_TOKEN_FRACTION_BAR:        return "—";

        default:
            snprintf(buf, sizeof(buf), "TOKEN[%d]", token_id);
            return buf;
    }
}

int kolibri_is_digit_token(uint16_t token_id) {
    return token_id >= KNT_TOKEN_DIGIT_0 && token_id <= KNT_TOKEN_DIGIT_9;
}

int kolibri_is_math_operator(uint16_t token_id) {
    return token_id >= KNT_TOKEN_PLUS && token_id <= KNT_TOKEN_SUPERSET;
}

int kolibri_is_function(uint16_t token_id) {
    return token_id >= KNT_TOKEN_SIN && token_id <= KNT_TOKEN_STDDEV;
}

int kolibri_is_constant(uint16_t token_id) {
    return token_id >= KNT_TOKEN_PI && token_id <= KNT_TOKEN_CATALAN;
}

double kolibri_constant_value(uint16_t token_id) {
    switch (token_id) {
        case KNT_TOKEN_PI:          return 3.14159265358979323846;
        case KNT_TOKEN_E:           return 2.71828182845904523536;
        case KNT_TOKEN_PHI:         return 1.61803398874989484820;
        case KNT_TOKEN_SQRT2:       return 1.41421356237309504880;
        case KNT_TOKEN_SQRT3:       return 1.73205080756887729352;
        case KNT_TOKEN_SQRT5:       return 2.23606797749978969640;
        case KNT_TOKEN_EULER_GAMMA: return 0.57721566490153286060;
        case KNT_TOKEN_CATALAN:     return 0.91596559417721901505;
        default:                    return 0.0;
    }
}

void kolibri_print_tokenization(const KolibriTokenizationResult *result) {
    if (!result) return;

    printf("Tokenization Result:\n");
    printf("  Input length:    %d\n", result->input_length);
    printf("  Token count:     %d\n", result->token_count);
    printf("  Numbers found:   %d\n", result->num_count);
    printf("  Operators found: %d\n", result->operator_count);
    printf("  Functions found: %d\n", result->function_count);
    printf("  Compression:     %d%%\n", result->compression_ratio);

    printf("\nTokens:\n");
    for (int i = 0; i < result->token_count && i < 50; i++) {
        const KolibriToken *t = &result->tokens[i];
        printf("  [%3d] %-20s", i, kolibri_token_name(t->token_id));

        if (t->is_number) {
            printf(" (num=%.6f, digits=%d)",
                   t->number_info.value,
                   t->number_info.num_digits);
        } else if (t->text[0]) {
            printf(" \"%s\"", t->text);
        }
        printf("\n");
    }

    if (result->token_count > 50) {
        printf("  ... and %d more tokens\n", result->token_count - 50);
    }
}

/* ============================================================================
 * ИНТЕГРАЦИЯ С TRANSFORMER
 * ============================================================================ */

void kolibri_numeric_embedding(uint16_t token_id,
                               const KolibriNumberInfo *number_info,
                               float *embedding, int embed_dim) {
    if (!embedding || embed_dim <= 0) return;

    /* Инициализируем нулями */
    memset(embedding, 0, embed_dim * sizeof(float));

    if (!number_info) {
        /* Обычный токен — one-hot encoding */
        if (token_id < (uint16_t)embed_dim) {
            embedding[token_id] = 1.0f;
        }
        return;
    }

    /* Специальное числовое embedding */
    double value = number_info->value;

    /* Компонент 1: Нормализованное значение числа */
    double normalized = tanh(value / 100.0);  /* Ограничиваем в [-1, 1] */
    embedding[0] = (float)normalized;

    /* Компонент 2: Количество цифр */
    embedding[1] = (float)(number_info->num_digits / 32.0);

    /* Компонент 3: Позиции после запятой */
    embedding[2] = (float)(number_info->decimal_places / 16.0);

    /* Компонент 4: Знак */
    embedding[3] = number_info->is_negative ? -1.0f : 1.0f;

    /* Компонент 5: Есть ли экспонента */
    embedding[4] = number_info->has_exponent ? 1.0f : 0.0f;

    /* Компоненты 6-11: Позиционное кодирование цифр числа */
    if (number_info->num_digits > 0) {
        double temp = number_info->value;
        for (int i = 0; i < 6 && i < number_info->num_digits; i++) {
            int digit = (int)fmod(temp, 10.0);
            embedding[5 + i] = digit / 9.0f;
            temp = floor(temp / 10.0);
        }
    }

    /* Остальные компоненты: sinusoidal encoding от значения */
    for (int i = 11; i < embed_dim; i++) {
        double angle = value / pow(10000.0, 2.0 * (i - 11) / (embed_dim - 11));
        embedding[i] = (i % 2 == 0) ? (float)sin(angle) : (float)cos(angle);
    }
}

int kolibri_vocab_size(void) {
    return KNT_VOCAB_EXTENDED;
}
