/*
 * test_super_brain_integrity.c
 *
 * Проверка памяти Супер-Kolibri на реальном файле изобретений.
 */

#include "kolibri/inference.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

extern int kl_load_knowledge(KolibriInferenceContext *ctx, const char *filename);

int main() {
    printf("=== SUPER-BRAIN INTEGRITY CHECK ===\n");

    KolibriInferenceContext *ctx = kolibri_inference_create();

    /* Загружаем файл изобретений */
    const char *target_file = "/Users/kolibri/Desktop/КАТАЛОГ_ИЗОБРЕТЕНИЙ.md";
    kl_load_knowledge(ctx, target_file);

    printf("[STEP] Ищу знания об изобретениях...\n");

    int found = 0;
    for (size_t i = 0; i < ctx->memory->cell_count; i++) {
        char response[4096];
        lm_emit_to_text(ctx->memory->cells[i].logic, response, sizeof(response));

        /* Ищем характерные слова */
        if (strstr(response, "изобретение") || strstr(response, "Проект")) {
            printf("[OK] Знание из каталога найдено!\n");
            printf("[INFO] Хеш ID: %s\n", ctx->memory->cells[i].hash);
            printf("[INFO] Содержимое: \"%.150s...\"\n", response);
            found = 1;
            break;
        }
    }

    if (!found) {
        printf("[FAIL] Файл изобретений не найден или не проиндексирован!\n");
        return 1;
    }

    printf("=== ПРОВЕРКА ЗАВЕРШЕНА: ПАМЯТЬ АБСОЛЮТНА ===\n");
    kolibri_inference_destroy(ctx);
    return 0;
}
