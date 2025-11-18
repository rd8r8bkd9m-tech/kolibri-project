/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * СОЗДАНИЕ ПОЛНОГО АРХИВА ПРОЕКТА
 * Сохраняет весь проект в .kolibri файл и восстанавливает его
 */

#include "kolibri/generation.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_FILES 200
#define CHUNK_SIZE 490

typedef struct {
    char name[256];
    char path[1024];
    size_t size;
    int chunks;
} FileInfo;

static FileInfo files[MAX_FILES];
static int file_count = 0;
static size_t total_size = 0;

/* Проверяет, нужно ли сжимать файл */
static int should_process_file(const char* filename) {
    size_t len = strlen(filename);
    if (len < 3) return 0;
    
    // .c или .h файлы
    if (len >= 2 && (strcmp(filename + len - 2, ".c") == 0 || 
                     strcmp(filename + len - 2, ".h") == 0)) {
        return 1;
    }
    
    return 0;
}

/* Рекурсивное сканирование директории */
static void scan_directory(const char* dirpath, const char* base_path,
                          KolibriGenerationContext* gen_ctx) {
    DIR* dir = opendir(dirpath);
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);
        
        struct stat st;
        if (stat(fullpath, &st) != 0) continue;
        
        if (S_ISDIR(st.st_mode)) {
            // Рекурсивно
            scan_directory(fullpath, base_path, gen_ctx);
        } else if (S_ISREG(st.st_mode) && should_process_file(entry->d_name)) {
            // Читаем и сжимаем файл
            FILE* f = fopen(fullpath, "r");
            if (!f) continue;
            
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            
            if (fsize <= 0 || fsize > 500000) {
                fclose(f);
                continue;
            }
            
            char* content = malloc(fsize + 1);
            if (!content) {
                fclose(f);
                continue;
            }
            
            size_t read_size = fread(content, 1, fsize, f);
            content[read_size] = '\0';
            fclose(f);
            
            // Разбиваем на чанки и сжимаем
            int chunks = 0;
            for (size_t offset = 0; offset < read_size; offset += CHUNK_SIZE) {
                size_t chunk_len = (offset + CHUNK_SIZE > read_size) ? 
                                  (read_size - offset) : CHUNK_SIZE;
                
                char chunk[512];
                memcpy(chunk, content + offset, chunk_len);
                chunk[chunk_len] = '\0';
                
                KolibriFormula formula;
                memset(&formula, 0, sizeof(formula));
                k_gen_compress_text(gen_ctx, chunk, &formula);
                chunks++;
            }
            
            // Сохраняем информацию о файле
            if (file_count < MAX_FILES && chunks > 0) {
                strncpy(files[file_count].name, entry->d_name, sizeof(files[file_count].name) - 1);
                
                // Сохраняем относительный путь
                const char* rel_path = fullpath + strlen(base_path);
                if (*rel_path == '/') rel_path++;
                strncpy(files[file_count].path, rel_path, sizeof(files[file_count].path) - 1);
                
                files[file_count].size = read_size;
                files[file_count].chunks = chunks;
                file_count++;
                
                total_size += read_size;
            }
            
            free(content);
            
            if (file_count >= MAX_FILES) {
                closedir(dir);
                return;
            }
        }
    }
    
    closedir(dir);
}

int main(int argc, char** argv) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║         СОЗДАНИЕ АРХИВА ПРОЕКТА KOLIBRI OS                   ║\n");
    printf("║         Полное сжатие с возможностью восстановления          ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    const char* base_path = "/Users/kolibri/Documents/os";
    const char* archive_name = "kolibri_os_project.kolibri";
    
    clock_t start = clock();
    
    // Инициализация
    KolibriCorpusContext corpus;
    k_corpus_init(&corpus, 0, 0);
    
    KolibriGenerationContext gen_ctx;
    k_gen_init(&gen_ctx, &corpus, KOLIBRI_GEN_FORMULA);
    
    printf("📂 Сканирование и сжатие проекта...\n\n");
    
    // Сжимаем основные директории
    const char* dirs[] = {
        "/Users/kolibri/Documents/os/backend/src",
        "/Users/kolibri/Documents/os/backend/include",
        "/Users/kolibri/Documents/os/core",
        "/Users/kolibri/Documents/os/kernel",
        NULL
    };
    
    for (int i = 0; dirs[i] != NULL; i++) {
        const char* dir = dirs[i];
        const char* short_name = strrchr(dir, '/');
        if (short_name) short_name++;
        else short_name = dir;
        
        printf("  • %s/\n", short_name);
        scan_directory(dir, base_path, &gen_ctx);
    }
    
    printf("\n✓ Сканирование завершено\n");
    printf("  Файлов: %d\n", file_count);
    printf("  Размер: %.2f КБ\n\n", total_size / 1024.0);
    
    // Финализация
    printf("🔧 Финализация компрессии...\n");
    k_gen_finalize_compression(&gen_ctx, 200);
    
    const KolibriFormula *best = kf_pool_best(gen_ctx.formula_pool);
    assert(best != NULL);
    
    // Вычисляем размеры
    uint8_t formula_digits[256];
    size_t formula_size = kf_formula_digits(best, formula_digits, 256);
    size_t assoc_count = best->association_count;
    
    printf("✓ Компрессия завершена\n");
    printf("  Формула: %zu байт\n", formula_size);
    printf("  Ассоциаций: %zu\n\n", assoc_count);
    
    // ========== СОХРАНЕНИЕ АРХИВА ==========
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("СОХРАНЕНИЕ АРХИВА\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    FILE* archive = fopen(archive_name, "wb");
    if (!archive) {
        printf("❌ ОШИБКА: Не удалось создать архив\n");
        k_gen_free(&gen_ctx);
        k_corpus_free(&corpus);
        return 1;
    }
    
    // Заголовок архива
    fprintf(archive, "KOLIBRI_ARCHIVE_V1\n");
    fprintf(archive, "FILES:%d\n", file_count);
    fprintf(archive, "SIZE:%zu\n", total_size);
    fprintf(archive, "ASSOCIATIONS:%zu\n", assoc_count);
    fprintf(archive, "FORMULA_SIZE:%zu\n", formula_size);
    fprintf(archive, "---\n");
    
    // Список файлов
    for (int i = 0; i < file_count; i++) {
        fprintf(archive, "FILE:%s:%zu\n", files[i].path, files[i].size);
    }
    fprintf(archive, "---\n");
    
    // Формула
    fwrite(formula_digits, 1, formula_size, archive);
    fprintf(archive, "\n---\n");
    
    // Ассоциации (хэш → текст)
    for (size_t i = 0; i < assoc_count; i++) {
        int input_hash = best->associations[i].input_hash;
        const char* answer = best->associations[i].answer;
        
        fwrite(&input_hash, sizeof(int), 1, archive);
        size_t answer_len = strlen(answer);
        fwrite(&answer_len, sizeof(size_t), 1, archive);
        fwrite(answer, 1, answer_len, archive);
    }
    
    fclose(archive);
    
    // Проверяем размер архива
    struct stat archive_stat;
    stat(archive_name, &archive_stat);
    size_t archive_size = archive_stat.st_size;
    
    double compression = (double)total_size / (double)archive_size;
    
    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("✓ Архив создан: %s\n", archive_name);
    printf("  Размер архива:    %zu байт (%.2f КБ)\n", 
           archive_size, archive_size / 1024.0);
    printf("  Исходный размер:  %zu байт (%.2f КБ)\n", 
           total_size, total_size / 1024.0);
    printf("  Компрессия:       %.2fx\n", compression);
    printf("  Время:            %.2f сек\n", elapsed);
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                     ИТОГОВЫЕ РЕЗУЛЬТАТЫ                      ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    printf("📦 Архив готов к использованию!\n\n");
    printf("  Файл:             %s\n", archive_name);
    printf("  Размер:           %.2f КБ\n", archive_size / 1024.0);
    printf("  Файлов внутри:    %d\n", file_count);
    printf("  Общий размер:     %.2f КБ\n", total_size / 1024.0);
    printf("  Коэффициент:      %.2fx\n", compression);
    printf("\n");
    printf("  Содержимое:\n");
    printf("    • Заголовок и индекс:  ~%d байт\n", file_count * 50);
    printf("    • Формула сжатия:      %zu байт\n", formula_size);
    printf("    • База ассоциаций:     ~%zu КБ\n", 
           (assoc_count * 516) / 1024);
    printf("\n");
    
    printf("📂 ТОП-10 файлов в архиве:\n\n");
    
    // Сортировка по размеру
    for (int i = 0; i < file_count - 1 && i < 10; i++) {
        for (int j = i + 1; j < file_count; j++) {
            if (files[j].size > files[i].size) {
                FileInfo temp = files[i];
                files[i] = files[j];
                files[j] = temp;
            }
        }
    }
    
    for (int i = 0; i < 10 && i < file_count; i++) {
        printf("  %2d. %-40s %7.2f КБ\n", 
               i + 1, files[i].name, files[i].size / 1024.0);
    }
    
    printf("\n💡 Для распаковки архива используйте:\n");
    printf("     ./test_project_unarchive %s\n", archive_name);
    
    k_gen_free(&gen_ctx);
    k_corpus_free(&corpus);
    
    printf("\n✓ Архивация завершена успешно!\n\n");
    
    return 0;
}
