/*
 * tool_registry.c
 *
 * Реализация реестра инструментов.
 */

#include "kolibri/tool_registry.h"
#include <string.h>
#include <stdio.h>

static KolibriTool g_tools[KTR_MAX_TOOLS];
static int g_tool_count = 0;
static int g_tr_initialized = 0;

/* --- Встроенные инструменты (Стабы для демонстрации) --- */

static int tool_calculator(const char *params, char *result_out, size_t max_len) {
    /* В реальности здесь был бы парсинг JSON и вызов math_engine.c */
    snprintf(result_out, max_len, "Calc result: 42 (from params: %s)", params);
    return 0;
}

static int tool_sys_info(const char *params, char *result_out, size_t max_len) {
    snprintf(result_out, max_len, "Kolibri OS v0.9.0-alpha, Arch: aarch64, Status: Optimal");
    return 0;
}

/* --- API --- */

int kolibri_tr_init(void) {
    if (g_tr_initialized) return 0;
    g_tool_count = 0;
    
    /* Регистрация базовых инструментов */
    kolibri_tr_register("calc", "Calculator", "Выполняет математические расчеты", 
                        "{\"expr\": \"string\"}", tool_calculator, 1);
    
    kolibri_tr_register("sys_info", "System Info", "Возвращает информацию о системе", 
                        "{}", tool_sys_info, 1);
                        
    g_tr_initialized = 1;
    return 0;
}

int kolibri_tr_register(const char *id, const char *name, const char *desc, 
                        const char *schema, KolibriToolFunc func, int is_safe) {
    if (g_tool_count >= KTR_MAX_TOOLS) return -1;
    
    KolibriTool *t = &g_tools[g_tool_count++];
    strncpy(t->id, id, KTR_MAX_NAME - 1);
    strncpy(t->name, name, KTR_MAX_NAME - 1);
    strncpy(t->description, desc, KTR_MAX_DESC - 1);
    if (schema) strncpy(t->schema, schema, 1023);
    t->func = func;
    t->is_safe = is_safe;
    
    return 0;
}

int kolibri_tr_execute(const char *id, const char *params, char *result_out, size_t max_len) {
    for (int i = 0; i < g_tool_count; i++) {
        if (strcmp(g_tools[i].id, id) == 0) {
            return g_tools[i].func(params, result_out, max_len);
        }
    }
    return -1;
}

const KolibriTool* kolibri_tr_get(const char *id) {
    for (int i = 0; i < g_tool_count; i++) {
        if (strcmp(g_tools[i].id, id) == 0) {
            return &g_tools[i];
        }
    }
    return NULL;
}

int kolibri_tr_list(char *buffer, size_t max_len) {
    size_t offset = 0;
    offset += snprintf(buffer + offset, max_len - offset, "Available tools:\n");
    for (int i = 0; i < g_tool_count; i++) {
        offset += snprintf(buffer + offset, max_len - offset, "- %s: %s\n", 
                           g_tools[i].id, g_tools[i].description);
        if (offset >= max_len) break;
    }
    return 0;
}
