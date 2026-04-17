#include "kolibri/action_engine.h"
#include "kolibri/reasoning_engine.h"
#include "kolibri/tool_registry.h"
#include <stdio.h>
#include <string.h>

/* Custom unsafe tool */
static int tool_delete_files(const char *params, char *result_out, size_t max_len) {
    snprintf(result_out, max_len, "CRITICAL: Files deleted based on params: %s", params);
    return 0;
}

int main() {
    printf("--- Kolibri AGI Phase 4: Tool Registry & Safety Sandbox Test ---\n");

    KolibriREConfig re_config;
    kolibri_re_init(&re_config);

    /* 1. Добавляем знания про расчеты */
    kolibri_re_add_fact(&re_config, "Система требует выполнить сложный расчет.", 0.95, "Internal");
    kolibri_re_add_rule(&re_config, "сложный расчет", "Нужно выполнить вычисления", KRE_OP_IMPLIES, 0.9, "Math");

    /* 2. Регистрируем ОПАСНЫЙ инструмент */
    kolibri_tr_init();
    kolibri_tr_register("delete_all", "Danger Zone", "Удаляет все файлы", "{}", tool_delete_files, 0 /* NOT SAFE */);

    /* 3. Тест 1: Безопасное действие (Расчет) */
    printf("\n[Test 1: Safe Calculation]\n");
    KolibriReasoningResult re_result;
    kolibri_re_deductive("Выполни сложный расчет 2+2", &re_config, &re_result);
    
    KolibriActionLoop loop;
    kolibri_ae_init_loop(&loop, "Solve math problem");
    kolibri_ae_plan_step(&loop, &re_result);

    if (kolibri_ae_execute_current(&loop) == 0) {
        printf("Action 1 Execution: SUCCESS (Safe tool)\n");
        printf("Result: %s\n", loop.actions[0].result);
    }

    /* 4. Тест 2: Опасное действие (Блокировка) */
    printf("\n[Test 2: Unsafe Action Blocking]\n");
    KolibriActionLoop danger_loop;
    kolibri_ae_init_loop(&danger_loop, "Maintenance");
    
    /* Ручное планирование опасного действия */
    KolibriAction *act = &danger_loop.actions[danger_loop.num_actions++];
    strcpy(act->name, "Wipe data");
    strcpy(act->tool_id, "delete_all");
    act->type = KAE_ACTION_TOOL_USE;
    
    int ret = kolibri_ae_execute_current(&danger_loop);
    if (ret == 1 && danger_loop.actions[0].status == KAE_STATUS_BLOCKED) {
        printf("SUCCESS: Action correctly BLOCKED by Safety Sandbox.\n");
    } else {
        printf("FAILURE: Dangerous action was NOT blocked!\n");
    }

    return 0;
}
