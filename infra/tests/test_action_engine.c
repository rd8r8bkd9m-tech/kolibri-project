#include "kolibri/action_engine.h"
#include "kolibri/reasoning_engine.h"
#include "kolibri/tool_registry.h"
#include "kolibri/genome.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Custom unsafe tool */
static int tool_delete_files(const char *params, char *result_out, size_t max_len) {
    snprintf(result_out, max_len, "CRITICAL: Files deleted based on params: %s", params);
    return 0;
}

int main() {
    printf("--- Kolibri AGI Phase 4: Action Provenance with Digital Genome ---\n");

    /* 0. Инициализация Генома */
    KolibriGenome genome;
    const char *genome_path = "test_action_provenance.genome";
    unsigned char key[32] = "secret_provenance_key_2026";
    unlink(genome_path);
    if (kg_open(&genome, genome_path, key, 32) != 0) {
        printf("Failed to open genome for provenance logging.\n");
        return 1;
    }

    KolibriREConfig re_config;
    kolibri_re_init(&re_config);

    /* 1. Добавляем знания */
    kolibri_re_add_fact(&re_config, "Система требует выполнить сложный расчет.", 0.95, "Internal");
    kolibri_re_add_rule(&re_config, "сложный расчет", "Нужно выполнить вычисления", KRE_OP_IMPLIES, 0.9, "Math");

    /* 2. Регистрируем инструменты */
    kolibri_tr_init();
    kolibri_tr_register("delete_all", "Danger Zone", "Удаляет все файлы", "{}", tool_delete_files, 0 /* NOT SAFE */);

    /* 3. Тест 1: Безопасное действие с логированием в Геном */
    printf("\n[Test 1: Action with Provenance Logging]\n");
    KolibriReasoningResult re_result;
    kolibri_re_deductive("Выполни сложный расчет 2+2", &re_config, &re_result);
    
    KolibriActionLoop loop;
    kolibri_ae_init_loop(&loop, "Solve math and log provenance", &genome);
    kolibri_ae_plan_step(&loop, &re_result);

    if (kolibri_ae_execute_current(&loop) == 0) {
        printf("Action Execution: SUCCESS\n");
        printf("Result: %s\n", loop.actions[0].result);
    }

    /* 4. Закрываем и проверяем геном */
    kg_close(&genome);
    printf("\nVerifying Genome Integrity...\n");
    if (kg_verify_file(genome_path, key, 32) == 0) {
        printf("SUCCESS: Genome provenance verified! All actions securely logged.\n");
    } else {
        printf("FAILURE: Genome integrity check failed!\n");
    }

    unlink(genome_path);
    return 0;
}
