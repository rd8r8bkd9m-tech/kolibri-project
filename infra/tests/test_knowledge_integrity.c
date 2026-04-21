/*
 * test_knowledge_integrity.c
 *
 * Выборочная проверка достоверности базы знаний после тотальной индексации.
 */

#include "kolibri/inference.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

extern int kl_load_knowledge(KolibriInferenceContext *ctx, const char *filename);

int main() {
    printf("=== KOLIBRI KNOWLEDGE INTEGRITY CHECK ===\n");

    KolibriInferenceContext *ctx = kolibri_inference_create();

    /* Загружаем файл ядра еще раз для теста */
    kl_load_knowledge(ctx, "core/decimal.c");

    printf("[STEP] Ищу знания о 'k_digit_stream_push'...\n");

    int found = 0;
    for (size_t i = 0; i < ctx->memory->cell_count; i++) {
        char response[4096];
        lm_emit_to_text(ctx->memory->cells[i].logic, response, sizeof(response));

        if (strstr(response, "k_digit_stream_push")) {
            printf("[OK] Знание найдено!\n");
            printf("[INFO] Хеш ID: %s\n", ctx->memory->cells[i].hash);
            printf("[INFO] Содержимое: \"%.100s...\"\n", response);
            found = 1;
            break;
        }
    }

    if (!found) {
        printf("[FAIL] Критическое знание не найдено в памяти!\n");
        return 1;
    }

    printf("=== ПРОВЕРКА ЦЕЛОСТНОСТИ ЗАВЕРШЕНА ===\n");
    kolibri_inference_destroy(ctx);
    return 0;
}
