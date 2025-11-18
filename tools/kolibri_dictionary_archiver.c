/*
 * KOLIBRI DICTIONARY ARCHIVER v15.0
 *
 * НАСТОЯЩАЯ АРХИТЕКТУРА - Dictionary-based compression
 *
 * АРХИВ СОДЕРЖИТ:
 * 1. Заголовок (метаинформация о размерах)
 * 2. Словарь паттернов (decimal sequences)
 * 3. Карту ID (какой паттерн где используется)
 *
 * ВОССТАНОВЛЕНИЕ:
 * 1. Читаем словарь из архива
 * 2. По карте ID восстанавливаем decimal строку
 * 3. Декодируем decimal → байты (детерминированно)
 *
 * ЭТО НЕ ВОССТАНОВЛЕНИЕ ИЗ ХЕШЕЙ!
 * ЭТО РАСПАКОВКА СЛОВАРЯ (как LZ77/LZW)!
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAGIC 0x4B4C4942 // "KLIB"
#define VERSION 15
#define PATTERN_SIZE 64  // Размер паттерна в цифрах

// ============================================================
// СТРУКТУРЫ
// ============================================================

typedef struct {
    uint32_t magic;           // 0x4B4C4942
    uint32_t version;         // 15
    uint32_t original_size;   // Размер оригинальных данных (байты)
    uint32_t decimal_size;    // Размер decimal строки (цифры)
    uint32_t num_patterns;    // Количество уникальных паттернов
    uint32_t map_size;        // Размер карты ID
} __attribute__((packed)) KolibriHeader;

typedef struct {
    uint32_t id;              // ID паттерна
    uint8_t digits[PATTERN_SIZE]; // 64 цифры (0-9)
    uint32_t count;           // Сколько раз встречается
} __attribute__((packed)) KolibriPattern;

// ============================================================
// L1 → L2: DECIMAL ENCODING
// ============================================================

// Байты → Decimal (детерминированно, обратимо)
uint8_t* bytes_to_decimal(const uint8_t *bytes, size_t size, size_t *decimal_size) {
    *decimal_size = size * 3;
    uint8_t *decimal = malloc(*decimal_size);
    if (!decimal) return NULL;
    
    for (size_t i = 0; i < size; i++) {
        decimal[i * 3 + 0] = bytes[i] / 100;
        decimal[i * 3 + 1] = (bytes[i] % 100) / 10;
        decimal[i * 3 + 2] = bytes[i] % 10;
    }
    
    return decimal;
}

// Decimal → Байты (детерминированно, обратимо)
uint8_t* decimal_to_bytes(const uint8_t *decimal, size_t decimal_size, size_t *bytes_size) {
    *bytes_size = decimal_size / 3;
    uint8_t *bytes = malloc(*bytes_size);
    if (!bytes) return NULL;
    
    for (size_t i = 0; i < *bytes_size; i++) {
        bytes[i] = decimal[i * 3 + 0] * 100 +
                   decimal[i * 3 + 1] * 10 +
                   decimal[i * 3 + 2];
    }
    
    return bytes;
}

// ============================================================
// L2 → L3: PATTERN EXTRACTION (словарь)
// ============================================================

typedef struct {
    KolibriPattern *patterns;
    uint32_t count;
    uint32_t capacity;
} PatternDict;

void dict_init(PatternDict *dict) {
    dict->capacity = 1024;
    dict->patterns = malloc(dict->capacity * sizeof(KolibriPattern));
    dict->count = 0;
}

uint32_t dict_find_or_add(PatternDict *dict, const uint8_t *pattern_digits) {
    // Ищем существующий паттерн
    for (uint32_t i = 0; i < dict->count; i++) {
        if (memcmp(dict->patterns[i].digits, pattern_digits, PATTERN_SIZE) == 0) {
            dict->patterns[i].count++;
            return dict->patterns[i].id;
        }
    }
    
    // Добавляем новый
    if (dict->count >= dict->capacity) {
        dict->capacity *= 2;
        dict->patterns = realloc(dict->patterns, dict->capacity * sizeof(KolibriPattern));
    }
    
    KolibriPattern *p = &dict->patterns[dict->count];
    p->id = dict->count;
    memcpy(p->digits, pattern_digits, PATTERN_SIZE);
    p->count = 1;
    
    return dict->count++;
}

// ============================================================
// КОМПРЕССИЯ
// ============================================================

void compress_file(const char *input_path, const char *output_path) {
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  KOLIBRI DICTIONARY ARCHIVER v15.0\n");
    printf("  Реальная dictionary-based compression\n");
    printf("═══════════════════════════════════════════════════════════\n\n");
    
    // Читаем входной файл
    FILE *fin = fopen(input_path, "rb");
    if (!fin) {
        perror("Cannot open input");
        return;
    }
    
    fseek(fin, 0, SEEK_END);
    size_t original_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    
    uint8_t *original_data = malloc(original_size);
    fread(original_data, 1, original_size, fin);
    fclose(fin);
    
    printf("📄 Входной файл: %s\n", input_path);
    printf("📊 Размер: %zu байт\n\n", original_size);
    
    // L1 → L2: Bytes → Decimal
    size_t decimal_size;
    uint8_t *decimal_data = bytes_to_decimal(original_data, original_size, &decimal_size);
    printf("✅ L1→L2: %zu байт → %zu цифр (%.2fx)\n", 
           original_size, decimal_size, (float)decimal_size/original_size);
    
    // L2 → L3: Decimal → Patterns
    PatternDict dict;
    dict_init(&dict);
    
    size_t num_chunks = (decimal_size + PATTERN_SIZE - 1) / PATTERN_SIZE;
    uint32_t *pattern_map = malloc(num_chunks * sizeof(uint32_t));
    
    for (size_t i = 0; i < num_chunks; i++) {
        uint8_t pattern[PATTERN_SIZE] = {0};
        size_t remaining = decimal_size - (i * PATTERN_SIZE);
        size_t chunk_size = remaining < PATTERN_SIZE ? remaining : PATTERN_SIZE;
        
        memcpy(pattern, decimal_data + i * PATTERN_SIZE, chunk_size);
        pattern_map[i] = dict_find_or_add(&dict, pattern);
    }
    
    printf("✅ L2→L3: %zu цифр → %u уникальных паттернов\n",
           decimal_size, dict.count);
    
    // Вычисляем размер архива
    size_t header_size = sizeof(KolibriHeader);
    size_t patterns_size = dict.count * sizeof(KolibriPattern);
    size_t map_size = num_chunks * sizeof(uint32_t);
    size_t total_archive = header_size + patterns_size + map_size;
    
    printf("✅ Архив: заголовок=%zu + словарь=%zu + карта=%zu = %zu байт\n",
           header_size, patterns_size, map_size, total_archive);
    printf("🎯 КОМПРЕССИЯ: %.2fx\n\n", (float)original_size / total_archive);
    
    // Сохраняем архив
    FILE *fout = fopen(output_path, "wb");
    if (!fout) {
        perror("Cannot create output");
        free(original_data);
        free(decimal_data);
        free(pattern_map);
        free(dict.patterns);
        return;
    }
    
    // Заголовок
    KolibriHeader header = {
        .magic = MAGIC,
        .version = VERSION,
        .original_size = original_size,
        .decimal_size = decimal_size,
        .num_patterns = dict.count,
        .map_size = num_chunks
    };
    fwrite(&header, sizeof(header), 1, fout);
    
    // Словарь паттернов
    fwrite(dict.patterns, sizeof(KolibriPattern), dict.count, fout);
    
    // Карта ID
    fwrite(pattern_map, sizeof(uint32_t), num_chunks, fout);
    
    fclose(fout);
    
    printf("💾 Архив сохранён: %s\n", output_path);
    printf("📦 Размер архива: %zu байт\n", total_archive);
    
    free(original_data);
    free(decimal_data);
    free(pattern_map);
    free(dict.patterns);
}

// ============================================================
// ДЕКОМПРЕССИЯ
// ============================================================

void decompress_file(const char *archive_path, const char *output_path) {
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  KOLIBRI DICTIONARY ARCHIVER v15.0\n");
    printf("  Восстановление из словаря\n");
    printf("═══════════════════════════════════════════════════════════\n\n");
    
    // Читаем архив
    FILE *fin = fopen(archive_path, "rb");
    if (!fin) {
        perror("Cannot open archive");
        return;
    }
    
    // Читаем заголовок
    KolibriHeader header;
    fread(&header, sizeof(header), 1, fin);
    
    if (header.magic != MAGIC || header.version != VERSION) {
        fprintf(stderr, "❌ Неверный формат архива\n");
        fclose(fin);
        return;
    }
    
    printf("📖 Архив: %s\n", archive_path);
    printf("   Оригинальный размер: %u байт\n", header.original_size);
    printf("   Decimal размер: %u цифр\n", header.decimal_size);
    printf("   Паттернов: %u\n", header.num_patterns);
    printf("   Карта: %u записей\n\n", header.map_size);
    
    // Читаем словарь
    KolibriPattern *patterns = malloc(header.num_patterns * sizeof(KolibriPattern));
    fread(patterns, sizeof(KolibriPattern), header.num_patterns, fin);
    printf("✅ Загружен словарь: %u паттернов\n", header.num_patterns);
    
    // Читаем карту
    uint32_t *pattern_map = malloc(header.map_size * sizeof(uint32_t));
    fread(pattern_map, sizeof(uint32_t), header.map_size, fin);
    printf("✅ Загружена карта: %u ID\n\n", header.map_size);
    
    fclose(fin);
    
    // L3 → L2: Pattern dictionary → Decimal string
    uint8_t *decimal_data = malloc(header.decimal_size);
    size_t decimal_pos = 0;
    
    for (uint32_t i = 0; i < header.map_size; i++) {
        uint32_t pattern_id = pattern_map[i];
        if (pattern_id >= header.num_patterns) {
            fprintf(stderr, "❌ Неверный ID паттерна: %u\n", pattern_id);
            free(patterns);
            free(pattern_map);
            free(decimal_data);
            return;
        }
        
        size_t remaining = header.decimal_size - decimal_pos;
        size_t copy_size = remaining < PATTERN_SIZE ? remaining : PATTERN_SIZE;
        
        memcpy(decimal_data + decimal_pos, patterns[pattern_id].digits, copy_size);
        decimal_pos += copy_size;
    }
    
    printf("✅ L3→L2: Восстановлено %zu цифр из словаря\n", decimal_pos);
    
    // L2 → L1: Decimal → Bytes
    size_t bytes_size;
    uint8_t *bytes_data = decimal_to_bytes(decimal_data, header.decimal_size, &bytes_size);
    printf("✅ L2→L1: %zu цифр → %zu байт\n\n", header.decimal_size, bytes_size);
    
    // Проверка размера
    if (bytes_size != header.original_size) {
        fprintf(stderr, "⚠️  ВНИМАНИЕ: Размер не совпадает! Ожидалось %u, получено %zu\n",
                header.original_size, bytes_size);
    }
    
    // Сохраняем восстановленный файл
    FILE *fout = fopen(output_path, "wb");
    if (!fout) {
        perror("Cannot create output");
        free(patterns);
        free(pattern_map);
        free(decimal_data);
        free(bytes_data);
        return;
    }
    
    fwrite(bytes_data, 1, bytes_size, fout);
    fclose(fout);
    
    printf("💾 Файл восстановлен: %s\n", output_path);
    printf("📊 Размер: %zu байт\n", bytes_size);
    
    free(patterns);
    free(pattern_map);
    free(decimal_data);
    free(bytes_data);
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Использование:\n");
        printf("  %s compress <input> <archive.kolibri>\n", argv[0]);
        printf("  %s decompress <archive.kolibri> <output>\n", argv[0]);
        return 1;
    }
    
    const char *command = argv[1];
    
    if (strcmp(command, "compress") == 0) {
        compress_file(argv[2], argv[3]);
    } else if (strcmp(command, "decompress") == 0) {
        decompress_file(argv[2], argv[3]);
    } else {
        fprintf(stderr, "Неизвестная команда: %s\n", command);
        return 1;
    }
    
    return 0;
}
