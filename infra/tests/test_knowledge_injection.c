/*
 * test_knowledge_injection.c
 *
 * Проверка процесса обучения на реальных текстах.
 * Загружаем Википедию и проверяем цифровую память.
 */

#include "kolibri/inference.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Объявляем функцию из нашего нового модуля */
extern int kl_load_knowledge(KolibriInferenceContext *ctx, const char *filename);

int main() {
    printf("=== KOLIBRI KNOWLEDGE INJECTION TEST ===\n");

    KolibriInferenceContext *ctx = kolibri_inference_create();
    assert(ctx != NULL);

    /* 1. Загружаем знания */
    int cells = kl_load_knowledge(ctx, "docs/wikipedia/AI.md");
    if (cells <= 0) {
        /* Если путь другой, попробуем альтернативный */
        cells = kl_load_knowledge(ctx, "docs/wikipedia/philosophy.md");
    }

    assert(cells > 0);
    printf("[OK] Память заполнена. Ячеек: %d\n", cells);

    /* 2. Проверяем наличие формул в памяти */
    assert(ctx->memory->cell_count > 0);

    /* Берем первую попавшуюся ячейку и смотрим, что там */
    LogicCell *sample = &ctx->memory->cells[0];
    printf("[INFO] Пример ячейки (Hash ID): %s\n", sample->hash);
    printf("[INFO] Тип логики: %d\n", sample->logic->type);

    /* Пытаемся восстановить текст из цифровой формулы */
    char response[4096];
    int len = lm_emit_to_text(sample->logic, response, sizeof(response));

    if (len > 0) {
        printf("[OK] Текст восстановлен из цифр: \"%.100s...\"\n", response);
    } else {
        printf("[FAIL] Ошибка материализации знаний!\n");
        return 1;
    }

    printf("========================================\n");
    printf("ОБУЧЕНИЕ ПРОШЛО УСПЕШНО!\n");

    kolibri_inference_destroy(ctx);
    return 0;
}
