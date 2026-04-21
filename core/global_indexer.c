/*
 * global_indexer.c - Глобальный поглотитель опыта
 * Сканирует все проекты на Desktop и загружает их в Kolibri.
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/inference.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

extern int kl_load_knowledge(KolibriInferenceContext *ctx, const char *filename);

void scan_all_projects(KolibriInferenceContext *ctx, const char *base_path) {
    struct dirent *entry;
    DIR *dp = opendir(base_path);

    if (!dp) return;

    while ((entry = readdir(dp))) {
        if (entry->d_name[0] == '.') continue;

        /* Игнорируем тяжелые папки с зависимостями */
        if (strcmp(entry->d_name, "node_modules") == 0 ||
            strcmp(entry->d_name, "dist") == 0 ||
            strcmp(entry->d_name, "build") == 0 ||
            strcmp(entry->d_name, "venv") == 0) continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", base_path, entry->d_name);

        struct stat statbuf;
        if (stat(path, &statbuf) == -1) continue;

        if (S_ISDIR(statbuf.st_mode)) {
            scan_all_projects(ctx, path);
        } else {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 ||
                        strcmp(ext, ".ts") == 0 || strcmp(ext, ".tsx") == 0 ||
                        strcmp(ext, ".py") == 0 || strcmp(ext, ".md") == 0 ||
                        strcmp(ext, ".js") == 0 || strcmp(ext, ".rs") == 0)) {
                kl_load_knowledge(ctx, path);
            }
        }
    }
    closedir(dp);
}

int main() {
    printf("=== KOLIBRI GLOBAL PROJECTS INDEXING ===\n");

    KolibriInferenceContext *ctx = kolibri_inference_create();

    /* Сканируем весь Desktop - там живут твои проекты */
    printf("[SCAN] Начало сканирования: /Users/kolibri/Desktop\n");
    scan_all_projects(ctx, "/Users/kolibri/Desktop");

    printf("\n=== ГЛОБАЛЬНАЯ ИНДЕКСАЦИЯ ЗАВЕРШЕНА ===\n");
    printf("Финальный объем базы знаний: %zu ячеек.\n", ctx->memory->cell_count);
    printf("Kolibri теперь знает ВСЕ твои проекты.\n");

    kolibri_inference_destroy(ctx);
    return 0;
}
