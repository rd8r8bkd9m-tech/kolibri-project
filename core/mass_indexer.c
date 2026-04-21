/*
 * mass_indexer.c - Потоковый загрузчик из списка
 * Загружает всё, что нашел скрипт.
 */

#include "kolibri/inference.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int kl_load_knowledge(KolibriInferenceContext *ctx, const char *filename);

int main() {
    printf("=== KOLIBRI SUPER-BRAIN INGESTION ===\n");

    KolibriInferenceContext *ctx = kolibri_inference_create();
    if (!ctx) return 1;

    FILE *list = fopen("knowledge_files.list", "r");
    if (!list) {
        printf("Ошибка: Список файлов не найден. Запустите scripts/find_all_knowledge.sh\n");
        return 1;
    }

    char path[1024];
    int file_count = 0;
    while (fgets(path, sizeof(path), list)) {
        path[strcspn(path, "\r\n")] = 0;
        if (strlen(path) > 0) {
            printf("[%d] Загрузка: %s\n", ++file_count, path);
            kl_load_knowledge(ctx, path);
        }
    }
    fclose(list);

    printf("\n=== ТОТАЛЬНОЕ ПОЗНАНИЕ ЗАВЕРШЕНА ===\n");
    printf("Всего ячеек знаний: %zu\n", ctx->memory->cell_count);

    kolibri_inference_destroy(ctx);
    return 0;
}
