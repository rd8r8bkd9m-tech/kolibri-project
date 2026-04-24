/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Code Generation Module - Специализированная генерация кода
 * Генерация программного кода через семантические паттерны и формулы
 */

#include "kolibri/generation.h"
#include "kolibri/semantic.h"
#include "kolibri/formula.h"
#include "kolibri/context.h"
#include "kolibri/code_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== КОНФИГУРАЦИЯ ГЕНЕРАЦИИ КОДА ========== */

#define KOLIBRI_CODEGEN_MAX_LINE_LEN 256
#define KOLIBRI_CODEGEN_MAX_INDENT_LEVEL 16
#define KOLIBRI_CODEGEN_TEMPLATE_CACHE_SIZE 64

/* Локальные определения для использования в этом файле */
#define KOLIBRI_CODE_C           0
#define KOLIBRI_CODE_PYTHON      1
#define KOLIBRI_CODE_KOLIBRI_SCRIPT 2
#define KOLIBRI_CODE_JSON        3
#define KOLIBRI_CODE_FORMULA     4
#define KOLIBRI_CODE_AUTO        5

/* Шаблоны кода для различных конструкций - храним как opaque данные */
typedef struct {
    char data[512];  /* Сырые данные шаблона */
} CodeTemplateOpaque;

/* Внутренняя структура контекста для реализации */
typedef struct {
    KolibriGenerationContext gen_ctx;
    int language;
    int current_indent;
    char line_buffer[KOLIBRI_CODEGEN_MAX_LINE_LEN];
    size_t lines_generated;
    
    /* Кэш шаблонов - упрощённая версия */
    CodeTemplateOpaque template_cache[KOLIBRI_CODEGEN_TEMPLATE_CACHE_SIZE];
    size_t template_count;
    
    /* Статистика */
    size_t functions_generated;
    size_t variables_generated;
    size_t control_structures;
    double avg_function_complexity;
} CodeGenInternalContext;

/* ========== ШАБЛОНЫ КОДА ========== */

static const CodeTemplate c_templates[] = {
    {
        .name = "function_def",
        .pattern = "%s %s(%s) {\n%s\n}",
        .lang = KOLIBRI_CODE_C,
        .indent_delta = 1,
        .keywords = {"int", "void", "char", "float", "double", NULL}
    },
    {
        .name = "if_statement",
        .pattern = "if (%s) {\n%s\n}",
        .lang = KOLIBRI_CODE_C,
        .indent_delta = 1,
        .keywords = {"if", "else", NULL}
    },
    {
        .name = "for_loop",
        .pattern = "for (%s; %s; %s) {\n%s\n}",
        .lang = KOLIBRI_CODE_C,
        .indent_delta = 1,
        .keywords = {"for", "while", "do", NULL}
    },
    {
        .name = "variable_decl",
        .pattern = "%s %s = %s;",
        .lang = KOLIBRI_CODE_C,
        .indent_delta = 0,
        .keywords = {"int", "char", "float", "double", "struct", NULL}
    },
    {
        .name = "struct_def",
        .pattern = "typedef struct {\n%s\n} %s;",
        .lang = KOLIBRI_CODE_C,
        .indent_delta = 1,
        .keywords = {"typedef", "struct", NULL}
    }
};

static const CodeTemplate python_templates[] = {
    {
        .name = "function_def",
        .pattern = "def %s(%s):\n%s",
        .lang = KOLIBRI_CODE_PYTHON,
        .indent_delta = 1,
        .keywords = {"def", "return", "lambda", NULL}
    },
    {
        .name = "if_statement",
        .pattern = "if %s:\n%s",
        .lang = KOLIBRI_CODE_PYTHON,
        .indent_delta = 1,
        .keywords = {"if", "elif", "else", NULL}
    },
    {
        .name = "for_loop",
        .pattern = "for %s in %s:\n%s",
        .lang = KOLIBRI_CODE_PYTHON,
        .indent_delta = 1,
        .keywords = {"for", "while", "in", NULL}
    },
    {
        .name = "variable_decl",
        .pattern = "%s = %s",
        .lang = KOLIBRI_CODE_PYTHON,
        .indent_delta = 0,
        .keywords = {NULL}
    },
    {
        .name = "class_def",
        .pattern = "class %s:\n%s",
        .lang = KOLIBRI_CODE_PYTHON,
        .indent_delta = 1,
        .keywords = {"class", "self", "__init__", NULL}
    }
};

/* ========== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ========== */

static void indent_string(char *buffer, size_t buffer_size, int indent_level) {
    if (!buffer || buffer_size == 0) return;
    
    size_t pos = 0;
    for (int i = 0; i < indent_level && pos < buffer_size - 1; i++) {
        buffer[pos++] = ' ';
        buffer[pos++] = ' ';
    }
    buffer[pos] = '\0';
}

static int detect_language_from_prompt(const char *prompt) {
    if (!prompt) return KOLIBRI_CODE_C;
    
    if (strstr(prompt, "def ") || strstr(prompt, "import ") || 
        strstr(prompt, "class ") || strstr(prompt, "print(")) {
        return KOLIBRI_CODE_PYTHON;
    }
    if (strstr(prompt, "#include") || strstr(prompt, "int main") ||
        strstr(prompt, "typedef") || strstr(prompt, "struct ")) {
        return KOLIBRI_CODE_C;
    }
    if (strstr(prompt, "ASK ") || strstr(prompt, "LEARN ") ||
        strstr(prompt, ".ks")) {
        return KOLIBRI_CODE_KOLIBRI_SCRIPT;
    }
    if (strstr(prompt, "{") && strstr(prompt, "\"")) {
        return KOLIBRI_CODE_JSON;
    }
    
    return KOLIBRI_CODE_C; /* По умолчанию */
}

static const CodeTemplate* find_template(const CodeTemplate* templates, 
                                         size_t template_count,
                                         const char* name) {
    for (size_t i = 0; i < template_count; i++) {
        if (strcmp(templates[i].name, name) == 0) {
            return &templates[i];
        }
    }
    return NULL;
}

/* ========== ОСНОВНОЙ API ========== */

/**
 * Инициализация контекста генерации кода
 */
int k_codegen_init(KolibriCodeGenContext *ctx,
                   KolibriCorpusContext *corpus,
                   KolibriCodeLanguage language) {
    if (!ctx || !corpus) return -1;
    
    memset(ctx, 0, sizeof(*ctx));
    ctx->language = language;
    ctx->current_indent = 0;
    ctx->template_count = 0;
    
    /* Инициализация базового контекста генерации */
    if (k_gen_init(&ctx->gen_ctx, corpus, KOLIBRI_GEN_FORMULA) != 0) {
        return -1;
    }
    
    /* Загрузка шаблонов в кэш */
    const CodeTemplate *src_templates = NULL;
    size_t src_count = 0;
    
    switch (language) {
        case KOLIBRI_CODE_PYTHON:
            src_templates = python_templates;
            src_count = sizeof(python_templates) / sizeof(python_templates[0]);
            break;
        case KOLIBRI_CODE_C:
        default:
            src_templates = c_templates;
            src_count = sizeof(c_templates) / sizeof(c_templates[0]);
            break;
    }
    
    for (size_t i = 0; i < src_count && ctx->template_count < KOLIBRI_CODEGEN_TEMPLATE_CACHE_SIZE; i++) {
        ctx->template_cache[ctx->template_count++] = src_templates[i];
    }
    
    return 0;
}

/**
 * Освобождение ресурсов
 */
void k_codegen_free(KolibriCodeGenContext *ctx) {
    if (!ctx) return;
    k_gen_free(&ctx->gen_ctx);
    memset(ctx, 0, sizeof(*ctx));
}

/**
 * Установка уровня отступа
 */
void k_codegen_set_indent(KolibriCodeGenContext *ctx, int indent) {
    if (!ctx) return;
    if (indent < 0) indent = 0;
    if (indent > KOLIBRI_CODEGEN_MAX_INDENT_LEVEL) indent = KOLIBRI_CODEGEN_MAX_INDENT_LEVEL;
    ctx->current_indent = indent;
}

/**
 * Увеличение отступа
 */
void k_codegen_increase_indent(KolibriCodeGenContext *ctx) {
    if (!ctx) return;
    ctx->current_indent++;
    if (ctx->current_indent > KOLIBRI_CODEGEN_MAX_INDENT_LEVEL) {
        ctx->current_indent = KOLIBRI_CODEGEN_MAX_INDENT_LEVEL;
    }
}

/**
 * Уменьшение отступа
 */
void k_codegen_decrease_indent(KolibriCodeGenContext *ctx) {
    if (!ctx) return;
    ctx->current_indent--;
    if (ctx->current_indent < 0) ctx->current_indent = 0;
}

/**
 * Генерация строки кода с подстановкой параметров
 */
int k_codegen_generate_line(KolibriCodeGenContext *ctx,
                           const char *template_name,
                           const char **params,
                           size_t param_count,
                           char *output,
                           size_t output_size) {
    if (!ctx || !template_name || !output || output_size == 0) return -1;
    
    /* Поиск шаблона */
    const CodeTemplate *tmpl = find_template(ctx->template_cache, 
                                             ctx->template_count,
                                             template_name);
    if (!tmpl) {
        /* Если шаблон не найден, используем семантическую генерацию */
        char prompt[256];
        snprintf(prompt, sizeof(prompt), "generate %s code: %s", 
                 ctx->language == KOLIBRI_CODE_PYTHON ? "python" : "c",
                 template_name);
        
        return k_gen_next_token(&ctx->gen_ctx, output, output_size);
    }
    
    /* Формирование строки с учётом отступа */
    char indent_str[64];
    indent_string(indent_str, sizeof(indent_str), ctx->current_indent);
    
    /* Простая подстановка параметров (поддержка до 4 параметров) */
    char formatted[512];
    switch (param_count) {
        case 0:
            snprintf(formatted, sizeof(formatted), tmpl->pattern);
            break;
        case 1:
            snprintf(formatted, sizeof(formatted), tmpl->pattern, params[0]);
            break;
        case 2:
            snprintf(formatted, sizeof(formatted), tmpl->pattern, params[0], params[1]);
            break;
        case 3:
            snprintf(formatted, sizeof(formatted), tmpl->pattern, params[0], params[1], params[2]);
            break;
        case 4:
            snprintf(formatted, sizeof(formatted), tmpl->pattern, params[0], params[1], params[2], params[3]);
            break;
        default:
            /* Для большего количества параметров нужна более сложная логика */
            strncpy(formatted, tmpl->pattern, sizeof(formatted) - 1);
            formatted[sizeof(formatted) - 1] = '\0';
            break;
    }
    
    snprintf(output, output_size, "%s%s", indent_str, formatted);
    ctx->lines_generated++;
    
    /* Обновление отступа если нужно */
    ctx->current_indent += tmpl->indent_delta;
    
    return 0;
}

/**
 * Генерация функции
 */
int k_codegen_generate_function(KolibriCodeGenContext *ctx,
                               const char *return_type,
                               const char *func_name,
                               const char *params,
                               const char *body,
                               char *output,
                               size_t output_size) {
    if (!ctx || !func_name || !output || output_size == 0) return -1;
    
    const char *template_name = "function_def";
    const char *params_arr[4];
    size_t param_count = 0;
    
    if (ctx->language == KOLIBRI_CODE_PYTHON) {
        params_arr[0] = func_name;
        params_arr[1] = params ? params : "";
        params_arr[2] = body ? body : "    pass";
        param_count = 3;
    } else {
        /* C-стиль */
        char full_pattern[512];
        if (return_type) {
            snprintf(full_pattern, sizeof(full_pattern), "%s %s(%s) {\n%s\n}",
                     return_type, func_name, params ? params : "void",
                     body ? body : "    /* TODO */");
        } else {
            snprintf(full_pattern, sizeof(full_pattern), "void %s(%s) {\n%s\n}",
                     func_name, params ? params : "void",
                     body ? body : "    /* TODO */");
        }
        
        char indent_str[64];
        indent_string(indent_str, sizeof(indent_str), ctx->current_indent);
        snprintf(output, output_size, "%s%s", indent_str, full_pattern);
        ctx->lines_generated++;
        ctx->functions_generated++;
        return 0;
    }
    
    int result = k_codegen_generate_line(ctx, template_name, params_arr, param_count, output, output_size);
    if (result == 0) {
        ctx->functions_generated++;
    }
    
    return result;
}

/**
 * Генерация переменной
 */
int k_codegen_generate_variable(KolibriCodeGenContext *ctx,
                               const char *var_type,
                               const char *var_name,
                               const char *initial_value,
                               char *output,
                               size_t output_size) {
    if (!ctx || !var_name || !output || output_size == 0) return -1;
    
    const char *params[3];
    size_t param_count = 0;
    
    if (ctx->language == KOLIBRI_CODE_PYTHON) {
        params[0] = var_name;
        params[1] = initial_value ? initial_value : "None";
        param_count = 2;
    } else {
        params[0] = var_type ? var_type : "int";
        params[1] = var_name;
        params[2] = initial_value ? initial_value : "0";
        param_count = 3;
    }
    
    int result = k_codegen_generate_line(ctx, "variable_decl", params, param_count, output, output_size);
    if (result == 0) {
        ctx->variables_generated++;
    }
    
    return result;
}

/**
 * Генерация цикла
 */
int k_codegen_generate_loop(KolibriCodeGenContext *ctx,
                           const char *loop_type,
                           const char *init,
                           const char *condition,
                           const char *increment,
                           const char *body,
                           char *output,
                           size_t output_size) {
    if (!ctx || !loop_type || !output || output_size == 0) return -1;
    
    const char *params[4];
    size_t param_count = 0;
    
    if (strcmp(loop_type, "for") == 0 && ctx->language != KOLIBRI_CODE_PYTHON) {
        params[0] = init ? init : "int i = 0";
        params[1] = condition ? condition : "i < n";
        params[2] = increment ? increment : "i++";
        params[3] = body ? body : "    /* loop body */";
        param_count = 4;
    } else if (strcmp(loop_type, "for") == 0 && ctx->language == KOLIBRI_CODE_PYTHON) {
        params[0] = init ? init : "i";
        params[1] = condition ? condition : "range(n)";
        params[2] = body ? body : "    pass";
        param_count = 3;
    } else {
        /* while loop */
        char while_pattern[512];
        if (ctx->language == KOLIBRI_CODE_PYTHON) {
            snprintf(while_pattern, sizeof(while_pattern), "while %s:\n%s",
                     condition ? condition : "True",
                     body ? body : "    pass");
        } else {
            snprintf(while_pattern, sizeof(while_pattern), "while (%s) {\n%s\n}",
                     condition ? condition : "1",
                     body ? body : "    /* loop body */");
        }
        
        char indent_str[64];
        indent_string(indent_str, sizeof(indent_str), ctx->current_indent);
        snprintf(output, output_size, "%s%s", indent_str, while_pattern);
        ctx->lines_generated++;
        ctx->control_structures++;
        return 0;
    }
    
    int result = k_codegen_generate_line(ctx, "for_loop", params, param_count, output, output_size);
    if (result == 0) {
        ctx->control_structures++;
    }
    
    return result;
}

/**
 * Генерация условного оператора
 */
int k_codegen_generate_if(KolibriCodeGenContext *ctx,
                         const char *condition,
                         const char *then_body,
                         const char *else_body,
                         char *output,
                         size_t output_size) {
    if (!ctx || !condition || !output || output_size == 0) return -1;
    
    const char *params[2];
    params[0] = condition;
    params[1] = then_body ? then_body : (ctx->language == KOLIBRI_CODE_PYTHON ? "    pass" : "    /* TODO */");
    
    int result = k_codegen_generate_line(ctx, "if_statement", params, 2, output, output_size);
    if (result == 0 && else_body) {
        /* Добавляем else часть */
        char indent_str[64];
        indent_string(indent_str, sizeof(indent_str), ctx->current_indent - 1);
        
        size_t current_len = strlen(output);
        if (ctx->language == KOLIBRI_CODE_PYTHON) {
            snprintf(output + current_len, output_size - current_len,
                     "\n%selse:\n%s", indent_str, else_body);
        } else {
            snprintf(output + current_len, output_size - current_len,
                     " else {\n%s\n%s}", else_body, indent_str);
        }
    }
    
    if (result == 0) {
        ctx->control_structures++;
    }
    
    return result;
}

/**
 * Генерация кода из промпта с авто-определением языка
 */
int k_codegen_generate_from_prompt(KolibriCodeGenContext *ctx,
                                  const char *prompt,
                                  char *output,
                                  size_t output_size) {
    if (!ctx || !prompt || !output || output_size == 0) return -1;
    
    /* Авто-определение языка если нужно */
    if (ctx->language == KOLIBRI_CODE_AUTO) {
        ctx->language = detect_language_from_prompt(prompt);
    }
    
    /* Используем семантический движок для генерации */
    KolibriSemanticPattern pattern;
    k_semantic_pattern_init(&pattern);
    
    /* Токенизация промпта */
    strncpy(pattern.word, prompt, sizeof(pattern.word) - 1);
    pattern.context_weight = 0.8;
    
    /* Генерация через формулы */
    KolibriFormula formula;
    memset(&formula, 0, sizeof(formula));
    
    double compression = k_gen_compress_pattern(&ctx->gen_ctx, &pattern, &formula);
    
    if (compression > 1.0) {
        /* Успешная компрессия - используем формулу для генерации */
        KolibriSemanticPattern generated;
        k_semantic_pattern_init(&generated);
        if (k_gen_decompress_pattern(&ctx->gen_ctx, &formula, &generated) == 0) {
            /* Формируем выход */
            strncpy(output, generated.word, output_size - 1);
            output[output_size - 1] = '\0';
            ctx->lines_generated++;
            return 0;
        }
    }
    
    /* Fallback на прямую генерацию */
    return k_gen_next_token(&ctx->gen_ctx, output, output_size);
}

/**
 * Генерация полного файла кода
 */
int k_codegen_generate_file(KolibriCodeGenContext *ctx,
                           const char *description,
                           const char **requirements,
                           size_t req_count,
                           char *output,
                           size_t output_size) {
    if (!ctx || !description || !output || output_size == 0) return -1;
    
    size_t total_written = 0;
    
    /* Заголовок файла */
    if (ctx->language == KOLIBRI_CODE_C) {
        total_written += snprintf(output + total_written, output_size - total_written,
                                  "/*\n * Auto-generated code\n * Description: %s\n */\n\n",
                                  description);
        
        /* Стандартные include */
        total_written += snprintf(output + total_written, output_size - total_written,
                                  "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\n");
    } else if (ctx->language == KOLIBRI_CODE_PYTHON) {
        total_written += snprintf(output + total_written, output_size - total_written,
                                  "#!/usr/bin/env python3\n\"\"\"\nAuto-generated code\nDescription: %s\n\"\"\"\n\n",
                                  description);
    }
    
    /* Генерация кода по требованиям */
    for (size_t i = 0; i < req_count && total_written < output_size - 1; i++) {
        char generated_line[512];
        if (k_codegen_generate_from_prompt(ctx, requirements[i], generated_line, sizeof(generated_line)) == 0) {
            total_written += snprintf(output + total_written, output_size - total_written,
                                      "%s\n", generated_line);
        }
    }
    
    return (total_written > 0) ? 0 : -1;
}

/**
 * Получение статистики
 */
void k_codegen_get_stats(const KolibriCodeGenContext *ctx,
                        size_t *lines_generated,
                        size_t *functions_generated,
                        size_t *variables_generated,
                        size_t *control_structures) {
    if (!ctx) return;
    
    if (lines_generated) *lines_generated = ctx->lines_generated;
    if (functions_generated) *functions_generated = ctx->functions_generated;
    if (variables_generated) *variables_generated = ctx->variables_generated;
    if (control_structures) *control_structures = ctx->control_structures;
}

/**
 * Вывод статистики
 */
void k_codegen_print_stats(const KolibriCodeGenContext *ctx) {
    if (!ctx) return;
    
    printf("\n=== Code Generation Statistics ===\n");
    printf("Language: %s\n", 
           ctx->language == KOLIBRI_CODE_PYTHON ? "Python" :
           ctx->language == KOLIBRI_CODE_C ? "C" :
           ctx->language == KOLIBRI_CODE_KOLIBRI_SCRIPT ? "Kolibri Script" :
           ctx->language == KOLIBRI_CODE_JSON ? "JSON" : "Auto");
    printf("Lines generated: %zu\n", ctx->lines_generated);
    printf("Functions: %zu\n", ctx->functions_generated);
    printf("Variables: %zu\n", ctx->variables_generated);
    printf("Control structures: %zu\n", ctx->control_structures);
    printf("Templates cached: %zu/%d\n", ctx->template_count, KOLIBRI_CODEGEN_TEMPLATE_CACHE_SIZE);
    
    k_gen_print_stats(&ctx->gen_ctx);
}

#ifdef __cplusplus
}
#endif
