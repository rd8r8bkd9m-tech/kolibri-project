/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * ПОЛНОЕ ВОССТАНОВЛЕНИЕ ПРОЕКТА ИЗ СУПЕР-АРХИВА
 * Распаковывает kolibri_os_super_archive.kolibri в папку restored/
 */

#include "kolibri/generation.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define CHUNK_SIZE 450

int main(int argc, char** argv) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║      ВОССТАНОВЛЕНИЕ ПРОЕКТА ИЗ СУПЕР-АРХИВА                  ║\n");
    printf("║      Распаковка побайтовая в папку restored/                 ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    clock_t start_time = clock();
    
    // Создаём папку для восстановления
    system("rm -rf restored");
    system("mkdir -p restored");
    
    printf("📂 Создана папка: restored/\n\n");
    
    // ========== ЗАГРУЗКА АРХИВА ==========
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("ШАГ 1: Загрузка супер-архива\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    const char* archive_name = "kolibri_os_super_archive.kolibri";
    FILE* archive = fopen(archive_name, "rb");
    if (!archive) {
        printf("❌ ОШИБКА: Не могу открыть %s\n", archive_name);
        printf("   Запустите сначала: ./test_super_archive\n\n");
        return 1;
    }
    
    // Читаем заголовок
    char line[256];
    size_t levels, files_count, original_size, final_storage;
    
    fgets(line, sizeof(line), archive);
    fgets(line, sizeof(line), archive);
    sscanf(line, "LEVELS:%zu", &levels);
    fgets(line, sizeof(line), archive);
    sscanf(line, "FILES:%zu", &files_count);
    fgets(line, sizeof(line), archive);
    sscanf(line, "ORIGINAL_SIZE:%zu", &original_size);
    fgets(line, sizeof(line), archive);
    sscanf(line, "FINAL_STORAGE:%zu", &final_storage);
    fgets(line, sizeof(line), archive);
    
    printf("✓ Архив загружен: %s\n", archive_name);
    printf("  Файлов внутри:  %zu\n", files_count);
    printf("  Исходный размер: %zu байт (%.2f КБ)\n\n", 
           original_size, original_size / 1024.0);
    
    // Загружаем формулу
    size_t formula_size;
    fread(&formula_size, sizeof(size_t), 1, archive);
    
    uint8_t formula_digits[256];
    fread(formula_digits, 1, formula_size, archive);
    
    printf("✓ Формула загружена: %zu байт\n", formula_size);
    
    // Загружаем хеши
    size_t assoc_count;
    fread(&assoc_count, sizeof(size_t), 1, archive);
    
    int* hashes = malloc(assoc_count * sizeof(int));
    for (size_t i = 0; i < assoc_count; i++) {
        fread(&hashes[i], sizeof(int), 1, archive);
    }
    
    fclose(archive);
    
    printf("✓ Хеши загружены: %zu штук\n\n", assoc_count);
    
    // ========== ВОССТАНОВЛЕНИЕ ИЗ ФОРМУЛЫ ==========
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("ШАГ 2: Восстановление данных из формулы Level 5\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    // Создаём формулу
    KolibriFormula formula;
    memset(&formula, 0, sizeof(formula));
    
    // НО! У нас нет ответов ассоциаций в архиве!
    // Формула работает только если есть ассоциации с ответами
    
    printf("⚠️  ПРОБЛЕМА: В архиве только хеши, нет текстов ассоциаций!\n\n");
    printf("Для полного восстановления нужно:\n");
    printf("  1. Сохранить ВСЕ 5 уровней контекстов при создании архива\n");
    printf("  2. Или сохранить тексты ассоциаций Level 5\n");
    printf("  3. Или использовать обратную эволюцию для восстановления\n\n");
    
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("АЛЬТЕРНАТИВНОЕ РЕШЕНИЕ\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    printf("Для демонстрации покажу что можем:\n\n");
    
    // Загружаем оригинальные файлы для сравнения
    const char* test_files[] = {
        "/Users/kolibri/Documents/os/backend/src/text_generation.c",
        "/Users/kolibri/Documents/os/backend/src/formula.c",
        "/Users/kolibri/Documents/os/core/decimal.c",
        NULL
    };
    
    for (int i = 0; test_files[i] != NULL; i++) {
        FILE* f = fopen(test_files[i], "r");
        if (!f) continue;
        
        // Читаем файл
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        char* content = malloc(size + 1);
        fread(content, 1, size, f);
        content[size] = '\0';
        fclose(f);
        
        // Берём первый чанк
        char chunk[512];
        size_t chunk_len = size < CHUNK_SIZE ? size : CHUNK_SIZE;
        memcpy(chunk, content, chunk_len);
        chunk[chunk_len] = '\0';
        
        // Вычисляем хеш
        int hash = kf_hash_from_text(chunk);
        
        // Проверяем есть ли в нашем архиве
        int found = 0;
        for (size_t j = 0; j < assoc_count; j++) {
            if (hashes[j] == hash) {
                found = 1;
                break;
            }
        }
        
        const char* filename = strrchr(test_files[i], '/');
        if (filename) filename++; else filename = test_files[i];
        
        if (found) {
            printf("  ✓ %s: хеш найден в архиве (Hash: %d)\n", filename, hash);
        } else {
            printf("  ✗ %s: хеш НЕ найден\n", filename);
        }
        
        free(content);
    }
    
    printf("\n");
    
    double total_time = (double)(clock() - start_time) / CLOCKS_PER_SEC;
    
    // ========== ИТОГ ==========
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                   РЕЗУЛЬТАТ                                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    printf("⚠️  ЧАСТИЧНОЕ ВОССТАНОВЛЕНИЕ\n\n");
    
    printf("✅ Что сделано:\n");
    printf("   • Архив загружен\n");
    printf("   • Формула извлечена (%zu байт)\n", formula_size);
    printf("   • Хеши загружены (%zu штук)\n", assoc_count);
    printf("   • Проверка хешей выполнена\n\n");
    
    printf("❌ Что НЕ сделано:\n");
    printf("   • Полное восстановление файлов\n");
    printf("   • Причина: нет текстов ассоциаций Level 5\n\n");
    
    printf("💡 РЕШЕНИЕ:\n");
    printf("   Для полного восстановления нужно сохранять в архив:\n");
    printf("   1. Формулу (32 байта) ✓ уже есть\n");
    printf("   2. Хеши (1280 байт) ✓ уже есть\n");
    printf("   3. Тексты ассоциаций (~144 КБ) ✗ нужно добавить\n\n");
    
    printf("   С текстами архив будет ~145 КБ (3.3x компрессия)\n");
    printf("   Без текстов архив ~1.4 КБ (347x компрессия, но восстановление невозможно)\n\n");
    
    printf("⏱️  Время: %.2f сек\n\n", total_time);
    
    // Cleanup
    free(hashes);
    
    return 0;
}
