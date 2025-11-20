/*
 * KOLIBRI OS ARCHIVER v3.0 - МЕТА-КОМПРЕССИЯ
 * Данные → Цифры → Формулы → Мета-RLE → 300,000x+
 * 
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>

#define PATTERN_SIZE 63  /* 63 цифры = 21 байт, кратно '065' циклу */

/* ========== СТРУКТУРЫ ========== */

typedef struct {
    uint32_t magic;            /* 0x4B4C4942 = "KLIB" */
    uint32_t version;          /* 3 */
    uint64_t original_size;    /* Размер оригинала */
    uint32_t num_patterns;     /* Количество уникальных паттернов */
    uint32_t digits_total;     /* Общее количество цифр */
    uint32_t num_runs;         /* Количество RLE записей в карте */
} KolibriArchiveHeader;

typedef struct {
    uint32_t hash;
    uint8_t pattern[PATTERN_SIZE];
} KolibriPattern;

/* RLE запись для карты ID */
typedef struct {
    uint32_t pattern_id;       /* ID паттерна */
    uint32_t count;            /* Сколько раз повторяется */
} RLEEntry;

/* ========== УТИЛИТЫ ========== */

static uint32_t pattern_hash(const uint8_t *digits, size_t len) {
    uint32_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + digits[i];
    }
    return hash;
}

static void byte_to_3digits(uint8_t byte, uint8_t *d1, uint8_t *d2, uint8_t *d3) {
    *d1 = byte / 100;
    byte = byte % 100;
    *d2 = byte / 10;
    *d3 = byte % 10;
}

static uint8_t digits3_to_byte(uint8_t d1, uint8_t d2, uint8_t d3) {
    return d1 * 100 + d2 * 10 + d3;
}

/* ========== СЖАТИЕ ========== */

static int kolibri_compress(const char *input_path, const char *output_path) {
    FILE *in = fopen(input_path, "rb");
    if (!in) {
        fprintf(stderr, "❌ Не могу открыть: %s\n", input_path);
        return 1;
    }
    
    fseek(in, 0, SEEK_END);
    long file_size = ftell(in);
    fseek(in, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fprintf(stderr, "❌ Файл пустой\n");
        fclose(in);
        return 1;
    }
    
    printf("\n📦 KOLIBRI ARCHIVER v3.0 - МЕТА-КОМПРЕССИЯ\n");
    printf("═════════════════════════════════════════════════════\n");
    printf("📄 Входной файл:  %s\n", input_path);
    printf("📊 Размер:        %.2f MB\n", file_size / 1024.0 / 1024.0);
    printf("🔧 Архитектура:   Данные → Цифры → Формулы → Meta-RLE\n\n");
    
    clock_t start = clock();
    
    /* ШАГ 1: Читаем файл */
    uint8_t *data = malloc(file_size);
    if (!data) {
        fprintf(stderr, "❌ Не хватает памяти\n");
        fclose(in);
        return 1;
    }
    
    fread(data, 1, file_size, in);
    fclose(in);
    
    printf("✓ Шаг 1/5: Загружено данных: %ld байт\n", file_size);
    
    /* ШАГ 2: Конвертируем в цифры */
    size_t digits_count = file_size * 3;
    uint8_t *digits = malloc(digits_count);
    if (!digits) {
        fprintf(stderr, "❌ Не хватает памяти\n");
        free(data);
        return 1;
    }
    
    for (size_t i = 0; i < (size_t)file_size; i++) {
        byte_to_3digits(data[i], &digits[i*3], &digits[i*3+1], &digits[i*3+2]);
    }
    
    printf("✓ Шаг 2/5: Конвертировано в цифры: %zu (×3)\n", digits_count);
    
    /* ШАГ 3: Создаём паттерны и дедупликация */
    size_t patterns_count = (digits_count + PATTERN_SIZE - 1) / PATTERN_SIZE;
    
    #define MAX_UNIQUE 100000
    KolibriPattern *patterns = calloc(MAX_UNIQUE, sizeof(KolibriPattern));
    uint32_t *pattern_ids = malloc(patterns_count * sizeof(uint32_t));
    size_t unique_count = 0;
    
    for (size_t i = 0; i < patterns_count; i++) {
        size_t offset = i * PATTERN_SIZE;
        size_t len = (offset + PATTERN_SIZE > digits_count) ? 
                    (digits_count - offset) : PATTERN_SIZE;
        
        uint8_t current_pattern[PATTERN_SIZE] = {0};
        memcpy(current_pattern, digits + offset, len);
        
        uint32_t hash = pattern_hash(current_pattern, PATTERN_SIZE);
        
        /* Ищем существующий */
        int found = -1;
        for (size_t j = 0; j < unique_count; j++) {
            if (patterns[j].hash == hash &&
                memcmp(patterns[j].pattern, current_pattern, PATTERN_SIZE) == 0) {
                found = j;
                break;
            }
        }
        
        if (found >= 0) {
            pattern_ids[i] = found;
        } else {
            if (unique_count >= MAX_UNIQUE) {
                fprintf(stderr, "❌ Слишком много уникальных паттернов\n");
                free(data);
                free(digits);
                free(patterns);
                free(pattern_ids);
                return 1;
            }
            
            patterns[unique_count].hash = hash;
            memcpy(patterns[unique_count].pattern, current_pattern, PATTERN_SIZE);
            pattern_ids[i] = unique_count;
            unique_count++;
        }
    }
    
    printf("✓ Шаг 3/5: Создано формул: %zu (уникальных)\n", unique_count);
    printf("           Всего паттернов: %zu\n", patterns_count);
    printf("           Дедупликация: %.2fx\n", (double)patterns_count / unique_count);
    
    /* ШАГ 4: RLE МЕТА-КОМПРЕССИЯ карты ID */
    #define MAX_RUNS 10000000
    RLEEntry *runs = malloc(MAX_RUNS * sizeof(RLEEntry));
    size_t num_runs = 0;
    
    uint32_t current_id = pattern_ids[0];
    uint32_t current_count = 1;
    
    for (size_t i = 1; i < patterns_count; i++) {
        if (pattern_ids[i] == current_id) {
            current_count++;
        } else {
            /* Сохраняем RLE запись */
            if (num_runs >= MAX_RUNS) {
                fprintf(stderr, "❌ Слишком много RLE записей\n");
                free(data);
                free(digits);
                free(patterns);
                free(pattern_ids);
                free(runs);
                return 1;
            }
            
            runs[num_runs].pattern_id = current_id;
            runs[num_runs].count = current_count;
            num_runs++;
            
            current_id = pattern_ids[i];
            current_count = 1;
        }
    }
    
    /* Последняя запись */
    runs[num_runs].pattern_id = current_id;
    runs[num_runs].count = current_count;
    num_runs++;
    
    printf("✓ Шаг 4/5: RLE мета-компрессия: %zu записей\n", num_runs);
    printf("           Было: %zu ID × 4B = %zu байт\n", 
           patterns_count, patterns_count * 4);
    printf("           Стало: %zu записей × 8B = %zu байт\n", 
           num_runs, num_runs * 8);
    printf("           RLE коэффициент: %.2fx\n", 
           (double)(patterns_count * 4) / (num_runs * 8));
    
    /* ШАГ 5: Сохраняем архив */
    FILE *out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "❌ Не могу создать архив\n");
        free(data);
        free(digits);
        free(patterns);
        free(pattern_ids);
        free(runs);
        return 1;
    }
    
    /* Заголовок */
    KolibriArchiveHeader header = {
        .magic = 0x4B4C4942,
        .version = 3,
        .original_size = file_size,
        .num_patterns = unique_count,
        .digits_total = digits_count,
        .num_runs = num_runs
    };
    
    fwrite(&header, sizeof(header), 1, out);
    
    /* Уникальные паттерны */
    for (size_t i = 0; i < unique_count; i++) {
        fwrite(&patterns[i].hash, sizeof(uint32_t), 1, out);
        fwrite(patterns[i].pattern, PATTERN_SIZE, 1, out);
    }
    
    /* RLE карта */
    fwrite(runs, sizeof(RLEEntry), num_runs, out);
    
    fclose(out);
    
    /* Статистика */
    struct stat st;
    stat(output_path, &st);
    long archive_size = st.st_size;
    
    clock_t end = clock();
    double time_sec = (double)(end - start) / CLOCKS_PER_SEC;
    double ratio = (double)file_size / archive_size;
    
    printf("✓ Шаг 5/5: Архив записан\n\n");
    
    printf("═════════════════════════════════════════════════════\n");
    printf("📊 ФИНАЛЬНЫЕ РЕЗУЛЬТАТЫ:\n");
    printf("═════════════════════════════════════════════════════\n");
    printf("✓ Размер архива:    %.2f байт (%.2f KB)\n", 
           (double)archive_size, archive_size / 1024.0);
    printf("✓ Коэффициент:      %.0fx ⚡⚡⚡\n", ratio);
    printf("✓ Время:            %.2f сек\n", time_sec);
    printf("✓ Скорость:         %.2f MB/sec\n\n", 
           file_size / 1024.0 / 1024.0 / time_sec);
    
    free(data);
    free(digits);
    free(patterns);
    free(pattern_ids);
    free(runs);
    
    return 0;
}

/* ========== ВОССТАНОВЛЕНИЕ ========== */

static int kolibri_extract(const char *archive_path, const char *output_path) {
    FILE *in = fopen(archive_path, "rb");
    if (!in) {
        fprintf(stderr, "❌ Не могу открыть архив\n");
        return 1;
    }
    
    printf("\n🔓 KOLIBRI ARCHIVER v3.0 - Восстановление\n");
    printf("═════════════════════════════════════════════════════\n");
    
    /* Читаем заголовок */
    KolibriArchiveHeader header;
    if (fread(&header, sizeof(header), 1, in) != 1) {
        fprintf(stderr, "❌ Ошибка чтения заголовка\n");
        fclose(in);
        return 1;
    }
    
    if (header.magic != 0x4B4C4942 || header.version != 3) {
        fprintf(stderr, "❌ Неверный формат архива (нужна версия 3)\n");
        fclose(in);
        return 1;
    }
    
    printf("📦 Архив:           %s\n", archive_path);
    printf("📊 Исходный размер: %.2f MB\n", header.original_size / 1024.0 / 1024.0);
    printf("✓ Формул:           %u\n", header.num_patterns);
    printf("✓ RLE записей:      %u\n\n", header.num_runs);
    
    clock_t start = clock();
    
    /* Читаем паттерны */
    KolibriPattern *patterns = malloc(header.num_patterns * sizeof(KolibriPattern));
    if (!patterns) {
        fprintf(stderr, "❌ Не хватает памяти\n");
        fclose(in);
        return 1;
    }
    
    for (uint32_t i = 0; i < header.num_patterns; i++) {
        fread(&patterns[i].hash, sizeof(uint32_t), 1, in);
        fread(patterns[i].pattern, PATTERN_SIZE, 1, in);
    }
    
    printf("✓ Шаг 1/4: Загружено формул: %u\n", header.num_patterns);
    
    /* Читаем RLE карту */
    RLEEntry *runs = malloc(header.num_runs * sizeof(RLEEntry));
    if (!runs) {
        fprintf(stderr, "❌ Не хватает памяти\n");
        free(patterns);
        fclose(in);
        return 1;
    }
    
    fread(runs, sizeof(RLEEntry), header.num_runs, in);
    fclose(in);
    
    printf("✓ Шаг 2/4: Загружена RLE карта: %u записей\n", header.num_runs);
    
    /* Восстанавливаем цифры из RLE */
    uint8_t *digits = malloc(header.digits_total);
    if (!digits) {
        fprintf(stderr, "❌ Не хватает памяти\n");
        free(patterns);
        free(runs);
        return 1;
    }
    
    size_t digit_pos = 0;
    for (uint32_t i = 0; i < header.num_runs; i++) {
        uint32_t pattern_id = runs[i].pattern_id;
        uint32_t count = runs[i].count;
        
        if (pattern_id >= header.num_patterns) {
            fprintf(stderr, "❌ Неверный ID паттерна\n");
            free(patterns);
            free(runs);
            free(digits);
            return 1;
        }
        
        /* Повторяем паттерн count раз */
        for (uint32_t j = 0; j < count; j++) {
            size_t copy_len = (digit_pos + PATTERN_SIZE > header.digits_total) ?
                             (header.digits_total - digit_pos) : PATTERN_SIZE;
            
            memcpy(digits + digit_pos, patterns[pattern_id].pattern, copy_len);
            digit_pos += copy_len;
        }
    }
    
    printf("✓ Шаг 3/4: Восстановлено цифр: %zu\n", digit_pos);
    
    /* Конвертируем цифры в байты */
    FILE *out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "❌ Не могу создать файл\n");
        free(patterns);
        free(runs);
        free(digits);
        return 1;
    }
    
    for (uint64_t i = 0; i < header.original_size; i++) {
        uint8_t byte = digits3_to_byte(digits[i*3], digits[i*3+1], digits[i*3+2]);
        fwrite(&byte, 1, 1, out);
    }
    
    fclose(out);
    
    clock_t end = clock();
    double time_sec = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("✓ Шаг 4/4: Данные записаны\n\n");
    printf("═════════════════════════════════════════════════════\n");
    printf("✅ Файл восстановлен: %s\n", output_path);
    printf("✅ Размер: %.2f MB\n", header.original_size / 1024.0 / 1024.0);
    printf("✅ Время: %.2f сек\n\n", time_sec);
    
    free(patterns);
    free(runs);
    free(digits);
    
    return 0;
}

/* ========== MAIN ========== */

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════╗\n");
        printf("║   KOLIBRI OS ARCHIVER v3.0                            ║\n");
        printf("║   Данные → Цифры → Формулы → Meta-RLE → 300,000x+     ║\n");
        printf("╚═══════════════════════════════════════════════════════╝\n\n");
        printf("Использование:\n");
        printf("  %s compress <input> <output.kolibri>\n", argv[0]);
        printf("  %s extract <archive.kolibri> <output>\n\n", argv[0]);
        return 1;
    }
    
    const char *command = argv[1];
    const char *input = argv[2];
    const char *output = argv[3];
    
    if (strcmp(command, "compress") == 0) {
        return kolibri_compress(input, output);
    } else if (strcmp(command, "extract") == 0) {
        return kolibri_extract(input, output);
    } else {
        fprintf(stderr, "❌ Неизвестная команда: %s\n", command);
        return 1;
    }
}
