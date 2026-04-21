/*
 * project_mapper.c - Инвентаризация разума Kolibri
 * Выводит список всех проектов, загруженных в ядро.
 */

#include "kolibri/inference.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void map_projects() {
    printf("=== KOLIBRI COGNITIVE PROJECT MAP ===\n\n");

    FILE *list = fopen("knowledge_files.list", "r");
    if (!list) return;

    char path[1024];
    char projects[100][256]; /* Максимум 100 проектов */
    int counts[100] = {0};
    int total_projects = 0;

    while (fgets(path, sizeof(path), list)) {
        path[strcspn(path, "\r\n")] = 0;

        /* Вычленяем название проекта из пути /Users/kolibri/Desktop/PROJECT_NAME/... */
        char *p = strstr(path, "Desktop/");
        if (!p) p = strstr(path, "Documents/");
        if (!p) continue;

        p = strchr(p, '/') + 1;
        char *end = strchr(p, '/');
        if (!end) continue;

        char project_name[256];
        size_t len = end - p;
        strncpy(project_name, p, len);
        project_name[len] = '\0';

        /* Ищем в нашем списке */
        int found = -1;
        for (int i = 0; i < total_projects; i++) {
            if (strcmp(projects[i], project_name) == 0) {
                found = i;
                break;
            }
        }

        if (found >= 0) {
            counts[found]++;
        } else if (total_projects < 100) {
            strcpy(projects[total_projects], project_name);
            counts[total_projects] = 1;
            total_projects++;
        }
    }
    fclose(list);

    /* Вывод таблицы */
    printf("%-25s | %-15s\n", "НАЗВАНИЕ ПРОЕКТА", "ФАЙЛОВ ЗНАНИЙ");
    printf("-------------------------------------------\n");
    int grand_total = 0;
    for (int i = 0; i < total_projects; i++) {
        printf("%-25s | %-15d\n", projects[i], counts[i]);
        grand_total += counts[i];
    }
    printf("-------------------------------------------\n");
    printf("ИТОГО: %d проектов, %d файлов в ядре.\n", total_projects, grand_total);
}

int main() {
    map_projects();
    return 0;
}
