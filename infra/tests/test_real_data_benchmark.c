/*
 * test_real_data_benchmark.c
 *
 * Исправленная версия с освобождением памяти.
 */

#include "kolibri/inference.h"
#include "kolibri/decimal.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>

void test_real_file_cognition(const char *filename) {
    printf("[BENCHMARK] Загружаю файл: %s\n", filename);

    FILE *f = fopen(filename, "rb");
    if (!f) { printf("Ошибка: файл не найден!\n"); return; }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *input = malloc(size + 1);
    fread(input, 1, size, f);
    input[size] = '\0';
    fclose(f);

    printf("[STEP] Размер входа: %ld байт\n", size);

    KolibriInferenceContext *ctx = kolibri_inference_create();
    KolibriCognitionResult result;

    clock_t start = clock();
    int rc = kolibri_inference_think(ctx, input, &result);
    double elapsed = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;

    if (rc != 0) {
        printf("[FAILURE] Ошибка при обработке!\n");
        free(input);
        return;
    }

    printf("[SUCCESS] Обработка завершена за %.2f мс.\n", elapsed);
    printf("[STATS] Скорость: %.2f МБ/сек\n", (size / 1024.0 / 1024.0) / (elapsed / 1000.0));

    /* Проверка целостности */
    assert(strncmp(input, result.response, 100) == 0);
    printf("[VERIFIED] Данные корректны!\n");

    /* Освобождаем всё */
    free(result.digit_stream);
    free(result.response);
    free(input);
    kolibri_inference_destroy(ctx);
}

int main() {
    printf("=== KOLIBRI REAL-WORLD DATA BENCHMARK ===\n");
    test_real_file_cognition("core/inference.c");
    printf("==========================================\n");
    return 0;
}
