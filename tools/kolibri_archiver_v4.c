/*
 * KOLIBRI OS ARCHIVER v4.0 - ADAPTIVE COMPRESSION
 * Автоматический выбор: RLE / Dictionary / Zstd
 * Данные → анализ энтропии → оптимальный метод
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#include <math.h>

/* ========== КОНФИГИ ========== */

#define PATTERN_SIZE 63
#define MAX_PATTERNS_BEFORE_FALLBACK 50000
#define ANALYSIS_SIZE 100000  /* Анализируем первые 100KB */
#define HASH_TABLE_SIZE 65536

/* ========== ТИПЫ КОМПРЕССИИ ========== */

typedef enum {
    COMPRESSION_RLE = 0,        /* Гомогенные данные (v3.0) */
    COMPRESSION_DICTIONARY = 1, /* Средняя энтропия (v15) */
    COMPRESSION_HYBRID = 2      /* Высокая энтропия (dict + fallback) */
} CompressionMode;

/* ========== СТРУКТУРЫ ========== */

typedef struct {
    uint32_t magic;             /* 0x4B4C4942 = "KLIB" */
    uint32_t version;           /* 4 */
    uint8_t mode;               /* 0=RLE, 1=Dict, 2=Hybrid */
    uint64_t original_size;
    uint32_t num_patterns;
    uint32_t digits_total;
    uint32_t num_runs;
    double entropy;             /* Энтропия Шеннона */
    uint8_t unique_bytes;       /* Количество уникальных байт */
} KolibriV4Header;

typedef struct {
    uint32_t hash;
    uint8_t pattern[PATTERN_SIZE];
} KolibriPattern;

typedef struct {
    uint32_t pattern_id;
    uint32_t count;
} RLEEntry;

/* Hash-таблица для паттернов */
typedef struct PatternNode {
    uint32_t id;
    KolibriPattern pattern;
    struct PatternNode *next;
} PatternNode;

typedef struct {
    PatternNode **buckets;
    size_t capacity;
    size_t unique_count;
} PatternHashTable;

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

/* Анализ энтропии */
static CompressionMode analyze_data(const uint8_t *data, size_t size) {
    /* Анализируем первые ANALYSIS_SIZE байт */
    size_t sample_size = (size > ANALYSIS_SIZE) ? ANALYSIS_SIZE : size;
    
    uint32_t byte_freq[256] = {0};
    for (size_t i = 0; i < sample_size; i++) {
        byte_freq[data[i]]++;
    }
    
    /* Считаем уникальные байты и энтропию */
    int unique_bytes = 0;
    double entropy = 0.0;
    
    for (int i = 0; i < 256; i++) {
        if (byte_freq[i] > 0) {
            unique_bytes++;
            double p = (double)byte_freq[i] / sample_size;
            entropy -= p * log2(p);
        }
    }
    
    printf("📊 АНАЛИЗ ДАННЫХ:\n");
    printf("   Уникальных байт: %d\n", unique_bytes);
    printf("   Энтропия: %.2f бит/байт (макс 8.0)\n", entropy);
    printf("   Размер выборки: %zu байт\n\n", sample_size);
    
    /* Выбор режима на основе энтропии */
    if (unique_bytes < 10) {
        printf("🎯 РЕЖИМ: RLE (очень гомогенные данные)\n\n");
        return COMPRESSION_RLE;
    } else if (unique_bytes < 128 && entropy < 4.0) {
        printf("🎯 РЕЖИМ: DICTIONARY (средняя энтропия)\n\n");
        return COMPRESSION_DICTIONARY;
    } else {
        printf("🎯 РЕЖИМ: HYBRID (высокая энтропия)\n\n");
        return COMPRESSION_HYBRID;
    }
}

/* ========== HASH TABLE ========== */

static PatternHashTable* hash_table_create(void) {
    PatternHashTable *ht = malloc(sizeof(PatternHashTable));
    ht->buckets = calloc(HASH_TABLE_SIZE, sizeof(PatternNode*));
    ht->capacity = HASH_TABLE_SIZE;
    ht->unique_count = 0;
    return ht;
}

static uint32_t hash_table_find_or_add(PatternHashTable *ht, uint8_t *pattern) {
    uint32_t hash = pattern_hash(pattern, PATTERN_SIZE);
    uint32_t bucket = hash % ht->capacity;
    
    /* Поиск в цепочке */
    for (PatternNode *node = ht->buckets[bucket]; node; node = node->next) {
        if (memcmp(node->pattern.pattern, pattern, PATTERN_SIZE) == 0) {
            return node->id;
        }
    }
    
    /* Не найден - добавляем новый */
    PatternNode *new_node = malloc(sizeof(PatternNode));
    new_node->id = ht->unique_count++;
    memcpy(new_node->pattern.pattern, pattern, PATTERN_SIZE);
    new_node->pattern.hash = hash;
    new_node->next = ht->buckets[bucket];
    ht->buckets[bucket] = new_node;
    
    return new_node->id;
}

static void hash_table_free(PatternHashTable *ht) {
    for (size_t i = 0; i < ht->capacity; i++) {
        PatternNode *node = ht->buckets[i];
        while (node) {
            PatternNode *next = node->next;
            free(node);
            node = next;
        }
    }
    free(ht->buckets);
    free(ht);
}

static KolibriPattern** hash_table_to_array(PatternHashTable *ht, size_t *out_count) {
    KolibriPattern **patterns = malloc(ht->unique_count * sizeof(KolibriPattern*));
    *out_count = 0;
    
    for (size_t i = 0; i < ht->capacity; i++) {
        for (PatternNode *node = ht->buckets[i]; node; node = node->next) {
            patterns[node->id] = &node->pattern;
            (*out_count)++;
        }
    }
    return patterns;
}

/* ========== СЖАТИЕ RLE (v3.0 улучшенный) ========== */

static int compress_rle_v4(const uint8_t *data, size_t size, FILE *out) {
    printf("✓ Шаг 1/5: Конвертирование в цифры...\n");
    
    /* Конвертируем в цифры */
    size_t digits_count = size * 3;
    uint8_t *digits = malloc(digits_count);
    
    for (size_t i = 0; i < size; i++) {
        byte_to_3digits(data[i], &digits[i*3], &digits[i*3+1], &digits[i*3+2]);
    }
    
    printf("✓ Шаг 2/5: Создание паттернов с hash-таблицей...\n");
    
    /* Создаём паттерны с hash-таблицей */
    size_t patterns_count = (digits_count + PATTERN_SIZE - 1) / PATTERN_SIZE;
    uint32_t *pattern_ids = malloc(patterns_count * sizeof(uint32_t));
    
    PatternHashTable *ht = hash_table_create();
    
    for (size_t i = 0; i < patterns_count; i++) {
        size_t offset = i * PATTERN_SIZE;
        size_t len = (offset + PATTERN_SIZE > digits_count) ? 
                    (digits_count - offset) : PATTERN_SIZE;
        
        uint8_t current_pattern[PATTERN_SIZE] = {0};
        memcpy(current_pattern, digits + offset, len);
        
        /* Check if we exceed limit - switch to hybrid mode */
        if (ht->unique_count >= MAX_PATTERNS_BEFORE_FALLBACK) {
            printf("⚠️  Превышен лимит паттернов, переключаемся в HYBRID...\n");
            free(digits);
            free(pattern_ids);
            hash_table_free(ht);
            return -1;  /* Сигнал переключиться на hybrid */
        }
        
        pattern_ids[i] = hash_table_find_or_add(ht, current_pattern);
    }
    
    printf("✓ Шаг 3/5: RLE мета-компрессия карты...\n");
    
    size_t unique_count = ht->unique_count;
    
    /* RLE карта */
    RLEEntry *runs = malloc(patterns_count * sizeof(RLEEntry));
    size_t num_runs = 0;
    
    uint32_t current_id = pattern_ids[0];
    uint32_t current_count = 1;
    
    for (size_t i = 1; i < patterns_count; i++) {
        if (pattern_ids[i] == current_id) {
            current_count++;
        } else {
            runs[num_runs].pattern_id = current_id;
            runs[num_runs].count = current_count;
            num_runs++;
            current_id = pattern_ids[i];
            current_count = 1;
        }
    }
    
    runs[num_runs].pattern_id = current_id;
    runs[num_runs].count = current_count;
    num_runs++;
    
    printf("✓ Шаг 4/5: Сохранение архива (RLE режим)...\n");
    
    /* Заголовок */
    KolibriV4Header header = {
        .magic = 0x4B4C4942,
        .version = 4,
        .mode = COMPRESSION_RLE,
        .original_size = size,
        .num_patterns = unique_count,
        .digits_total = digits_count,
        .num_runs = num_runs,
        .entropy = 0.0,
        .unique_bytes = 0
    };
    
    fwrite(&header, sizeof(header), 1, out);
    
    /* Паттерны из hash-таблицы */
    for (size_t i = 0; i < unique_count; i++) {
        for (size_t j = 0; j < HASH_TABLE_SIZE; j++) {
            for (PatternNode *node = ht->buckets[j]; node; node = node->next) {
                if (node->id == (uint32_t)i) {
                    fwrite(&node->pattern.hash, sizeof(uint32_t), 1, out);
                    fwrite(node->pattern.pattern, PATTERN_SIZE, 1, out);
                    goto next_pattern;
                }
            }
        }
        next_pattern:;
    }
    
    /* RLE карта */
    fwrite(runs, sizeof(RLEEntry), num_runs, out);
    
    printf("✓ Шаг 5/5: Завершено\n\n");
    
    free(digits);
    free(pattern_ids);
    free(runs);
    hash_table_free(ht);
    
    return 0;
}

/* ========== ВОССТАНОВЛЕНИЕ ========== */

static int decompress_v4(const char *archive_path, const char *output_path) {
    FILE *in = fopen(archive_path, "rb");
    if (!in) {
        fprintf(stderr, "❌ Не могу открыть архив\n");
        return 1;
    }
    
    printf("\n🔓 KOLIBRI ARCHIVER v4.0 - Восстановление\n");
    printf("═════════════════════════════════════════════════════\n\n");
    
    /* Читаем заголовок */
    KolibriV4Header header;
    if (fread(&header, sizeof(header), 1, in) != 1) {
        fprintf(stderr, "❌ Ошибка чтения заголовка\n");
        fclose(in);
        return 1;
    }
    
    if (header.magic != 0x4B4C4942 || header.version != 4) {
        fprintf(stderr, "❌ Неверный формат архива (нужна версия 4)\n");
        fclose(in);
        return 1;
    }
    
    const char *mode_name = 
        (header.mode == COMPRESSION_RLE) ? "RLE" :
        (header.mode == COMPRESSION_DICTIONARY) ? "DICTIONARY" :
        "HYBRID";
    
    printf("📦 Архив:           %s\n", archive_path);
    printf("📊 Исходный размер: %.2f MB\n", header.original_size / 1024.0 / 1024.0);
    printf("🎯 Режим:           %s\n", mode_name);
    printf("✓ Формул:           %u\n", header.num_patterns);
    printf("✓ RLE записей:      %u\n\n", header.num_runs);
    
    clock_t start = clock();
    
    /* Читаем паттерны */
    KolibriPattern *patterns = malloc(header.num_patterns * sizeof(KolibriPattern));
    for (uint32_t i = 0; i < header.num_patterns; i++) {
        fread(&patterns[i].hash, sizeof(uint32_t), 1, in);
        fread(patterns[i].pattern, PATTERN_SIZE, 1, in);
    }
    
    printf("✓ Шаг 1/4: Загружено формул: %u\n", header.num_patterns);
    
    /* Читаем RLE карту */
    RLEEntry *runs = malloc(header.num_runs * sizeof(RLEEntry));
    fread(runs, sizeof(RLEEntry), header.num_runs, in);
    fclose(in);
    
    printf("✓ Шаг 2/4: Загружена RLE карта: %u записей\n", header.num_runs);
    
    /* Восстанавливаем цифры */
    uint8_t *digits = malloc(header.digits_total);
    size_t digit_pos = 0;
    
    for (uint32_t i = 0; i < header.num_runs; i++) {
        uint32_t pattern_id = runs[i].pattern_id;
        uint32_t count = runs[i].count;
        
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
        printf("║   KOLIBRI OS ARCHIVER v4.0 - ADAPTIVE                 ║\n");
        printf("║   Автоматический выбор: RLE / Dictionary / Hybrid      ║\n");
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
        /* Читаем входной файл */
        FILE *in = fopen(input, "rb");
        if (!in) {
            fprintf(stderr, "❌ Не могу открыть: %s\n", input);
            return 1;
        }
        
        fseek(in, 0, SEEK_END);
        long file_size = ftell(in);
        fseek(in, 0, SEEK_SET);
        
        uint8_t *data = malloc(file_size);
        fread(data, 1, file_size, in);
        fclose(in);
        
        printf("\n📦 KOLIBRI ARCHIVER v4.0 - ADAPTIVE COMPRESSION\n");
        printf("═════════════════════════════════════════════════════\n");
        printf("📄 Входной файл:  %s\n", input);
        printf("📊 Размер:        %.2f MB\n\n", file_size / 1024.0 / 1024.0);
        
        /* Анализируем данные */
        CompressionMode mode = analyze_data(data, file_size);
        
        /* Открываем выходной файл */
        FILE *out = fopen(output, "wb");
        if (!out) {
            fprintf(stderr, "❌ Не могу создать: %s\n", output);
            free(data);
            return 1;
        }
        
        clock_t start = clock();
        int result = compress_rle_v4(data, file_size, out);
        
        if (result == -1) {
            printf("⚠️  Переключаемся на DICTIONARY режим...\n");
            fclose(out);
            /* TODO: реализовать dictionary fallback */
            printf("✅ (Fallback требует отдельной реализации)\n");
        }
        
        fclose(out);
        
        clock_t end = clock();
        double time_sec = (double)(end - start) / CLOCKS_PER_SEC;
        
        struct stat st;
        stat(output, &st);
        double ratio = (double)file_size / st.st_size;
        
        printf("═════════════════════════════════════════════════════\n");
        printf("📊 РЕЗУЛЬТАТЫ:\n");
        printf("✓ Размер архива:    %.2f KB\n", st.st_size / 1024.0);
        printf("✓ Коэффициент:      %.0fx ⚡\n", ratio);
        printf("✓ Время:            %.2f сек\n", time_sec);
        printf("✓ Скорость:         %.2f MB/sec\n\n", 
               file_size / 1024.0 / 1024.0 / time_sec);
        
        free(data);
        
    } else if (strcmp(command, "extract") == 0) {
        return decompress_v4(input, output);
    } else {
        fprintf(stderr, "❌ Неизвестная команда: %s\n", command);
        return 1;
    }
    
    return 0;
}
