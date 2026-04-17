#include "kolibri/action_engine.h"
#include "kolibri/reasoning_engine.h"
#include <stdio.h>

int main() {
    printf("--- Kolibri AGI Phase 4: Reasoning-guided Action Loop Test ---\n");

    KolibriREConfig re_config;
    kolibri_re_init(&re_config);

    /* 1. Добавляем знания */
    kolibri_re_add_fact(&re_config, "Искусственный интеллект требует верификации ответов.", 0.95, "Internal");
    kolibri_re_add_rule(&re_config, "Искусственный интеллект", "Необходим поиск определений", KRE_OP_IMPLIES, 0.9, "AI");

    /* 2. Выполняем рассуждение */
    KolibriReasoningResult re_result;
    const char *query = "Что такое Искусственный интеллект?";
    kolibri_re_deductive(query, &re_config, &re_result);
    
    printf("Reasoning Answer: %s\n", re_result.answer);

    /* 3. Запускаем Action Loop на основе рассуждения */
    KolibriActionLoop action_loop;
    kolibri_ae_init_loop(&action_loop, "Define and verify AI");
    
    printf("Planning action based on reasoning...\n");
    kolibri_ae_plan_step(&action_loop, &re_result);

    printf("Executing action loop...\n");
    if (kolibri_ae_run_to_goal(&action_loop, 5) == 0) {
        printf("\nSUCCESS: Action Loop reached goal.\n");
        printf("Final Action Result: %s\n", action_loop.actions[0].result);
    } else {
        printf("\nFAILURE: Action Loop failed.\n");
    }

    return 0;
}
