/*
 * Kolibri Multi-level Archive Restorer
 * Восстановление реальных данных из архива
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <stdint.h>

#define MAX_PATH 4096
#define BUFFER_SIZE (1024 * 1024)

static void ensure_directory(const char* path) {
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len == 0) {
        return;
    }
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Использование: %s <архив.kolibri> [выходная_директория]\n", argv[0]);
        return 1;
    }

    const char* archive_path = argv[1];
    const char* output_dir = (argc > 2) ? argv[2] : "/tmp/restored_multilevel";

    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  🔄 ВОССТАНОВЛЕНИЕ ИЗ KOLIBRI MULTI-LEVEL АРХИВА           ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    printf("📂 Архив:           %s\n", archive_path);
    printf("💾 Восстановить в:  %s\n\n", output_dir);

    FILE* archive = fopen(archive_path, "rb");
    if (!archive) {
        printf("❌ Не могу открыть архив: %s\n", archive_path);
        return 1;
    }

    clock_t start = clock();

    char line[256];
    if (!fgets(line, sizeof(line), archive) || strncmp(line, "KOLIBRI_SUPER_ARCHIVE_V1", 24) != 0) {
        printf("❌ Неверный формат архива\n");
        fclose(archive);
        return 1;
    }
    fgets(line, sizeof(line), archive); // LEVELS
    int stored_files = 0;
    size_t original_size = 0;
    fgets(line, sizeof(line), archive);
    sscanf(line, "FILES:%d", &stored_files);
    fgets(line, sizeof(line), archive);
    sscanf(line, "ORIGINAL_SIZE:%zu", &original_size);
    fgets(line, sizeof(line), archive); // FINAL_STORAGE
    fgets(line, sizeof(line), archive); // ---DATA---

    size_t formula_size = 0;
    fread(&formula_size, sizeof(size_t), 1, archive);
    unsigned char* formula = malloc(formula_size);
    if (!formula) {
        printf("❌ Не хватает памяти для формулы\n");
        fclose(archive);
        return 1;
    }
    fread(formula, 1, formula_size, archive);

    int group_count = 0;
    fread(&group_count, sizeof(int), 1, archive);
    for (int i = 0; i < group_count; i++) {
        unsigned int hash;
        size_t group_size;
        fread(&hash, sizeof(unsigned int), 1, archive);
        fread(&group_size, sizeof(size_t), 1, archive);
        if (fseek(archive, (long)group_size, SEEK_CUR) != 0) {
            printf("❌ Ошибка чтения секции ассоциаций\n");
            free(formula);
            fclose(archive);
            return 1;
        }
    }

    uint32_t dir_count32 = 0;
    fread(&dir_count32, sizeof(uint32_t), 1, archive);
    for (uint32_t i = 0; i < dir_count32; i++) {
        uint32_t path_len = 0;
        fread(&path_len, sizeof(uint32_t), 1, archive);
        if (path_len == 0 || path_len >= MAX_PATH) {
            printf("❌ Неверная длина директории\n");
            free(formula);
            fclose(archive);
            return 1;
        }
        char dir_path[MAX_PATH];
        fread(dir_path, 1, path_len, archive);
        dir_path[path_len] = '\0';

        char full_dir[MAX_PATH];
        snprintf(full_dir, MAX_PATH, "%s/%s", output_dir, dir_path);
        ensure_directory(full_dir);
    }

    uint32_t file_count32 = 0;
    fread(&file_count32, sizeof(uint32_t), 1, archive);
    int file_count = (int)file_count32;

    printf("📖 ЭТАП 1: Чтение архива\n\n");
    printf("   ✓ Формула прочитана: %zu байт\n", formula_size);
    printf("   ✓ Файлов для восстановления: %d\n", file_count);
    printf("   ✓ Исходный размер: %.2f MB\n\n", original_size / 1024.0 / 1024.0);

    printf("📊 Формула (HEX): ");
    for (size_t i = 0; i < formula_size && i < 32; i++) {
        printf("%02x ", formula[i]);
        if ((i + 1) % 16 == 0) printf("\n                  ");
    }
    printf("\n\n");

    ensure_directory(output_dir);

    printf("🔄 ЭТАП 2: Восстановление файлов из архива\n\n");

    unsigned char* buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        printf("❌ Не хватает памяти для буфера\n");
        free(formula);
        fclose(archive);
        return 1;
    }

    size_t total_restored = 0;
    char filepath[MAX_PATH];

    for (int i = 0; i < file_count; i++) {
        uint32_t path_len = 0;
        fread(&path_len, sizeof(uint32_t), 1, archive);
        if (path_len == 0 || path_len >= MAX_PATH) {
            printf("❌ Неверная длина пути в архиве\n");
            free(buffer);
            free(formula);
            fclose(archive);
            return 1;
        }
        char rel_path[MAX_PATH];
        fread(rel_path, 1, path_len, archive);
        rel_path[path_len] = '\0';

        uint64_t file_size = 0;
        fread(&file_size, sizeof(uint64_t), 1, archive);

        snprintf(filepath, MAX_PATH, "%s/%s", output_dir, rel_path);
        char* slash = strrchr(filepath, '/');
        if (slash) {
            *slash = '\0';
            ensure_directory(filepath);
            *slash = '/';
        }
        FILE* out = fopen(filepath, "wb");
        if (!out) {
            printf("❌ Не могу создать файл: %s\n", filepath);
            free(buffer);
            free(formula);
            fclose(archive);
            return 1;
        }

        uint64_t remaining = file_size;
        while (remaining > 0) {
            size_t chunk = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : (size_t)remaining;
            size_t read_bytes = fread(buffer, 1, chunk, archive);
            if (read_bytes != chunk) {
                printf("❌ Ошибка чтения данных файла\n");
                fclose(out);
                free(buffer);
                free(formula);
                fclose(archive);
                return 1;
            }
            fwrite(buffer, 1, read_bytes, out);
            remaining -= read_bytes;
            total_restored += read_bytes;
        }
        fclose(out);

        if ((i + 1) % 50 == 0 || i == file_count - 1) {
            printf("   ✓ Восстановлено: %d/%d файлов\r", i + 1, file_count);
            fflush(stdout);
        }
    }
    printf("\n\n");

    clock_t end = clock();
    double time_sec = (double)(end - start) / CLOCKS_PER_SEC;

    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("📊 СТАТИСТИКА ВОССТАНОВЛЕНИЯ\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    printf("   Файлов:           %d\n", file_count);
    printf("   Общий размер:     %.2f MB\n", total_restored / 1024.0 / 1024.0);
    printf("   Директория:       %s\n", output_dir);
    printf("   Время:            %.2f сек\n", time_sec);
    printf("   Скорость:         %.2f MB/сек\n\n", (total_restored / 1024.0 / 1024.0) / time_sec);

    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║  ✅ ВОССТАНОВЛЕНИЕ ЗАВЕРШЕНО                              ║\n");
    printf("║  📁 Файлы: %s\n", output_dir);
    printf("║  📊 Восстановлено: %d файлов (%.2f MB)\n", file_count, total_restored / 1024.0 / 1024.0);
    printf("╚══════════════════════════════════════════════════════════╝\n\n");
    printf("💡 ПРОВЕРКА: diff -qr <оригинал> %s\n", output_dir);

    free(buffer);
    free(formula);
    fclose(archive);
    return 0;
}
