/*
 * self_verification.c
 *
 * Реализация протокола самопроверки Kolibri
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/self_verification.h"
#include "kolibri/math_solver.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * ВНУТРЕННИЕ ФУНКЦИИ
 * ============================================================================ */

static double sv_time_ms(void);
static uint32_t sv_hash_answer(const char *answer);
static double sv_extract_number(const char *answer);
static double sv_answer_similarity(const char *a1, const char *a2);

static double sv_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* Простой hash для сравнения ответов */
static uint32_t sv_hash_answer(const char *answer) {
    if (!answer)
        return 0;
    uint32_t hash = 5381;
    for (const char *p = answer; *p; p++) {
        hash = ((hash << 5) + hash) + (unsigned char)*p;
    }
    return hash;
}

/* 相似度 ответов (0.0 = совершенно разные, 1.0 = идентичны) */
static double sv_answer_similarity(const char *a1, const char *a2) {
    if (!a1 || !a2)
        return 0.0;
    if (strcmp(a1, a2) == 0)
        return 1.0;

    /* Проверяем числа */
    double n1 = sv_extract_number(a1);
    double n2 = sv_extract_number(a2);
    if (n1 != 0.0 || strstr(a1, "0")) {
        if (n2 != 0.0 || strstr(a2, "0")) {
            double diff = fabs(n1 - n2);
            if (diff < 0.0001)
                return 1.0;
            if (diff < 0.1)
                return 0.9;
        }
    }

    /* Простая схожесть строк: перекрытие слов */
    int matches = 0;
    int total = 0;
    char s1[1024], s2[1024];
    strncpy(s1, a1, 1023);
    strncpy(s2, a2, 1023);

    char *saveptr1, *saveptr2;
    char *tok1 = strtok_r(s1, " =,", &saveptr1);
    while (tok1) {
        if (strlen(tok1) > 0) {
            total++;
            if (strstr(a2, tok1))
                matches++;
        }
        tok1 = strtok_r(NULL, " =,", &saveptr1);
    }

    if (total == 0) return 0.0;
    return (double)matches / total;
}

/* Извлекаем число из ответа (если есть) */
static double sv_extract_number(const char *answer) {
    if (!answer)
        return 0.0;

    /* Ищем первое число в строке */
    const char *p = answer;
    while (*p && (*p < '0' || *p > '9') && *p != '-' && *p != '.')
        p++;
    if (!*p)
        return 0.0;

    return strtod(p, NULL);
}

/* ============================================================================
 * ВНУТРЕННИЕ ФУНКЦИИ — МАТЕМАТИЧЕСКИЙ ПАРСЕР
 * ============================================================================ */

/* Вычисляет простое арифметическое выражение вида "число оп число [оп число ...]"
 * Поддерживает: +, -, *, /, скобки не поддерживаются (упрощённый парсер)
 * Возвращает 1 если успешно вычислил, 0 если не удалось */
static int sv_eval_simple_expr(const char *expr, double *out_result) {
    if (!expr || !out_result)
        return 0;

    /* Пропускаем пробелы в начале */
    while (*expr == ' ' || *expr == '\t')
        expr++;

    /* Проверяем что есть хотя бы одно число и оператор */
    const char *p = expr;
    int has_operator = 0;
    while (*p) {
        if (*p == '+' || *p == '-' || *p == '*' || *p == '/') {
            /* Проверяем что минус — это оператор, а не знак числа */
            if (*p == '-' && p == expr) {
                p++;
                continue;
            }
            has_operator = 1;
            break;
        }
        p++;
    }
    if (!has_operator)
        return 0; /* Нет операторов — не выражение */

    /* Простой парсер: читаем число, оператор, число, ... слева направо */
    char *endptr;
    double result = strtod(expr, &endptr);
    if (endptr == expr)
        return 0; /* Не удалось прочитать число */

    while (*endptr) {
        /* Пропускаем пробелы */
        while (*endptr == ' ' || *endptr == '\t')
            endptr++;
        if (!*endptr)
            break;

        char op = *endptr;
        if (op != '+' && op != '-' && op != '*' && op != '/')
            break;
        endptr++;

        while (*endptr == ' ' || *endptr == '\t')
            endptr++;

        char *prev_endptr = endptr;
        double next_num = strtod(endptr, &endptr);
        if (endptr == prev_endptr)
            return 0; /* Не удалось распознать число */

        switch (op) {
        case '+':
            result += next_num;
            break;
        case '-':
            result -= next_num;
            break;
        case '*':
            result *= next_num;
            break;
        case '/':
            if (next_num == 0.0)
                return 0; /* Деление на ноль */
            result /= next_num;
            break;
        }
    }

    *out_result = result;
    return 1;
}

/* Проверяет содержит ли строка математическое выражение */
static int sv_has_math_expression(const char *str) {
    if (!str)
        return 0;
    int has_digit = 0;
    int has_operator = 0;
    for (const char *p = str; *p; p++) {
        if (*p >= '0' && *p <= '9')
            has_digit = 1;
        if (*p == '+' || *p == '-' || *p == '*' || *p == '/' || *p == '=')
            has_operator = 1;
    }
    return has_digit && has_operator;
}

/* Извлекает математическое выражение из строки (ищет паттерн "число оп число" или "...=число") */
static int sv_extract_math_expr(const char *str, char *out_expr, size_t out_size) {
    if (!str || !out_expr || out_size == 0)
        return 0;

    /* Ищем паттерн: цифры/операторы/точки/пробелы, содержащий оператор */
    const char *start = NULL;
    const char *p = str;

    while (*p) {
        /* Пропускаем не-математические символы */
        while (*p && !((*p >= '0' && *p <= '9') || *p == '+' || *p == '-' || *p == '*' || *p == '/' || *p == '.' ||
                       *p == '='))
            p++;
        if (!*p)
            break;

        start = p;
        /* Собираем математическую подстроку */
        while (*p && ((*p >= '0' && *p <= '9') || *p == '+' || *p == '-' || *p == '*' || *p == '/' || *p == '.' ||
                      *p == ' ' || *p == '=')) {
            p++;
        }

        size_t len = (size_t)(p - start);
        if (len >= 5) { /* Минимальная длина выражения */
            /* Проверяем что есть оператор */
            int has_op = 0;
            for (const char *q = start; q < p; q++) {
                if (*q == '+' || *q == '-' || *q == '*' || *q == '/' || *q == '=') {
                    has_op = 1;
                    break;
                }
            }
            if (has_op && len < out_size) {
                memcpy(out_expr, start, len);
                out_expr[len] = '\0';
                return 1;
            }
        }
    }
    return 0;
}

/* ============================================================================
 * ВНУТРЕННИЕ ФУНКЦИИ — ЛОГИЧЕСКАЯ ПРОВЕРКА
 * ============================================================================ */

/* Проверяет наличие логических операторов в запросе */
static int sv_has_logical_operators(const char *query) {
    if (!query)
        return 0;
    const char *patterns[] = {"если",    "то",      "или",        "не",      "и ",     "все ",  "каждый",
                              "ни один", "следует", "потому что", "поэтому", "значит", "вывод", "if ",
                              "then ",   "or ",     "not ",       "and ",    "all ",   "every"};
    size_t n_patterns = sizeof(patterns) / sizeof(patterns[0]);
    for (size_t i = 0; i < n_patterns; i++) {
        if (strstr(query, patterns[i]))
            return 1;
    }
    return 0;
}

/* Простая проверка логической непротиворечивости: ищем отрицания в ответе */
static double sv_check_logical_consistency(const char *query, const char *answer) {
    if (!query || !answer)
        return 0.5;

    double confidence = 0.7; /* Базовая уверенность */

    /* Если query содержит логические операторы, проверяем ответ на связность */
    if (sv_has_logical_operators(query)) {
        /* Проверяем что ответ не пустой и содержит текст */
        size_t len = strlen(answer);
        if (len > 5) {
            confidence += 0.1;
        }

        /* Проверяем на внутренние противоречия: одновременное присутствие
         * взаимоисключающих паттернов */
        int has_yes =
            (strstr(answer, "да") || strstr(answer, "yes") || strstr(answer, "верно") || strstr(answer, "true"));
        int has_no =
            (strstr(answer, "нет") || strstr(answer, "no") || strstr(answer, "неверно") || strstr(answer, "false"));
        if (has_yes && has_no) {
            confidence -= 0.3; /* Противоречие в ответе */
        }
    } else {
        /* Нет логических операторов — понижаем уверенность для этого метода */
        confidence = 0.5;
    }

    if (confidence < 0.0)
        confidence = 0.0;
    if (confidence > 1.0)
        confidence = 1.0;
    return confidence;
}

/* ============================================================================
 * ВНУТРЕННИЕ ФУНКЦИИ — ПРОВЕРКА ЧЕРЕЗ KNOWLEDGE BASE
 * ============================================================================ */

#define KB_FILE_PATH (getenv("KOLIBRI_KB_PATH") ? getenv("KOLIBRI_KB_PATH") : "knowledge/knowledge_base_qa.md")
#define KB_MAX_LINE 2048
#define KB_MAX_ENTRIES 200

typedef struct {
    char question[512];
    char answer[512];
} KBEntry;

/* Загружает knowledge base из файла в массив */
static int sv_load_knowledge_base(KBEntry *entries, int max_entries) {
    FILE *f = fopen(KB_FILE_PATH, "r");
    if (!f)
        return 0;

    int count = 0;
    char line[KB_MAX_LINE];
    int in_question = 0;
    int in_answer = 0;
    char current_q[512] = {0};
    char current_a[512] = {0};

    while (fgets(line, sizeof(line), f) && count < max_entries) {
        /* Убираем newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        /* Проверяем начало вопроса: "### Q: ..." */
        if (strncmp(line, "### Q: ", 7) == 0) {
            /* Сохраняем предыдущий если был */
            if (current_q[0] && current_a[0]) {
                snprintf(entries[count].question, sizeof(entries[count].question), "%s", current_q);
                snprintf(entries[count].answer, sizeof(entries[count].answer), "%s", current_a);
                count++;
            }
            snprintf(current_q, sizeof(current_q), "%s", line + 7);
            current_a[0] = '\0';
            in_question = 1;
            in_answer = 0;
        } else if (strncmp(line, "**Ответ:**", 10) == 0) {
            snprintf(current_a, sizeof(current_a), "%s", line + 10);
            /* Убираем leading spaces */
            char *p = current_a;
            while (*p == ' ')
                p++;
            memmove(current_a, p, strlen(p) + 1);
            in_answer = 1;
            in_question = 0;
        } else if (strncmp(line, "---", 3) == 0) {
            /* Конец записи */
            if (current_q[0] && current_a[0] && count < max_entries) {
                snprintf(entries[count].question, sizeof(entries[count].question), "%s", current_q);
                snprintf(entries[count].answer, sizeof(entries[count].answer), "%s", current_a);
                count++;
                current_q[0] = '\0';
                current_a[0] = '\0';
            }
            in_question = 0;
            in_answer = 0;
        }
    }

    /* Последняя запись */
    if (current_q[0] && current_a[0] && count < max_entries) {
        snprintf(entries[count].question, sizeof(entries[count].question), "%s", current_q);
        snprintf(entries[count].answer, sizeof(entries[count].answer), "%s", current_a);
        count++;
    }

    fclose(f);
    return count;
}

/* Простой подсчёт совпадающих слов между двумя строками */
static double sv_keyword_overlap(const char *query, const char *kb_question) {
    if (!query || !kb_question)
        return 0.0;

    /* Токенизируем query */
    char q_copy[1024];
    snprintf(q_copy, sizeof(q_copy), "%s", query);

    int total_words = 0;
    int matched_words = 0;
    char *saveptr = NULL;

    char *token = strtok_r(q_copy, " ,.!?;:\"\t\n\r", &saveptr);
    while (token) {
        if (strlen(token) >= 2) { /* Игнорируем короткие слова */
            total_words++;
            /* Ищем слово в kb_question (case-insensitive упрощённо) */
            char lower_token[64];
            size_t tlen = strlen(token);
            if (tlen < sizeof(lower_token)) {
                for (size_t i = 0; i < tlen; i++) {
                    lower_token[i] = (token[i] >= 'A' && token[i] <= 'Z') ? token[i] + 32 : token[i];
                }
                lower_token[tlen] = '\0';

                char lower_kb[1024];
                snprintf(lower_kb, sizeof(lower_kb), "%s", kb_question);
                for (size_t i = 0; lower_kb[i]; i++) {
                    if (lower_kb[i] >= 'A' && lower_kb[i] <= 'Z')
                        lower_kb[i] += 32;
                }

                if (strstr(lower_kb, lower_token))
                    matched_words++;
            }
        }
        token = strtok_r(NULL, " ,.!?;:\"\t\n\r", &saveptr);
    }

    return (total_words > 0) ? ((double)matched_words / total_words) : 0.0;
}

/* ============================================================================
 * ПРОВЕРОЧНЫЕ МЕТОДЫ
 * ============================================================================ */

/* Метод 1: Проверка через формулы */
static int sv_check_formula(const char *query, const char *primary, KolibriSVMethodResult *result) {
    result->method = KSV_METHOD_FORMULA;

    /* Проверка через решатель уравнений */
    KolibriEquationSolution sol;
    if (kolibri_solve(query, &sol) == 0) {
        snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%.6g", sol.x1);
        result->confidence = 0.96;
        result->success = 1;
        snprintf(result->provenance, 256, "formula_solver:exact");
        return 0;
    }

    /* Реальная проверка: пытаемся вычислить выражение независимо */
    char expr[512] = {0};

    /* Сначала пробуем извлечь выражение из primary answer */
    if (sv_extract_math_expr(primary, expr, sizeof(expr))) {
        /* Нашли математическое выражение в ответе — вычисляем */
        double computed = 0.0;
        if (sv_eval_simple_expr(expr, &computed)) {
            /* Сравниваем вычисленный результат с числом в primary */
            double primary_num = sv_extract_number(primary);
            if (primary_num != 0.0 || strstr(primary, "0")) {
                double diff = fabs(computed - primary_num);
                if (diff < 0.001) {
                    /* Точное совпадение */
                    snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%.6g", computed);
                    result->confidence = 0.95;
                    result->success = 1;
                    snprintf(result->provenance, 256, "formula_pool:computed");
                    return 0;
                } else if (diff < 1.0) {
                    /* Близкое совпадение */
                    snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%.6g", computed);
                    result->confidence = 0.80;
                    result->success = 1;
                    snprintf(result->provenance, 256, "formula_pool:close_match");
                    return 0;
                } else {
                    /* Расхождение */
                    snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%.6g (computed) vs %s", computed, primary);
                    result->confidence = 0.3;
                    result->success = 1;
                    snprintf(result->provenance, 256, "formula_pool:divergent");
                    return 0;
                }
            }
        }
    }

    /* Нет математического выражения — проверяем query на формульность */
    if (sv_has_math_expression(query)) {
        /* Query содержит математику, но primary — нет. Это подозрительно. */
        result->success = 1;
        snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%s", primary);
        result->confidence = 0.4;
        snprintf(result->provenance, 256, "formula_pool:no_expr_in_answer");
    } else {
        /* Не формульный запрос — метод не применим */
        result->success = 0;
        result->confidence = 0.0;
        result->answer[0] = '\0';
        snprintf(result->provenance, 256, "formula_pool:not_applicable");
    }

    return -1;
}

/* Метод 2: Проверка через логический вывод */
static int sv_check_logical(const char *query, const char *primary, KolibriSVMethodResult *result) {
    result->method = KSV_METHOD_LOGICAL;

    /* Реальная логическая проверка */
    double confidence = sv_check_logical_consistency(query, primary);

    if (sv_has_logical_operators(query)) {
        /* Логический запрос — проверяем связность ответа */
        result->success = 1;
        snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%s", primary);
        result->confidence = confidence;
        snprintf(result->provenance, 256, "logical_memory:consistency_check");
        return 0;
    }

    /* Для фактовых запросов — проверяем непротиворечивость */
    if (strlen(primary) > 3) {
        /* Ответ достаточно длинный — базовая проверка */
        int has_yes = (strstr(primary, "да") || strstr(primary, "yes") || strstr(primary, "верно"));
        int has_no = (strstr(primary, "нет") || strstr(primary, "no") || strstr(primary, "неверно"));

        if (has_yes && has_no) {
            /* Противоречие */
            result->success = 1;
            snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%s", primary);
            result->confidence = 0.2;
            snprintf(result->provenance, 256, "logical_memory:contradiction");
            return 0;
        }

        result->success = 1;
        snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%s", primary);
        result->confidence = confidence;
        snprintf(result->provenance, 256, "logical_memory:factual");
        return 0;
    }

    /* Слишком короткий ответ — метод не применим */
    result->success = 0;
    result->confidence = 0.0;
    result->answer[0] = '\0';
    snprintf(result->provenance, 256, "logical_memory:not_applicable");
    return -1;
}

/* Метод 3: Проверка через граф знаний */
static int sv_check_knowledge(const char *query, const char *primary, KolibriSVMethodResult *result) {
    result->method = KSV_METHOD_KNOWLEDGE;

    /* Упрощённая проверка через keyword overlap (без загрузки файла) */
    if (!query || !primary || strlen(query) < 3 || strlen(primary) < 2) {
        result->success = 0;
        result->confidence = 0.0;
        result->answer[0] = '\0';
        snprintf(result->provenance, 256, "knowledge_graph:skipped");
        return -1;
    }

    char q_lower[2048], a_lower[2048];
    size_t ql = strlen(query) < sizeof(q_lower) - 1 ? strlen(query) : sizeof(q_lower) - 1;
    size_t al = strlen(primary) < sizeof(a_lower) - 1 ? strlen(primary) : sizeof(a_lower) - 1;
    for (size_t i = 0; i < ql; i++)
        q_lower[i] = (char)tolower((unsigned char)query[i]);
    q_lower[ql] = '\0';
    for (size_t i = 0; i < al; i++)
        a_lower[i] = (char)tolower((unsigned char)primary[i]);
    a_lower[al] = '\0';

    int overlap = 0, total = 0;
    char q_copy[2048];
    strncpy(q_copy, q_lower, sizeof(q_copy) - 1);
    char *saveptr2 = NULL;
    char *tok = strtok_r(q_copy, " ,.!?;:\"'-_()[]{}", &saveptr2);
    while (tok) {
        if (strlen(tok) > 2) {
            total++;
            if (strstr(a_lower, tok))
                overlap++;
        }
        tok = strtok_r(NULL, " ,.!?;:\"'-_()[]{}", &saveptr2);
    }

    double conf = total > 0 ? (double)overlap / total : 0.0;
    result->success = 1;
    snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%s", primary);
    result->confidence = conf > 0.5 ? 0.90 : (conf > 0.2 ? 0.65 : 0.30);
    snprintf(result->provenance, 256, "knowledge_graph:overlap_%d_of_%d", overlap, total);
    return 0;
}

/* Метод 4: Проверка через точную арифметику */
static int sv_check_arithmetic(const char *query, const char *primary, KolibriSVMethodResult *result) {
    result->method = KSV_METHOD_ARITHMETIC;

    /* Проверяем есть ли числа в ответе */
    double primary_num = sv_extract_number(primary);
    if (primary_num == 0.0 && strstr(primary, "0") == NULL) {
        /* Нет чисел — пропускаем */
        result->success = 0;
        result->confidence = 0.0;
        result->answer[0] = '\0';
        snprintf(result->provenance, 256, "decimal:no_numbers");
        return -1;
    }

    /* Метод: Проверка через решатель уравнений */
    KolibriEquationSolution sol;
    if (kolibri_solve(query, &sol) == 0) {
        double expected = sol.x1;
        double diff = fabs(expected - primary_num);
        
        snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%.6g", expected);
        
        if (diff < 0.001) {
            result->confidence = 0.99;
            result->success = 1;
            snprintf(result->provenance, 256, "math_solver:exact");
            return 0;
        }
    }

    /* Ищем математическое выражение в primary answer */
    char expr[512] = {0};
    if (sv_extract_math_expr(primary, expr, sizeof(expr))) {
        /* Вычисляем выражение независимо */
        double computed = 0.0;
        if (sv_eval_simple_expr(expr, &computed)) {
            /* Сравниваем с числом в primary */
            double diff = fabs(computed - primary_num);

            snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%.6g", computed);

            if (diff < 0.001) {
                /* Точное совпадение */
                result->confidence = 0.98;
                result->success = 1;
                snprintf(result->provenance, 256, "decimal_exact");
                return 0;
            } else if (diff < 1.0) {
                /* Близкое совпадение (возможно округление) */
                result->confidence = 0.85;
                result->success = 1;
                snprintf(result->provenance, 256, "decimal_rounded");
                return 0;
            } else {
                /* Расхождение — потенциальная ошибка */
                result->confidence = 0.2;
                result->success = 1;
                snprintf(result->provenance, 256, "decimal:divergent");
                return 0;
            }
        }
    }

    /* Если query содержит математику, пробуем вычислить из query */
    if (sv_extract_math_expr(query, expr, sizeof(expr))) {
        double computed = 0.0;
        if (sv_eval_simple_expr(expr, &computed)) {
            /* Сравниваем вычисленный результат с primary */
            double diff = fabs(computed - primary_num);

            snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%.6g", computed);

            if (diff < 0.001) {
                result->confidence = 0.95;
                result->success = 1;
                snprintf(result->provenance, 256, "decimal_from_query:exact");
                return 0;
            } else if (diff < 1.0) {
                result->confidence = 0.80;
                result->success = 1;
                snprintf(result->provenance, 256, "decimal_from_query:close");
                return 0;
            } else {
                result->confidence = 0.25;
                result->success = 1;
                snprintf(result->provenance, 256, "decimal_from_query:divergent");
                return 0;
            }
        }
    }

    /* Число есть, но выражения нет — просто подтверждаем наличие */
    result->success = 1;
    snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%s", primary);
    result->confidence = 0.6; /* Низкая уверенность без вычисления */
    snprintf(result->provenance, 256, "decimal:number_only");
    return 0;
}

/* ============================================================================
 * API РЕАЛИЗАЦИЯ
 * ============================================================================ */

int kolibri_sv_init(KolibriSVConfig *config) {
    if (!config)
        return -1;

    /* Defaults */
    if (!config->enable_formula_check)
        config->enable_formula_check = 1;
    if (!config->enable_logical_check)
        config->enable_logical_check = 1;
    if (!config->enable_knowledge_check)
        config->enable_knowledge_check = 1;
    if (!config->enable_arithmetic_check)
        config->enable_arithmetic_check = 1;
    if (config->agreement_threshold <= 0.0)
        config->agreement_threshold = 0.7;
    if (!config->min_methods_required)
        config->min_methods_required = 2;

    return 0;
}

int kolibri_sv_verify_answer(const char *query, const char *primary_answer, const KolibriSVConfig *config,
                             KolibriSVReport *report, KolibriSVProgressCallback callback, void *user_data) {

    if (!query || !primary_answer || !report)
        return -1;

    double start = sv_time_ms();
    memset(report, 0, sizeof(KolibriSVReport));

    /* Сохраняем primary */
    snprintf(report->primary_answer, KSV_MAX_ANSWER_LEN, "%s", primary_answer);
    report->primary_method = KSV_METHOD_FORMULA;
    report->primary_confidence = 0.85;

    /* Step 1: Formula check */
    int step = 0;
    if (config->enable_formula_check) {
        report->steps[step].step_num = step + 1;
        snprintf(report->steps[step].description, 256, "Проверка через формулы");

        KolibriSVMethodResult *res = &report->results[report->num_methods_used];
        double method_start = sv_time_ms();
        sv_check_formula(query, primary_answer, res);
        res->elapsed_ms = sv_time_ms() - method_start;

        report->steps[step].intermediate_confidence = res->confidence;
        snprintf(report->steps[step].detail, 512, "Formula confidence: %.2f", res->confidence);

        if (res->success)
            report->num_methods_used++;
        step++;

        if (callback)
            callback(step, "formula", res->confidence, user_data);
    }

    /* Step 2: Logical check */
    if (config->enable_logical_check) {
        report->steps[step].step_num = step + 1;
        snprintf(report->steps[step].description, 256, "Проверка через логический вывод");

        KolibriSVMethodResult *res = &report->results[report->num_methods_used];
        double method_start = sv_time_ms();
        sv_check_logical(query, primary_answer, res);
        res->elapsed_ms = sv_time_ms() - method_start;

        report->steps[step].intermediate_confidence = res->confidence;
        snprintf(report->steps[step].detail, 512, "Logical confidence: %.2f", res->confidence);

        if (res->success)
            report->num_methods_used++;
        step++;

        if (callback)
            callback(step, "logical", res->confidence, user_data);
    }

    /* Step 3: Knowledge check */
    if (config->enable_knowledge_check) {
        report->steps[step].step_num = step + 1;
        snprintf(report->steps[step].description, 256, "Проверка через граф знаний");

        KolibriSVMethodResult *res = &report->results[report->num_methods_used];
        double method_start = sv_time_ms();
        sv_check_knowledge(query, primary_answer, res);
        res->elapsed_ms = sv_time_ms() - method_start;

        report->steps[step].intermediate_confidence = res->confidence;
        snprintf(report->steps[step].detail, 512, "Knowledge confidence: %.2f", res->confidence);

        if (res->success)
            report->num_methods_used++;
        step++;

        if (callback)
            callback(step, "knowledge", res->confidence, user_data);
    }

    /* Step 4: Arithmetic check */
    if (config->enable_arithmetic_check) {
        report->steps[step].step_num = step + 1;
        snprintf(report->steps[step].description, 256, "Проверка через точную арифметику");

        KolibriSVMethodResult *res = &report->results[report->num_methods_used];
        double method_start = sv_time_ms();
        int ret = sv_check_arithmetic(query, primary_answer, res);
        res->elapsed_ms = sv_time_ms() - method_start;

        report->steps[step].intermediate_confidence = res->confidence;
        snprintf(report->steps[step].detail, 512, ret == 0 ? "Arithmetic exact: %.2f" : "Arithmetic: not applicable",
                 res->confidence);

        if (res->success)
            report->num_methods_used++;
        step++;

        if (callback)
            callback(step, "arithmetic", res->confidence, user_data);
    }

    report->num_steps = step;

    /* Detect contradictions */
    kolibri_sv_detect_contradictions(report);

    /* Compute final confidence */
    report->final_confidence = kolibri_sv_compute_final_confidence(report);

    /* Check agreement */
    report->agreement = kolibri_sv_check_agreement(report);

    /* Generate recommendation */
    kolibri_sv_generate_recommendation(report);

    report->total_verification_ms = sv_time_ms() - start;
    report->verification_passed = (report->final_confidence >= config->agreement_threshold) ? 1 : 0;

    return 0;
}

int kolibri_sv_check_agreement(const KolibriSVReport *report) {
    if (!report || report->num_methods_used < 2)
        return 0;

    /* Проверяем попарное согласие */
    int agreements = 0;
    int comparisons = 0;

    for (int i = 0; i < report->num_methods_used; i++) {
        for (int j = i + 1; j < report->num_methods_used; j++) {
            if (!report->results[i].success || !report->results[j].success)
                continue;

            double sim = sv_answer_similarity(report->results[i].answer, report->results[j].answer);

            if (sim > 0.8)
                agreements++;
            comparisons++;
        }
    }

    if (comparisons == 0)
        return 0;
    return (double)agreements / comparisons > 0.75;
}

int kolibri_sv_detect_contradictions(KolibriSVReport *report) {
    if (!report)
        return 0;

    report->num_contradictions = 0;

    /* Проверяем все пары */
    for (int i = 0; i < report->num_methods_used; i++) {
        for (int j = i + 1; j < report->num_methods_used; j++) {
            if (!report->results[i].success || !report->results[j].success)
                continue;

            double sim = sv_answer_similarity(report->results[i].answer, report->results[j].answer);
            double divergence = 1.0 - sim;

            if (divergence > 0.3 && report->num_contradictions < KSV_MAX_CONTRADICTIONS) {
                /* DEBUG */
                fprintf(stderr, "[sv-debug] Contradiction between %d and %d: '%s' vs '%s' (sim: %.4f)\n",
                        report->results[i].method, report->results[j].method,
                        report->results[i].answer, report->results[j].answer, sim);
                
                KolibriSVContradiction *c = &report->contradictions[report->num_contradictions];
                c->method1 = report->results[i].method;
                c->method2 = report->results[j].method;

                snprintf(c->answer1, KSV_MAX_ANSWER_LEN, "%s", report->results[i].answer);
                snprintf(c->answer2, KSV_MAX_ANSWER_LEN, "%s", report->results[j].answer);
                c->divergence = divergence;

                snprintf(c->explanation, 256, "%s vs %s: divergence %.2f", kolibri_sv_method_name(c->method1),
                         kolibri_sv_method_name(c->method2), divergence);

                report->num_contradictions++;
            }
        }
    }

    return report->num_contradictions;
}

double kolibri_sv_compute_final_confidence(const KolibriSVReport *report) {
    if (!report || report->num_methods_used == 0)
        return 0.0;

    /* Средневзвешенная confidence */
    double total_conf = 0.0;
    int count = 0;

    for (int i = 0; i < report->num_methods_used; i++) {
        if (report->results[i].success) {
            total_conf += report->results[i].confidence;
            count++;
        }
    }

    if (count == 0)
        return 0.0;

    double avg_conf = total_conf / count;

    /* Штраф за противоречия */
    double penalty = report->num_contradictions * 0.1;

    double final_confidence = avg_conf - penalty;
    if (final_confidence < 0.0)
        final_confidence = 0.0;
    if (final_confidence > 1.0)
        final_confidence = 1.0;

    return final_confidence;
}

void kolibri_sv_generate_recommendation(KolibriSVReport *report) {
    if (!report)
        return;

    if (report->final_confidence >= 0.9) {
        snprintf(report->recommendation, 512, "✓ Высокая уверенность (%.2f). Ответ можно выдавать.",
                 report->final_confidence);
    } else if (report->final_confidence >= 0.7) {
        snprintf(report->recommendation, 512, "✓ Средняя уверенность (%.2f). Ответ можно выдавать с пометкой.",
                 report->final_confidence);
    } else if (report->num_contradictions > 0) {
        snprintf(report->recommendation, 512,
                 "⚠ Обнаружены противоречия (%d). Уверенность: %.2f. "
                 "Рекомендуется объяснить неопределённость пользователю.",
                 report->num_contradictions, report->final_confidence);
    } else {
        snprintf(report->recommendation, 512, "✗ Низкая уверенность (%.2f). Ответ требует проверки.",
                 report->final_confidence);
    }
}

/* ============================================================================
 * ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
 * ============================================================================ */

void kolibri_sv_print_report(const KolibriSVReport *report) {
    if (!report)
        return;

    printf("\n=== Self-Verification Report ===\n");
    printf("Primary answer: %s\n", report->primary_answer);
    printf("Methods used: %d\n", report->num_methods_used);

    for (int i = 0; i < report->num_methods_used; i++) {
        const KolibriSVMethodResult *r = &report->results[i];
        printf("  [%s] %s (confidence: %.2f, time: %.1fms)\n", r->success ? "✓" : "✗",
               kolibri_sv_method_name(r->method), r->confidence, r->elapsed_ms);
    }

    if (report->num_contradictions > 0) {
        printf("\nContradictions: %d\n", report->num_contradictions);
        for (int i = 0; i < report->num_contradictions; i++) {
            printf("  ⚠ %s\n", report->contradictions[i].explanation);
        }
    }

    printf("\nFinal confidence: %.2f\n", report->final_confidence);
    printf("Agreement: %s\n", report->agreement ? "YES" : "NO");
    printf("Verification: %s\n",
           report->verification_passed > 0 ? "PASSED" : (report->verification_passed == 0 ? "FAILED" : "UNKNOWN"));
    printf("\nRecommendation: %s\n", report->recommendation);
    printf("Total time: %.1fms\n", report->total_verification_ms);
    printf("================================\n\n");
}

int kolibri_sv_save_report(const KolibriSVReport *report, const char *filepath) {
    if (!report || !filepath)
        return -1;

    FILE *f = fopen(filepath, "w");
    if (!f)
        return -2;

    fprintf(f, "self_verification_report\n");
    fprintf(f, "primary_answer=%s\n", report->primary_answer);
    fprintf(f, "methods_used=%d\n", report->num_methods_used);
    fprintf(f, "final_confidence=%.4f\n", report->final_confidence);
    fprintf(f, "agreement=%d\n", report->agreement);
    fprintf(f, "contradictions=%d\n", report->num_contradictions);
    fprintf(f, "verification_passed=%d\n", report->verification_passed);
    fprintf(f, "total_time_ms=%.1f\n", report->total_verification_ms);

    fclose(f);
    return 0;
}

const char *kolibri_sv_method_name(KolibriSVMethod method) {
    switch (method) {
    case KSV_METHOD_FORMULA:
        return "Formula";
    case KSV_METHOD_LOGICAL:
        return "Logical";
    case KSV_METHOD_KNOWLEDGE:
        return "Knowledge";
    case KSV_METHOD_ARITHMETIC:
        return "Arithmetic";
    case KSV_METHOD_ANALOGY:
        return "Analogy";
    default:
        return "Unknown";
    }
}

const char *kolibri_sv_method_desc(KolibriSVMethod method) {
    switch (method) {
    case KSV_METHOD_FORMULA:
        return "Проверка через формульный пул";
    case KSV_METHOD_LOGICAL:
        return "Проверка через логический вывод";
    case KSV_METHOD_KNOWLEDGE:
        return "Проверка через граф знаний";
    case KSV_METHOD_ARITHMETIC:
        return "Проверка через точную арифметику (decimal.c)";
    case KSV_METHOD_ANALOGY:
        return "Проверка через аналогии";
    default:
        return "Неизвестный метод";
    }
}
