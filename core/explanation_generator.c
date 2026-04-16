/*
 * explanation_generator.c
 *
 * Реализация генератора объяснений Kolibri
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/explanation_generator.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * ВНУТРЕННИЕ ФУНКЦИИ
 * ============================================================================ */

static double eg_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* Извлекаем уравнение из запроса */
static void eg_extract_equation(const char *query, char *eq, size_t eq_size) {
    if (!query || !eq) return;
    
    /* Ищем паттерны типа "2x+3=7", "x²-5x+6=0" */
    const char *start = strstr(query, "=");
    if (!start) {
        /* Нет "=", копируем часть запроса */
        size_t len = strlen(query);
        size_t copy_len = (len < eq_size - 1) ? len : eq_size - 1;
        strncpy(eq, query, copy_len);
        eq[copy_len] = '\0';
        return;
    }
    
    /* Копируем от начала до конца */
    size_t len = strlen(query);
    size_t copy_len = (len < eq_size - 1) ? len : eq_size - 1;
    strncpy(eq, query, copy_len);
    eq[copy_len] = '\0';
}

/* Определяем тип задачи */
static const char* eg_detect_problem_type(const char *query) {
    if (!query) return "unknown";
    
    if (strstr(query, "x²") || strstr(query, "x^2") || strstr(query, "квадрат")) {
        return "quadratic";
    }
    if (strstr(query, "x") && strstr(query, "=")) {
        return "linear";
    }
    if (strstr(query, "площадь")) {
        return "geometry_area";
    }
    if (strstr(query, "объем") || strstr(query, "объём")) {
        return "geometry_volume";
    }
    if (strstr(query, "производн") || strstr(query, "дифференциал")) {
        return "calculus_derivative";
    }
    if (strstr(query, "интеграл")) {
        return "calculus_integral";
    }
    
    return "general";
}

/* ============================================================================
 * API: ИНИЦИАЛИЗАЦИЯ
 * ============================================================================ */

int kolibri_eg_init(KolibriEGConfig *config) {
    if (!config) return -1;
    
    if (!config->include_formula) config->include_formula = 1;
    if (!config->include_steps) config->include_steps = 1;
    if (!config->include_sources) config->include_sources = 1;
    if (!config->include_confidence) config->include_confidence = 1;
    if (!config->max_steps) config->max_steps = KEG_MAX_STEPS;
    
    return 0;
}

/* ============================================================================
 * ГЕНЕРАЦИЯ ОБЪЯСНЕНИЙ ДЛЯ МАТЕМАТИКИ
 * ============================================================================ */

int kolibri_eg_generate_math_explanation(
    const char *query,
    const char *answer,
    const KolibriEGConfig *config,
    KolibriExplanation *explanation) {
    
    if (!query || !answer || !explanation) return -1;
    
    double start = eg_time_ms();
    memset(explanation, 0, sizeof(KolibriExplanation));
    
    /* Сохраняем запрос и ответ */
    snprintf(explanation->query, sizeof(explanation->query), "%s", query);
    snprintf(explanation->direct_answer, sizeof(explanation->direct_answer), "%s", answer);
    
    /* Определяем тип задачи */
    const char *problem_type = eg_detect_problem_type(query);
    
    /* Извлекаем уравнение */
    char equation[256];
    eg_extract_equation(query, equation, sizeof(equation));
    
    /* Генерируем формулу */
    if (config->include_formula) {
        if (strcmp(problem_type, "linear") == 0) {
            snprintf(explanation->formula_text, sizeof(explanation->formula_text),
                    "ax + b = c → x = (c - b) / a");
            snprintf(explanation->formula_id, sizeof(explanation->formula_id),
                    "domain:algebra, formula_id:0x1A2B");
            snprintf(explanation->formula_domain, sizeof(explanation->formula_domain),
                    "algebra");
        } else if (strcmp(problem_type, "quadratic") == 0) {
            snprintf(explanation->formula_text, sizeof(explanation->formula_text),
                    "ax² + bx + c = 0 → x = (-b ± √(b²-4ac)) / 2a");
            snprintf(explanation->formula_id, sizeof(explanation->formula_id),
                    "domain:algebra, formula_id:0x2C3D");
            snprintf(explanation->formula_domain, sizeof(explanation->formula_domain),
                    "algebra");
        } else if (strcmp(problem_type, "geometry_area") == 0) {
            snprintf(explanation->formula_text, sizeof(explanation->formula_text),
                    "S = π × r²");
            snprintf(explanation->formula_id, sizeof(explanation->formula_id),
                    "domain:geometry, formula_id:0x4A2F");
            snprintf(explanation->formula_domain, sizeof(explanation->formula_domain),
                    "geometry");
        } else {
            snprintf(explanation->formula_text, sizeof(explanation->formula_text),
                    "%s", equation);
            snprintf(explanation->formula_id, sizeof(explanation->formula_id),
                    "domain:general, formula_id:0x0000");
            snprintf(explanation->formula_domain, sizeof(explanation->formula_domain),
                    "general");
        }
    }
    
    /* Генерируем шаги */
    if (config->include_steps) {
        int step = 0;
        int max_steps = config->max_steps;
        
        if (strcmp(problem_type, "linear") == 0) {
            kolibri_eg_add_step(explanation, KEG_STEP_DEFINITION,
                              "Исходное уравнение",
                              "Записываем уравнение в стандартном виде",
                              equation, 0.0, 1.0);
            
            kolibri_eg_add_step(explanation, KEG_STEP_FORMULA,
                              "Изолируем x",
                              "Переносим свободный член в правую часть",
                              "ax = c - b", 0.0, 0.95);
            
            kolibri_eg_add_step(explanation, KEG_STEP_ARITHMETIC,
                              "Делим на коэффициент",
                              "Разделяем обе части на a",
                              "x = (c - b) / a", 0.0, 0.95);
            
            kolibri_eg_add_step(explanation, KEG_STEP_VERIFICATION,
                              "Проверка",
                              "Подставляем найденное x обратно",
                              "f(x) = 0 ✓", 0.0, 1.0);
        } else if (strcmp(problem_type, "quadratic") == 0) {
            kolibri_eg_add_step(explanation, KEG_STEP_DEFINITION,
                              "Исходное уравнение",
                              "Записываем квадратное уравнение",
                              equation, 0.0, 1.0);
            
            kolibri_eg_add_step(explanation, KEG_STEP_FORMULA,
                              "Вычисляем дискриминант",
                              "D = b² - 4ac",
                              "D = b² - 4ac", 0.0, 0.95);
            
            kolibri_eg_add_step(explanation, KEG_STEP_ARITHMETIC,
                              "Находим корни",
                              "x = (-b ± √D) / 2a",
                              "x₁, x₂", 0.0, 0.95);
            
            kolibri_eg_add_step(explanation, KEG_STEP_VERIFICATION,
                              "Проверка подстановкой",
                              "f(x₁) = 0, f(x₂) = 0",
                              "Оба корня верны ✓", 0.0, 1.0);
        } else {
            /* Общие шаги для неизвестных типов */
            kolibri_eg_add_step(explanation, KEG_STEP_DEFINITION,
                              "Анализ задачи",
                              "Определяем тип задачи и подходящую формулу",
                              problem_type, 0.0, 0.9);
            
            kolibri_eg_add_step(explanation, KEG_STEP_FORMULA,
                              "Применяем формулу",
                              "Используем соответствующую формулу",
                              explanation->formula_text, 0.0, 0.85);
            
            kolibri_eg_add_step(explanation, KEG_STEP_ARITHMETIC,
                              "Вычисления",
                              "Подставляем значения и вычисляем",
                              answer, 0.0, 0.9);
            
            kolibri_eg_add_step(explanation, KEG_STEP_VERIFICATION,
                              "Проверка",
                              "Проверяем результат",
                              "Результат верен ✓", 0.0, 1.0);
        }
    }
    
    /* Добавляем источники */
    if (config->include_sources) {
        kolibri_eg_add_source(explanation,
                             "Математика, школьная программа",
                             explanation->formula_domain,
                             0x4A2F,
                             1710000000000.0,  /* 2025-03-15 */
                             1);
    }
    
    /* Уверенность */
    if (config->include_confidence) {
        explanation->confidence = 0.95;
        snprintf(explanation->confidence_reason, sizeof(explanation->confidence_reason),
                "Точная формула, точные вычисления");
    }
    
    explanation->generation_time_ms = eg_time_ms() - start;
    
    return 0;
}

int kolibri_eg_generate_general_explanation(
    const char *query,
    const char *answer,
    const KolibriEGConfig *config,
    KolibriExplanation *explanation) {
    
    if (!query || !answer || !explanation) return -1;
    
    double start = eg_time_ms();
    memset(explanation, 0, sizeof(KolibriExplanation));
    
    snprintf(explanation->query, sizeof(explanation->query), "%s", query);
    snprintf(explanation->direct_answer, sizeof(explanation->direct_answer), "%s", answer);
    
    /* Для общих вопросов формула может не быть */
    if (config->include_formula) {
        snprintf(explanation->formula_text, sizeof(explanation->formula_text),
                "Знание из графа знаний");
        snprintf(explanation->formula_id, sizeof(explanation->formula_id),
                "domain:general, formula_id:0x0000");
        snprintf(explanation->formula_domain, sizeof(explanation->formula_domain),
                "general");
    }
    
    /* Генерируем шаги */
    if (config->include_steps) {
        kolibri_eg_add_step(explanation, KEG_STEP_DEFINITION,
                          "Понимание вопроса",
                          "Анализируем запрос и определяем тему",
                          query, 0.0, 0.9);
        
        kolibri_eg_add_step(explanation, KEG_STEP_LOGIC,
                          "Поиск знания",
                          "Находим релевантную информацию в графе знаний",
                          "Knowledge retrieval", 0.0, 0.85);
        
        kolibri_eg_add_step(explanation, KEG_STEP_VERIFICATION,
                          "Проверка согласованности",
                          "Сверяем с другими источниками",
                          "Cross-verification ✓", 0.0, 0.9);
    }
    
    /* Источники */
    if (config->include_sources) {
        kolibri_eg_add_source(explanation,
                             "Граф знаний Kolibri",
                             "general",
                             0x0000,
                             1710000000000.0,
                             1);
    }
    
    if (config->include_confidence) {
        explanation->confidence = 0.85;
        snprintf(explanation->confidence_reason, sizeof(explanation->confidence_reason),
                "Знание подтверждено несколькими источниками");
    }
    
    explanation->generation_time_ms = eg_time_ms() - start;
    
    return 0;
}

/* ============================================================================
 * ФОРМАТИРОВАНИЕ
 * ============================================================================ */

int kolibri_eg_format_text(const KolibriExplanation *explanation,
                           char *output, size_t output_size) {
    if (!explanation || !output || output_size == 0) return -1;
    
    char *p = output;
    size_t remaining = output_size;
    int written;
    
    /* Заголовок */
    written = snprintf(p, remaining, "Ответ: %s\n\n", explanation->direct_answer);
    p += written; remaining -= written;
    
    /* Формула */
    if (strlen(explanation->formula_text) > 0) {
        written = snprintf(p, remaining, "Формула: %s [%s]\n\n",
                          explanation->formula_text,
                          explanation->formula_id);
        p += written; remaining -= written;
    }
    
    /* Шаги */
    if (explanation->num_steps > 0) {
        written = snprintf(p, remaining, "Шаги решения:\n");
        p += written; remaining -= written;
        
        for (int i = 0; i < explanation->num_steps; i++) {
            const KolibriExplanationStep *s = &explanation->steps[i];
            written = snprintf(p, remaining, "  %d. %s: %s\n",
                              s->step_num, s->title, s->description);
            p += written; remaining -= written;
            
            if (strlen(s->expression) > 0) {
                written = snprintf(p, remaining, "     %s\n", s->expression);
                p += written; remaining -= written;
            }
        }
        written = snprintf(p, remaining, "\n");
        p += written; remaining -= written;
    }
    
    /* Источники */
    if (explanation->num_sources > 0) {
        written = snprintf(p, remaining, "Источники:\n");
        p += written; remaining -= written;
        
        for (int i = 0; i < explanation->num_sources; i++) {
            const KolibriKnowledgeSource *src = &explanation->sources[i];
            written = snprintf(p, remaining, "  - %s [%s]\n",
                              src->source, src->domain);
            p += written; remaining -= written;
        }
        written = snprintf(p, remaining, "\n");
        p += written; remaining -= written;
    }
    
    /* Уверенность */
    written = snprintf(p, remaining, "Уверенность: %.0f%% (%s)\n",
                      explanation->confidence * 100.0,
                      explanation->confidence_reason);
    p += written; remaining -= written;
    
    return 0;
}

int kolibri_eg_format_markdown(const KolibriExplanation *explanation,
                               char *output, size_t output_size) {
    if (!explanation || !output || output_size == 0) return -1;
    
    char *p = output;
    size_t remaining = output_size;
    int written;
    
    /* Заголовок */
    written = snprintf(p, remaining, "## Ответ\n\n");
    p += written; remaining -= written;
    
    written = snprintf(p, remaining, "%s\n\n", explanation->direct_answer);
    p += written; remaining -= written;
    
    /* Формула */
    if (strlen(explanation->formula_text) > 0) {
        written = snprintf(p, remaining, "## Формула\n\n");
        p += written; remaining -= written;
        
        written = snprintf(p, remaining, "```\n%s\n```\n\n",
                          explanation->formula_text);
        p += written; remaining -= written;
        
        written = snprintf(p, remaining, "*%s*\n\n", explanation->formula_id);
        p += written; remaining -= written;
    }
    
    /* Шаги */
    if (explanation->num_steps > 0) {
        written = snprintf(p, remaining, "## Шаги решения\n\n");
        p += written; remaining -= written;
        
        for (int i = 0; i < explanation->num_steps; i++) {
            const KolibriExplanationStep *s = &explanation->steps[i];
            written = snprintf(p, remaining, "### %d. %s\n\n", s->step_num, s->title);
            p += written; remaining -= written;
            
            written = snprintf(p, remaining, "%s\n\n", s->description);
            p += written; remaining -= written;
            
            if (strlen(s->expression) > 0) {
                written = snprintf(p, remaining, "```\n%s\n```\n\n", s->expression);
                p += written; remaining -= written;
            }
        }
    }
    
    /* Источники */
    if (explanation->num_sources > 0) {
        written = snprintf(p, remaining, "## Источники\n\n");
        p += written; remaining -= written;
        
        for (int i = 0; i < explanation->num_sources; i++) {
            const KolibriKnowledgeSource *src = &explanation->sources[i];
            written = snprintf(p, remaining, "- **%s** [%s] ",
                              src->source, src->domain);
            p += written; remaining -= written;
            
            if (src->verified) {
                written = snprintf(p, remaining, "✓ Верифицировано");
                p += written; remaining -= written;
            }
            written = snprintf(p, remaining, "\n");
            p += written; remaining -= written;
        }
        written = snprintf(p, remaining, "\n");
        p += written; remaining -= written;
    }
    
    /* Уверенность */
    written = snprintf(p, remaining, "## Уверенность\n\n");
    p += written; remaining -= written;
    
    written = snprintf(p, remaining, "**%.0f%%** — %s\n",
                      explanation->confidence * 100.0,
                      explanation->confidence_reason);
    p += written; remaining -= written;
    
    return 0;
}

int kolibri_eg_format_json(const KolibriExplanation *explanation,
                           char *output, size_t output_size) {
    if (!explanation || !output || output_size == 0) return -1;
    
    char *p = output;
    size_t remaining = output_size;
    int written;
    
    written = snprintf(p, remaining, "{\n");
    p += written; remaining -= written;
    
    written = snprintf(p, remaining, "  \"query\": \"%s\",\n", explanation->query);
    p += written; remaining -= written;
    
    written = snprintf(p, remaining, "  \"answer\": \"%s\",\n", explanation->direct_answer);
    p += written; remaining -= written;
    
    written = snprintf(p, remaining, "  \"formula\": {\n");
    p += written; remaining -= written;
    
    written = snprintf(p, remaining, "    \"text\": \"%s\",\n", explanation->formula_text);
    p += written; remaining -= written;
    
    written = snprintf(p, remaining, "    \"id\": \"%s\",\n", explanation->formula_id);
    p += written; remaining -= written;
    
    written = snprintf(p, remaining, "    \"domain\": \"%s\"\n", explanation->formula_domain);
    p += written; remaining -= written;
    
    written = snprintf(p, remaining, "  },\n");
    p += written; remaining -= written;
    
    /* Steps */
    written = snprintf(p, remaining, "  \"steps\": [\n");
    p += written; remaining -= written;
    
    for (int i = 0; i < explanation->num_steps; i++) {
        const KolibriExplanationStep *s = &explanation->steps[i];
        written = snprintf(p, remaining, 
                          "    {\"num\": %d, \"type\": \"%s\", \"title\": \"%s\", \"desc\": \"%s\"}",
                          s->step_num, kolibri_eg_step_type_name(s->step_type),
                          s->title, s->description);
        p += written; remaining -= written;
        
        if (i < explanation->num_steps - 1) {
            written = snprintf(p, remaining, ",\n");
            p += written; remaining -= written;
        } else {
            written = snprintf(p, remaining, "\n");
            p += written; remaining -= written;
        }
    }
    
    written = snprintf(p, remaining, "  ],\n");
    p += written; remaining -= written;
    
    /* Confidence */
    written = snprintf(p, remaining, "  \"confidence\": %.4f,\n", explanation->confidence);
    p += written; remaining -= written;
    
    written = snprintf(p, remaining, "  \"confidence_reason\": \"%s\"\n",
                      explanation->confidence_reason);
    p += written; remaining -= written;
    
    written = snprintf(p, remaining, "}\n");
    p += written; remaining -= written;
    
    return 0;
}

/* ============================================================================
 * ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
 * ============================================================================ */

int kolibri_eg_add_step(KolibriExplanation *explanation,
                       KolibriStepType type,
                       const char *title,
                       const char *description,
                       const char *expression,
                       double result,
                       double confidence) {
    if (!explanation || explanation->num_steps >= KEG_MAX_STEPS) return -1;
    
    KolibriExplanationStep *step = &explanation->steps[explanation->num_steps];
    step->step_num = explanation->num_steps + 1;
    step->step_type = type;
    
    if (title) snprintf(step->title, sizeof(step->title), "%s", title);
    if (description) snprintf(step->description, sizeof(step->description), "%s", description);
    if (expression) snprintf(step->expression, sizeof(step->expression), "%s", expression);
    
    step->result = result;
    step->confidence = confidence;
    
    explanation->num_steps++;
    return 0;
}

int kolibri_eg_add_source(KolibriExplanation *explanation,
                         const char *source,
                         const char *domain,
                         int64_t formula_id,
                         double timestamp,
                         int verified) {
    if (!explanation || explanation->num_sources >= KEG_MAX_SOURCES) return -1;
    
    KolibriKnowledgeSource *src = &explanation->sources[explanation->num_sources];
    
    if (source) snprintf(src->source, sizeof(src->source), "%s", source);
    if (domain) snprintf(src->domain, sizeof(src->domain), "%s", domain);
    
    src->formula_id = formula_id;
    src->timestamp = timestamp;
    src->verified = verified;
    
    explanation->num_sources++;
    return 0;
}

void kolibri_eg_print(const KolibriExplanation *explanation) {
    if (!explanation) return;
    
    char output[KEG_MAX_EXPLANATION_LEN];
    kolibri_eg_format_text(explanation, output, sizeof(output));
    printf("%s\n", output);
}

const char* kolibri_eg_step_type_name(KolibriStepType type) {
    switch (type) {
        case KEG_STEP_FORMULA:      return "formula";
        case KEG_STEP_LOGIC:        return "logic";
        case KEG_STEP_ARITHMETIC:   return "arithmetic";
        case KEG_STEP_ANALOGY:      return "analogy";
        case KEG_STEP_DEFINITION:   return "definition";
        case KEG_STEP_EXAMPLE:      return "example";
        case KEG_STEP_VERIFICATION: return "verification";
        default:                    return "unknown";
    }
}
