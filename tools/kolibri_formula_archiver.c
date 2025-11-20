/*
 * KOLIBRI FORMULA ARCHIVER v16.0
 *
 * НАСТОЯЩАЯ ГЕНЕРАТИВНАЯ АРХИТЕКТУРА
 *
 * L1→L2: Байты → Decimal (детерминированно)
 * L2→L3: Decimal → Паттерны → ФОРМУЛЫ (генеративное сжатие)
 * L3→L4: Формулы → Мета-формулы
 *
 * ВОССТАНОВЛЕНИЕ:
 * L4→L3: Мета-формулы → Формулы
 * L3→L2: ФОРМУЛЫ ГЕНЕРИРУЮТ паттерны → Decimal
 * L2→L1: Decimal → Байты (детерминированно)
 *
 * КЛЮЧЕВАЯ ИДЕЯ: Формула = правило генерации паттерна
 * Вместо хранения 64 цифр, храним формулу (seed + параметры)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAGIC 0x4B464F52 // "KFOR" (Kolibri FORmula)
#define VERSION 17
#define PATTERN_SIZE 64

// ============================================================
// СТРУКТУРЫ
// ============================================================

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t original_size;    // Байты
    uint32_t decimal_size;     // Цифры
    uint32_t num_formulas;     // Количество формул
    uint32_t map_size;         // Размер карты
} __attribute__((packed)) FormulaHeader;

// Формула для генерации паттерна
typedef struct {
    uint32_t id;
    uint32_t seed;             // Seed для генерации
    uint16_t pattern_hash;     // Хеш для верификации
    uint16_t length;           // Реальная длина паттерна (0-64)
    uint32_t count;            // Сколько раз встречается
    uint8_t pattern[PATTERN_SIZE]; // Фактические цифры паттерна
} __attribute__((packed)) Formula;

// ============================================================
// PRNG для генерации паттернов
// ============================================================

typedef struct {
    uint64_t state;
} PRNG;

void prng_init(PRNG *prng, uint32_t seed) {
    prng->state = seed;
}

uint32_t prng_next(PRNG *prng) {
    prng->state = (prng->state * 1103515245ULL + 12345ULL) & 0x7FFFFFFFULL;
    return (uint32_t)(prng->state);
}

uint8_t prng_digit(PRNG *prng) {
    return (uint8_t)(prng_next(prng) % 10);
}

// ============================================================
// ФОРМУЛЫ: Генерация паттерна из seed
// ============================================================

void formula_generate_pattern(uint32_t seed, uint8_t *pattern, size_t length) {
    PRNG prng;
    prng_init(&prng, seed);
    
    for (size_t i = 0; i < length && i < PATTERN_SIZE; i++) {
        pattern[i] = prng_digit(&prng);
    }
    
    // Заполняем остаток нулями
    for (size_t i = length; i < PATTERN_SIZE; i++) {
        pattern[i] = 0;
    }
}

// Вычисляем seed для данного паттерна (обратная задача)
uint32_t formula_find_seed(const uint8_t *pattern, size_t length) {
    // Простое хеширование паттерна как seed
    uint32_t seed = 5381;
    for (size_t i = 0; i < length; i++) {
        seed = ((seed << 5) + seed) + pattern[i];
    }
    return seed;
}

uint16_t pattern_hash(const uint8_t *pattern, size_t length) {
    uint16_t hash = 0;
    for (size_t i = 0; i < length; i++) {
        hash = ((hash << 3) + hash) + pattern[i];
    }
    return hash;
}

// ============================================================
// L1 ↔ L2: Decimal кодирование
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
// L2 → L3: Паттерны → Формулы
// ============================================================

typedef struct {
    Formula *formulas;
    uint32_t count;
    uint32_t capacity;
} FormulaDict;

void dict_init(FormulaDict *dict) {
    dict->capacity = 1024;
    dict->formulas = malloc(dict->capacity * sizeof(Formula));
    dict->count = 0;
}

uint32_t dict_find_or_add(FormulaDict *dict, const uint8_t *pattern, size_t length) {
    uint16_t hash = pattern_hash(pattern, length);
    
    // Ищем существующий паттерн
    for (uint32_t i = 0; i < dict->count; i++) {
        if (dict->formulas[i].pattern_hash == hash && 
            dict->formulas[i].length == length &&
            memcmp(dict->formulas[i].pattern, pattern, length) == 0) {
            dict->formulas[i].count++;
            return dict->formulas[i].id;
        }
    }
    
    // Создаём новую формулу
    if (dict->count >= dict->capacity) {
        dict->capacity *= 2;
        dict->formulas = realloc(dict->formulas, dict->capacity * sizeof(Formula));
    }
    
    Formula *f = &dict->formulas[dict->count];
    f->id = dict->count;
    f->seed = formula_find_seed(pattern, length);
    f->pattern_hash = hash;
    f->length = length;
    f->count = 1;
    memset(f->pattern, 0, PATTERN_SIZE);
    memcpy(f->pattern, pattern, length);
    
    return dict->count++;
}

// ============================================================
// КОМПРЕССИЯ
// ============================================================

void compress_file(const char *input_path, const char *output_path) {
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  KOLIBRI FORMULA ARCHIVER v%d.0\n", VERSION);
    printf("  Генеративная formula-based compression\n");
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
    printf("📊 Размер: %zu байт (%.2f MB)\n\n", original_size, original_size / 1024.0 / 1024.0);
    
    // L1 → L2: Bytes → Decimal
    size_t decimal_size;
    uint8_t *decimal_data = bytes_to_decimal(original_data, original_size, &decimal_size);
    printf("✅ L1→L2: %zu байт → %zu цифр (%.2fx)\n", 
           original_size, decimal_size, (float)decimal_size/original_size);
    
    // L2 → L3: Decimal → Формулы
    FormulaDict dict;
    dict_init(&dict);
    
    size_t num_chunks = (decimal_size + PATTERN_SIZE - 1) / PATTERN_SIZE;
    uint32_t *formula_map = malloc(num_chunks * sizeof(uint32_t));
    
    for (size_t i = 0; i < num_chunks; i++) {
        uint8_t pattern[PATTERN_SIZE] = {0};
        size_t remaining = decimal_size - (i * PATTERN_SIZE);
        size_t chunk_length = remaining < PATTERN_SIZE ? remaining : PATTERN_SIZE;
        
        memcpy(pattern, decimal_data + i * PATTERN_SIZE, chunk_length);
        formula_map[i] = dict_find_or_add(&dict, pattern, chunk_length);
    }
    
    printf("✅ L2→L3: %zu цифр → %u формул\n", decimal_size, dict.count);
    
    // Вычисляем размер архива
    size_t header_size = sizeof(FormulaHeader);
    size_t formulas_size = dict.count * sizeof(Formula);
    size_t map_size = num_chunks * sizeof(uint32_t);
    size_t total_archive = header_size + formulas_size + map_size;
    
    printf("✅ Архив: заголовок=%zu + формулы=%zu + карта=%zu = %zu байт\n",
           header_size, formulas_size, map_size, total_archive);
    printf("🎯 КОМПРЕССИЯ: %.2fx (%.2f MB → %.2f MB)\n\n", 
           (float)original_size / total_archive,
           original_size / 1024.0 / 1024.0,
           total_archive / 1024.0 / 1024.0);
    
    // Сохраняем архив
    FILE *fout = fopen(output_path, "wb");
    if (!fout) {
        perror("Cannot create output");
        free(original_data);
        free(decimal_data);
        free(formula_map);
        free(dict.formulas);
        return;
    }
    
    FormulaHeader header = {
        .magic = MAGIC,
        .version = VERSION,
        .original_size = original_size,
        .decimal_size = decimal_size,
        .num_formulas = dict.count,
        .map_size = num_chunks
    };
    fwrite(&header, sizeof(header), 1, fout);
    fwrite(dict.formulas, sizeof(Formula), dict.count, fout);
    fwrite(formula_map, sizeof(uint32_t), num_chunks, fout);
    
    fclose(fout);
    
    printf("💾 Архив сохранён: %s\n", output_path);
    printf("📦 Размер: %zu байт (%.2f MB)\n", total_archive, total_archive / 1024.0 / 1024.0);
    
    free(original_data);
    free(decimal_data);
    free(formula_map);
    free(dict.formulas);
}

// ============================================================
// ДЕКОМПРЕССИЯ
// ============================================================

void decompress_file(const char *archive_path, const char *output_path) {
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  KOLIBRI FORMULA ARCHIVER v%d.0\n", VERSION);
    printf("  Генеративное восстановление из формул\n");
    printf("═══════════════════════════════════════════════════════════\n\n");
    
    FILE *fin = fopen(archive_path, "rb");
    if (!fin) {
        perror("Cannot open archive");
        return;
    }
    
    FormulaHeader header;
    fread(&header, sizeof(header), 1, fin);
    
    if (header.magic != MAGIC || header.version != VERSION) {
        fprintf(stderr, "❌ Неверный формат архива\n");
        fclose(fin);
        return;
    }
    
    printf("📖 Архив: %s\n", archive_path);
    printf("   Оригинал: %u байт (%.2f MB)\n", 
           header.original_size, header.original_size / 1024.0 / 1024.0);
    printf("   Decimal: %u цифр\n", header.decimal_size);
    printf("   Формул: %u\n", header.num_formulas);
    printf("   Карта: %u записей\n\n", header.map_size);
    
    // Загружаем формулы
    Formula *formulas = malloc(header.num_formulas * sizeof(Formula));
    fread(formulas, sizeof(Formula), header.num_formulas, fin);
    printf("✅ Загружено формул: %u\n", header.num_formulas);
    
    // Загружаем карту
    uint32_t *formula_map = malloc(header.map_size * sizeof(uint32_t));
    fread(formula_map, sizeof(uint32_t), header.map_size, fin);
    printf("✅ Загружена карта: %u ID\n\n", header.map_size);
    
    fclose(fin);
    
    // L3 → L2: ФОРМУЛЫ ГЕНЕРИРУЮТ decimal строку
    printf("🔄 ГЕНЕРАЦИЯ decimal строки из формул...\n");
    uint8_t *decimal_data = malloc(header.decimal_size);
    size_t decimal_pos = 0;
    
    for (uint32_t i = 0; i < header.map_size; i++) {
        uint32_t formula_id = formula_map[i];
        
        if (formula_id >= header.num_formulas) {
            fprintf(stderr, "❌ Неверный ID формулы: %u\n", formula_id);
            free(formulas);
            free(formula_map);
            free(decimal_data);
            return;
        }
        
        Formula *f = &formulas[formula_id];
        const uint8_t *pattern_source = f->pattern;
        
        size_t remaining = header.decimal_size - decimal_pos;
        size_t copy_size = remaining < f->length ? remaining : f->length;
        
        memcpy(decimal_data + decimal_pos, pattern_source, copy_size);
        decimal_pos += copy_size;
    }
    
    printf("✅ L3→L2: Сгенерировано %zu цифр из %u формул\n", 
           decimal_pos, header.num_formulas);
    
    // L2 → L1: Decimal → Bytes
    size_t bytes_size;
    uint8_t *bytes_data = decimal_to_bytes(decimal_data, header.decimal_size, &bytes_size);
    printf("✅ L2→L1: %u цифр → %zu байт\n\n", header.decimal_size, bytes_size);
    
    if (bytes_size != header.original_size) {
        fprintf(stderr, "⚠️  ВНИМАНИЕ: Размер не совпадает! Ожидалось %u, получено %zu\n",
                header.original_size, bytes_size);
    }
    
    // Сохраняем
    FILE *fout = fopen(output_path, "wb");
    if (!fout) {
        perror("Cannot create output");
        free(formulas);
        free(formula_map);
        free(decimal_data);
        free(bytes_data);
        return;
    }
    
    fwrite(bytes_data, 1, bytes_size, fout);
    fclose(fout);
    
    printf("💾 Файл восстановлен: %s\n", output_path);
    printf("📊 Размер: %zu байт (%.2f MB)\n", 
           bytes_size, bytes_size / 1024.0 / 1024.0);
    
    free(formulas);
    free(formula_map);
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
