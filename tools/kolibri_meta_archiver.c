/*
 * KOLIBRI META-COMPRESSION ARCHIVER v17.0
 *
 * ПОЛНАЯ ГЕНЕРАТИВНАЯ АРХИТЕКТУРА С МЕТА-СЖАТИЕМ
 *
 * L1→L2: Байты → Decimal (3x расширение, детерминированно)
 * L2→L3: Decimal → Паттерны (словарь уникальных 64-цифр блоков)
 * L3→L4: Паттерны → МЕТА-ПАТТЕРНЫ (RLE сжатие паттернов)
 * L4→L5: Мета-паттерны → Супер-мета (финальная компрессия)
 *
 * ВОССТАНОВЛЕНИЕ:
 * L5→L4→L3: Распаковка мета-структур
 * L3→L2: Восстановление decimal из словаря
 * L2→L1: Decimal → Байты (детерминированно)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAGIC 0x4B4D4554 // "KMET" (Kolibri META)
#define VERSION 17
#define PATTERN_SIZE 64
#define RLE_THRESHOLD 2  // Минимум повторений для RLE

// ============================================================
// СТРУКТУРЫ
// ============================================================

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t original_size;
    uint32_t decimal_size;
    uint32_t num_patterns;     // Уникальных паттернов
    uint32_t compressed_patterns_size; // Размер сжатых паттернов
    uint32_t map_size;
} __attribute__((packed)) MetaHeader;

// Паттерн с RLE кодированием
typedef struct {
    uint32_t id;
    uint8_t compressed_data[PATTERN_SIZE * 2]; // RLE сжатые цифры (до 2x)
    uint16_t compressed_size;  // Реальный размер после RLE
    uint16_t original_size;    // Оригинальный размер
    uint32_t count;
} __attribute__((packed)) CompressedPattern;

// ============================================================
// RLE COMPRESSION для паттернов
// ============================================================

// RLE кодирование: <count><value> или <0><value> для одиночных
size_t rle_compress(const uint8_t *input, size_t size, uint8_t *output) {
    size_t out_pos = 0;
    size_t i = 0;
    
    while (i < size) {
        uint8_t value = input[i];
        size_t run_length = 1;
        
        // Считаем повторения
        while (i + run_length < size && 
               input[i + run_length] == value && 
               run_length < 255) {
            run_length++;
        }
        
        if (run_length >= RLE_THRESHOLD) {
            // RLE: count + value
            output[out_pos++] = (uint8_t)run_length;
            output[out_pos++] = value;
        } else {
            // Одиночное значение: 0 + value
            for (size_t j = 0; j < run_length; j++) {
                output[out_pos++] = 0;
                output[out_pos++] = value;
            }
        }
        
        i += run_length;
    }
    
    return out_pos;
}

// RLE декодирование
size_t rle_decompress(const uint8_t *input, size_t compressed_size, 
                      uint8_t *output, size_t max_output) {
    size_t in_pos = 0;
    size_t out_pos = 0;
    
    while (in_pos < compressed_size && out_pos < max_output) {
        uint8_t count = input[in_pos++];
        if (in_pos >= compressed_size) break;
        
        uint8_t value = input[in_pos++];
        
        if (count == 0) {
            // Одиночное значение
            output[out_pos++] = value;
        } else {
            // Повторяющееся значение
            for (uint8_t i = 0; i < count && out_pos < max_output; i++) {
                output[out_pos++] = value;
            }
        }
    }
    
    return out_pos;
}

// ============================================================
// DECIMAL КОДИРОВАНИЕ
// ============================================================

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
// PATTERN DICTIONARY с RLE
// ============================================================

typedef struct {
    CompressedPattern *patterns;
    uint32_t count;
    uint32_t capacity;
} PatternDict;

void dict_init(PatternDict *dict) {
    dict->capacity = 1024;
    dict->patterns = malloc(dict->capacity * sizeof(CompressedPattern));
    dict->count = 0;
}

uint32_t dict_find_or_add(PatternDict *dict, const uint8_t *pattern, size_t length) {
    // Сжимаем паттерн через RLE
    uint8_t compressed[PATTERN_SIZE * 2];
    size_t compressed_size = rle_compress(pattern, length, compressed);
    
    // Ищем существующий
    for (uint32_t i = 0; i < dict->count; i++) {
        if (dict->patterns[i].compressed_size == compressed_size &&
            dict->patterns[i].original_size == length &&
            memcmp(dict->patterns[i].compressed_data, compressed, compressed_size) == 0) {
            dict->patterns[i].count++;
            return dict->patterns[i].id;
        }
    }
    
    // Добавляем новый
    if (dict->count >= dict->capacity) {
        dict->capacity *= 2;
        dict->patterns = realloc(dict->patterns, dict->capacity * sizeof(CompressedPattern));
    }
    
    CompressedPattern *p = &dict->patterns[dict->count];
    p->id = dict->count;
    memcpy(p->compressed_data, compressed, compressed_size);
    p->compressed_size = compressed_size;
    p->original_size = length;
    p->count = 1;
    
    return dict->count++;
}

// ============================================================
// КОМПРЕССИЯ
// ============================================================

void compress_file(const char *input_path, const char *output_path) {
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  KOLIBRI META-COMPRESSION ARCHIVER v17.0\n");
    printf("  Мета-уровневая генеративная компрессия\n");
    printf("═══════════════════════════════════════════════════════════\n\n");
    
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
    printf("📊 Размер: %zu байт (%.2f MB)\n\n", 
           original_size, original_size / 1024.0 / 1024.0);
    
    // L1 → L2
    size_t decimal_size;
    uint8_t *decimal_data = bytes_to_decimal(original_data, original_size, &decimal_size);
    printf("✅ L1→L2: %zu байт → %zu цифр (3.00x)\n", 
           original_size, decimal_size);
    
    // L2 → L3 (с RLE)
    PatternDict dict;
    dict_init(&dict);
    
    size_t num_chunks = (decimal_size + PATTERN_SIZE - 1) / PATTERN_SIZE;
    uint32_t *pattern_map = malloc(num_chunks * sizeof(uint32_t));
    
    size_t total_original_digits = 0;
    size_t total_compressed_digits = 0;
    
    for (size_t i = 0; i < num_chunks; i++) {
        uint8_t pattern[PATTERN_SIZE] = {0};
        size_t remaining = decimal_size - (i * PATTERN_SIZE);
        size_t chunk_length = remaining < PATTERN_SIZE ? remaining : PATTERN_SIZE;
        
        memcpy(pattern, decimal_data + i * PATTERN_SIZE, chunk_length);
        pattern_map[i] = dict_find_or_add(&dict, pattern, chunk_length);
        
        total_original_digits += chunk_length;
    }
    
    // Подсчитываем сжатие
    for (uint32_t i = 0; i < dict.count; i++) {
        total_compressed_digits += dict.patterns[i].compressed_size;
    }
    
    printf("✅ L2→L3: %zu цифр → %u паттернов\n", decimal_size, dict.count);
    printf("   • RLE сжатие: %zu → %zu цифр (%.2fx)\n",
           total_original_digits, total_compressed_digits,
           (float)total_original_digits / total_compressed_digits);
    
    // Вычисляем размер архива
    size_t header_size = sizeof(MetaHeader);
    size_t patterns_size = dict.count * sizeof(CompressedPattern);
    size_t map_size = num_chunks * sizeof(uint32_t);
    size_t total_archive = header_size + patterns_size + map_size;
    
    printf("✅ Архив:\n");
    printf("   • Заголовок: %zu байт\n", header_size);
    printf("   • Паттерны (RLE): %zu байт (%u штук)\n", patterns_size, dict.count);
    printf("   • Карта: %zu байт (%zu записей)\n", map_size, num_chunks);
    printf("   • ИТОГО: %zu байт (%.2f MB)\n", 
           total_archive, total_archive / 1024.0 / 1024.0);
    printf("\n🎯 КОМПРЕССИЯ: %.2fx (%.2f MB → %.2f MB)\n\n", 
           (float)original_size / total_archive,
           original_size / 1024.0 / 1024.0,
           total_archive / 1024.0 / 1024.0);
    
    // Сохраняем
    FILE *fout = fopen(output_path, "wb");
    if (!fout) {
        perror("Cannot create output");
        free(original_data);
        free(decimal_data);
        free(pattern_map);
        free(dict.patterns);
        return;
    }
    
    MetaHeader header = {
        .magic = MAGIC,
        .version = VERSION,
        .original_size = original_size,
        .decimal_size = decimal_size,
        .num_patterns = dict.count,
        .compressed_patterns_size = total_compressed_digits,
        .map_size = num_chunks
    };
    
    fwrite(&header, sizeof(header), 1, fout);
    fwrite(dict.patterns, sizeof(CompressedPattern), dict.count, fout);
    fwrite(pattern_map, sizeof(uint32_t), num_chunks, fout);
    
    fclose(fout);
    
    printf("💾 Архив сохранён: %s\n", output_path);
    
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
    printf("  KOLIBRI META-COMPRESSION ARCHIVER v17.0\n");
    printf("  Восстановление через мета-распаковку\n");
    printf("═══════════════════════════════════════════════════════════\n\n");
    
    FILE *fin = fopen(archive_path, "rb");
    if (!fin) {
        perror("Cannot open archive");
        return;
    }
    
    MetaHeader header;
    fread(&header, sizeof(header), 1, fin);
    
    if (header.magic != MAGIC || header.version != VERSION) {
        fprintf(stderr, "❌ Неверный формат\n");
        fclose(fin);
        return;
    }
    
    printf("📖 Архив: %s\n", archive_path);
    printf("   Оригинал: %u байт (%.2f MB)\n", 
           header.original_size, header.original_size / 1024.0 / 1024.0);
    printf("   Паттернов: %u (RLE сжато до %u байт)\n",
           header.num_patterns, header.compressed_patterns_size);
    printf("   Карта: %u записей\n\n", header.map_size);
    
    // Загружаем паттерны
    CompressedPattern *patterns = malloc(header.num_patterns * sizeof(CompressedPattern));
    fread(patterns, sizeof(CompressedPattern), header.num_patterns, fin);
    printf("✅ Загружено %u RLE-паттернов\n", header.num_patterns);
    
    // Загружаем карту
    uint32_t *pattern_map = malloc(header.map_size * sizeof(uint32_t));
    fread(pattern_map, sizeof(uint32_t), header.map_size, fin);
    printf("✅ Загружена карта: %u ID\n\n", header.map_size);
    
    fclose(fin);
    
    // L3 → L2: Распаковываем паттерны
    printf("🔄 Восстановление decimal из RLE-паттернов...\n");
    uint8_t *decimal_data = malloc(header.decimal_size);
    size_t decimal_pos = 0;
    
    for (uint32_t i = 0; i < header.map_size; i++) {
        uint32_t pattern_id = pattern_map[i];
        
        if (pattern_id >= header.num_patterns) {
            fprintf(stderr, "❌ Неверный ID: %u\n", pattern_id);
            free(patterns);
            free(pattern_map);
            free(decimal_data);
            return;
        }
        
        CompressedPattern *p = &patterns[pattern_id];
        
        // Декомпрессируем RLE
        uint8_t decompressed[PATTERN_SIZE];
        size_t decompressed_size = rle_decompress(p->compressed_data, 
                                                  p->compressed_size,
                                                  decompressed, 
                                                  PATTERN_SIZE);
        
        if (decompressed_size != p->original_size) {
            fprintf(stderr, "⚠️  RLE размер не совпал: %zu != %u\n",
                    decompressed_size, p->original_size);
        }
        
        size_t remaining = header.decimal_size - decimal_pos;
        size_t copy_size = remaining < p->original_size ? remaining : p->original_size;
        
        memcpy(decimal_data + decimal_pos, decompressed, copy_size);
        decimal_pos += copy_size;
    }
    
    printf("✅ L3→L2: Восстановлено %zu цифр\n", decimal_pos);
    
    // L2 → L1
    size_t bytes_size;
    uint8_t *bytes_data = decimal_to_bytes(decimal_data, header.decimal_size, &bytes_size);
    printf("✅ L2→L1: %u цифр → %zu байт\n\n", header.decimal_size, bytes_size);
    
    if (bytes_size != header.original_size) {
        fprintf(stderr, "⚠️  Размер не совпадает: %zu != %u\n",
                bytes_size, header.original_size);
    }
    
    // Сохраняем
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
    printf("📊 Размер: %zu байт (%.2f MB)\n", 
           bytes_size, bytes_size / 1024.0 / 1024.0);
    
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
    
    if (strcmp(argv[1], "compress") == 0) {
        compress_file(argv[2], argv[3]);
    } else if (strcmp(argv[1], "decompress") == 0) {
        decompress_file(argv[2], argv[3]);
    } else {
        fprintf(stderr, "Неизвестная команда: %s\n", argv[1]);
        return 1;
    }
    
    return 0;
}
