/*
 * tool_registry.h
 *
 * Реестр инструментов для Kolibri AGI.
 * Обеспечивает типизацию, описание и безопасный вызов внешних функций.
 */

#ifndef KOLIBRI_TOOL_REGISTRY_H
#define KOLIBRI_TOOL_REGISTRY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KTR_MAX_TOOLS 64
#define KTR_MAX_NAME 64
#define KTR_MAX_DESC 256

typedef int (*KolibriToolFunc)(const char *params, char *result_out, size_t result_max_len);

typedef struct {
    char id[KTR_MAX_NAME];
    char name[KTR_MAX_NAME];
    char description[KTR_MAX_DESC];
    char schema[1024]; /* JSON Schema параметров */
    KolibriToolFunc func;
    int is_safe;       /* Требует ли ручного подтверждения? */
} KolibriTool;

/**
 * Инициализировать реестр
 */
int kolibri_tr_init(void);

/**
 * Зарегистрировать новый инструмент
 */
int kolibri_tr_register(const char *id, const char *name, const char *desc, 
                        const char *schema, KolibriToolFunc func, int is_safe);

/**
 * Вызвать инструмент по ID
 */
int kolibri_tr_execute(const char *id, const char *params, char *result_out, size_t max_len);

/**
 * Получить описание инструмента
 */
const KolibriTool* kolibri_tr_get(const char *id);

/**
 * Список всех доступных инструментов (для планировщика)
 */
int kolibri_tr_list(char *buffer, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_TOOL_REGISTRY_H */
