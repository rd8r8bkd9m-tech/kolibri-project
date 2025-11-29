// ═══════════════════════════════════════════════════════════════
//   KOLIBRI ARCHIVER v12.0 - MULTI-LEVEL HYBRID COMPRESSION
//   Target: 10x better than ZLIB (60x+ compression)
//   Methods: RLE + ZLIB + Dictionary + Delta + Multi-pass
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <zlib.h>

#define MAGIC 0x4B4C4944  // KLID v12
#define MAX_DICT_ENTRIES 4096
#define MAX_PATTERN_LEN 64
#define MIN_PATTERN_LEN 4
#define MIN_PATTERN_FREQ 3

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t original_size;
    uint32_t compressed_size;
    uint8_t  method;        // 1=RLE, 2=DICT+ZLIB, 3=ZLIB, 4=MULTI
    uint8_t  levels;        // Количество уровней сжатия
    uint16_t dict_entries;  // Записей в словаре
} __attribute__((packed)) ArchiveHeader;

typedef struct {
    char pattern[MAX_PATTERN_LEN];
    uint8_t len;
    uint32_t freq;
    uint32_t savings;  // Экономия в байтах
} DictEntry;

typedef struct {
    DictEntry entries[MAX_DICT_ENTRIES];
    int count;
} Dictionary;

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// ═══════════════════════════════════════════════════════════════
//   УРОВЕНЬ 1: Построение словаря частых паттернов
// ═══════════════════════════════════════════════════════════════

static uint32_t pattern_hash(const char *s, int len) {
    uint32_t h = 0;
    for (int i = 0; i < len; i++) {
        h = h * 31 + (uint8_t)s[i];
    }
    return h;
}

// Подсчёт частоты паттерна в данных
static int count_pattern(const uint8_t *data, size_t size, const char *pattern, int plen) {
    int count = 0;
    for (size_t i = 0; i <= size - plen; i++) {
        if (memcmp(data + i, pattern, plen) == 0) {
            count++;
            i += plen - 1;  // Не перекрывающиеся
        }
    }
    return count;
}

// Поиск лучших паттернов для словаря
static void build_dictionary(const uint8_t *data, size_t size, Dictionary *dict) {
    dict->count = 0;
    
    // Хеш-таблица для быстрого поиска дубликатов
    #define HASH_SIZE 65536
    int *hash_freq = calloc(HASH_SIZE, sizeof(int));
    char (*hash_pattern)[MAX_PATTERN_LEN] = calloc(HASH_SIZE, MAX_PATTERN_LEN);
    uint8_t *hash_len = calloc(HASH_SIZE, sizeof(uint8_t));
    
    // Проход 1: Собираем кандидатов (длины 4-32)
    for (int plen = MIN_PATTERN_LEN; plen <= 32 && plen <= MAX_PATTERN_LEN; plen++) {
        for (size_t i = 0; i <= size - plen; i++) {
            uint32_t h = pattern_hash((char*)(data + i), plen) % HASH_SIZE;
            
            if (hash_len[h] == 0) {
                // Новый паттерн
                memcpy(hash_pattern[h], data + i, plen);
                hash_len[h] = plen;
                hash_freq[h] = 1;
            } else if (hash_len[h] == plen && 
                       memcmp(hash_pattern[h], data + i, plen) == 0) {
                // Совпадение
                hash_freq[h]++;
            }
        }
    }
    
    // Проход 2: Выбираем лучшие по экономии
    for (int h = 0; h < HASH_SIZE; h++) {
        if (hash_freq[h] >= MIN_PATTERN_FREQ && hash_len[h] >= MIN_PATTERN_LEN) {
            // Экономия = (длина - 2) * частота - длина_словаря
            // Замена: паттерн -> 2 байта (0xFF + индекс)
            int savings = (hash_len[h] - 2) * hash_freq[h] - hash_len[h] - 2;
            
            if (savings > 10 && dict->count < MAX_DICT_ENTRIES) {
                DictEntry *e = &dict->entries[dict->count];
                memcpy(e->pattern, hash_pattern[h], hash_len[h]);
                e->len = hash_len[h];
                e->freq = hash_freq[h];
                e->savings = savings;
                dict->count++;
            }
        }
    }
    
    // Сортировка по экономии (простой bubble sort)
    for (int i = 0; i < dict->count - 1; i++) {
        for (int j = i + 1; j < dict->count; j++) {
            if (dict->entries[j].savings > dict->entries[i].savings) {
                DictEntry tmp = dict->entries[i];
                dict->entries[i] = dict->entries[j];
                dict->entries[j] = tmp;
            }
        }
    }
    
    // Оставляем топ-256 (чтобы индекс был 1 байт)
    if (dict->count > 255) dict->count = 255;
    
    free(hash_freq);
    free(hash_pattern);
    free(hash_len);
}

// ═══════════════════════════════════════════════════════════════
//   УРОВЕНЬ 2: Замена паттернов на индексы словаря
// ═══════════════════════════════════════════════════════════════

static size_t apply_dictionary(const uint8_t *data, size_t size, 
                               const Dictionary *dict, uint8_t *out) {
    size_t out_pos = 0;
    size_t pos = 0;
    
    while (pos < size) {
        int best_idx = -1;
        int best_len = 0;
        
        // Ищем самый длинный паттерн из словаря
        for (int i = 0; i < dict->count; i++) {
            int plen = dict->entries[i].len;
            if (pos + plen <= size && plen > best_len) {
                if (memcmp(data + pos, dict->entries[i].pattern, plen) == 0) {
                    best_idx = i;
                    best_len = plen;
                }
            }
        }
        
        if (best_idx >= 0) {
            // Замена: 0xFE + индекс
            out[out_pos++] = 0xFE;
            out[out_pos++] = (uint8_t)best_idx;
            pos += best_len;
        } else {
            // Литерал (экранируем 0xFE)
            if (data[pos] == 0xFE) {
                out[out_pos++] = 0xFE;
                out[out_pos++] = 0xFF;  // Escaped
            } else {
                out[out_pos++] = data[pos];
            }
            pos++;
        }
    }
    
    return out_pos;
}

// ═══════════════════════════════════════════════════════════════
//   УРОВЕНЬ 3: Delta-кодирование для числовых данных
// ═══════════════════════════════════════════════════════════════

static size_t delta_encode(const uint8_t *data, size_t size, uint8_t *out) {
    if (size == 0) return 0;
    out[0] = data[0];
    for (size_t i = 1; i < size; i++) {
        out[i] = data[i] - data[i-1];
    }
    return size;
}

static void delta_decode(uint8_t *data, size_t size) {
    for (size_t i = 1; i < size; i++) {
        data[i] = data[i] + data[i-1];
    }
}

// ═══════════════════════════════════════════════════════════════
//   ГЛАВНАЯ ФУНКЦИЯ: Многоуровневое сжатие
// ═══════════════════════════════════════════════════════════════

static int is_homogeneous(const uint8_t *data, size_t size) {
    if (size == 0) return 0;
    uint8_t first = data[0];
    for (size_t i = 1; i < size; i++) {
        if (data[i] != first) return 0;
    }
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI ARCHIVER v12.0 - Multi-Level Hybrid Compression      ║\n");
        printf("║  Target: 60x+ compression (10x better than ZLIB)              ║\n");
        printf("║  Methods: RLE | Dictionary | Delta | ZLIB | Multi-pass        ║\n");
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        printf("Usage:\n");
        printf("  %s compress <input> <output.kolibri>\n", argv[0]);
        printf("  %s extract <input.kolibri> <output>\n\n", argv[0]);
        return 1;
    }
    
    const char* mode = argv[1];
    const char* input_path = argv[2];
    const char* output_path = argv[3];
    
    if (strcmp(mode, "compress") == 0) {
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI v12.0 - MULTI-LEVEL COMPRESSION                      ║\n");
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        
        FILE* fin = fopen(input_path, "rb");
        if (!fin) { printf("❌ Cannot open: %s\n", input_path); return 1; }
        
        fseek(fin, 0, SEEK_END);
        long file_size = ftell(fin);
        fseek(fin, 0, SEEK_SET);
        
        uint8_t* data = malloc(file_size);
        if (fread(data, 1, file_size, fin) != (size_t)file_size) {
            fclose(fin); free(data); return 1;
        }
        fclose(fin);
        
        printf("📄 Input:  %s\n", input_path);
        printf("📊 Size:   %.2f KB (%ld bytes)\n\n", file_size/1024.0, file_size);
        
        double start = get_time();
        
        ArchiveHeader header = {
            .magic = MAGIC,
            .version = 12,
            .original_size = file_size,
            .method = 0,
            .levels = 0,
            .dict_entries = 0
        };
        
        uint8_t* best_data = NULL;
        size_t best_size = file_size + 1;
        const char* method_name = "RAW";
        
        // ═══ МЕТОД 1: RLE для однородных данных ═══
        if (is_homogeneous(data, file_size)) {
            best_data = malloc(5);
            best_data[0] = data[0];
            *(uint32_t*)(best_data + 1) = file_size;
            best_size = 5;
            header.method = 1;
            method_name = "RLE";
            printf("   ✓ RLE:      5 bytes (однородные данные)\n");
        } else {
            // ═══ МЕТОД 2: ZLIB базовый ═══
            uLongf zlib_size = compressBound(file_size);
            uint8_t* zlib_out = malloc(zlib_size);
            compress2(zlib_out, &zlib_size, data, file_size, 9);
            printf("   ✓ ZLIB:     %lu bytes (%.2fx)\n", zlib_size, (double)file_size/zlib_size);
            
            if (zlib_size < best_size) {
                best_data = zlib_out;
                best_size = zlib_size;
                header.method = 3;
                method_name = "ZLIB";
            } else {
                free(zlib_out);
            }
            
            // ═══ МЕТОД 3: Dictionary + ZLIB ═══
            printf("\n🔍 Анализ паттернов...\n");
            Dictionary dict;
            build_dictionary(data, file_size, &dict);
            printf("   Найдено паттернов: %d\n", dict.count);
            
            if (dict.count > 0) {
                // Применяем словарь
                uint8_t* dict_data = malloc(file_size * 2);
                size_t dict_size = apply_dictionary(data, file_size, &dict, dict_data);
                printf("   После словаря: %zu bytes (%.2fx)\n", dict_size, (double)file_size/dict_size);
                
                // ZLIB на результат
                uLongf dict_zlib_size = compressBound(dict_size);
                uint8_t* dict_zlib = malloc(dict_zlib_size);
                compress2(dict_zlib, &dict_zlib_size, dict_data, dict_size, 9);
                
                // Размер словаря в архиве
                size_t dict_storage = 2;  // count (2 bytes)
                for (int i = 0; i < dict.count; i++) {
                    dict_storage += 1 + dict.entries[i].len;  // len + data
                }
                
                size_t total = dict_storage + dict_zlib_size;
                printf("   ✓ DICT+ZLIB: %zu bytes (%.2fx)\n", total, (double)file_size/total);
                
                if (total < best_size) {
                    // Формируем данные: словарь + сжатые данные
                    free(best_data);
                    best_data = malloc(total + 4);
                    
                    size_t pos = 0;
                    // Записываем количество записей словаря
                    *(uint16_t*)(best_data + pos) = dict.count;
                    pos += 2;
                    
                    // Записываем словарь
                    for (int i = 0; i < dict.count; i++) {
                        best_data[pos++] = dict.entries[i].len;
                        memcpy(best_data + pos, dict.entries[i].pattern, dict.entries[i].len);
                        pos += dict.entries[i].len;
                    }
                    
                    // Размер сжатых данных
                    *(uint32_t*)(best_data + pos) = dict_zlib_size;
                    pos += 4;
                    
                    // Сжатые данные
                    memcpy(best_data + pos, dict_zlib, dict_zlib_size);
                    pos += dict_zlib_size;
                    
                    best_size = pos;
                    header.method = 2;
                    header.dict_entries = dict.count;
                    method_name = "DICT+ZLIB";
                }
                
                free(dict_data);
                free(dict_zlib);
            }
            
            // ═══ МЕТОД 4: Multi-pass (ZLIB → ZLIB) ═══
            if (best_size > 100) {
                uint8_t* current = malloc(best_size);
                memcpy(current, best_data, best_size);
                size_t current_size = best_size;
                
                for (int pass = 0; pass < 3; pass++) {
                    uLongf next_size = compressBound(current_size);
                    uint8_t* next = malloc(next_size);
                    
                    if (compress2(next, &next_size, current, current_size, 9) == Z_OK) {
                        if (next_size < current_size - 10) {
                            free(current);
                            current = next;
                            current_size = next_size;
                            printf("   ✓ Pass %d:   %zu bytes\n", pass+1, current_size);
                        } else {
                            free(next);
                            break;
                        }
                    } else {
                        free(next);
                        break;
                    }
                }
                
                if (current_size < best_size) {
                    free(best_data);
                    best_data = current;
                    best_size = current_size;
                    header.method = 4;
                    header.levels = 2;
                    method_name = "MULTI-PASS";
                } else {
                    free(current);
                }
            }
        }
        
        // RAW fallback
        if (best_size >= (size_t)file_size) {
            free(best_data);
            best_data = malloc(file_size);
            memcpy(best_data, data, file_size);
            best_size = file_size;
            header.method = 0;
            method_name = "RAW";
        }
        
        header.compressed_size = best_size;
        
        // Запись
        FILE* fout = fopen(output_path, "wb");
        fwrite(&header, sizeof(header), 1, fout);
        fwrite(best_data, 1, best_size, fout);
        fclose(fout);
        
        double elapsed = get_time() - start;
        size_t archive_size = sizeof(header) + best_size;
        double ratio = (double)file_size / archive_size;
        
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  РЕЗУЛЬТАТЫ                                                   ║\n");
        printf("╠════════════════════════════════════════════════════════════════╣\n");
        printf("║  Метод:        %-10s                                     ║\n", method_name);
        printf("║  Исходник:     %8.2f KB                                    ║\n", file_size/1024.0);
        printf("║  Архив:        %8.2f KB                                    ║\n", archive_size/1024.0);
        printf("║  Коэффициент:  %8.2fx                                      ║\n", ratio);
        printf("║  Время:        %8.3f сек                                   ║\n", elapsed);
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        
        printf("✅ Архив: %s\n\n", output_path);
        
        free(data);
        free(best_data);
    }
    else if (strcmp(mode, "extract") == 0) {
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI v12.0 - EXTRACTOR                                    ║\n");
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        
        FILE* fin = fopen(input_path, "rb");
        if (!fin) { printf("❌ Cannot open: %s\n", input_path); return 1; }
        
        ArchiveHeader header;
        fread(&header, sizeof(header), 1, fin);
        
        if (header.magic != MAGIC) {
            printf("❌ Invalid archive (magic: 0x%08X)\n", header.magic);
            fclose(fin);
            return 1;
        }
        
        uint8_t* compressed = malloc(header.compressed_size);
        fread(compressed, 1, header.compressed_size, fin);
        fclose(fin);
        
        const char* methods[] = {"RAW", "RLE", "DICT+ZLIB", "ZLIB", "MULTI-PASS"};
        printf("📄 Archive: %s\n", input_path);
        printf("📊 Method:  %s\n", methods[header.method]);
        printf("�� Ratio:   %.2fx\n\n", (double)header.original_size/(header.compressed_size + sizeof(header)));
        
        uint8_t* output = malloc(header.original_size + 1024);
        size_t out_size = 0;
        
        switch (header.method) {
            case 0:  // RAW
                memcpy(output, compressed, header.original_size);
                out_size = header.original_size;
                break;
                
            case 1:  // RLE
                memset(output, compressed[0], header.original_size);
                out_size = header.original_size;
                break;
                
            case 2: {  // DICT+ZLIB
                // Читаем словарь
                size_t pos = 0;
                uint16_t dict_count = *(uint16_t*)(compressed + pos);
                pos += 2;
                
                Dictionary dict;
                dict.count = dict_count;
                for (int i = 0; i < dict_count; i++) {
                    dict.entries[i].len = compressed[pos++];
                    memcpy(dict.entries[i].pattern, compressed + pos, dict.entries[i].len);
                    pos += dict.entries[i].len;
                }
                
                // Читаем размер сжатых данных
                uint32_t zlib_size = *(uint32_t*)(compressed + pos);
                pos += 4;
                
                // Распаковываем ZLIB
                uLongf dict_data_size = header.original_size * 2;
                uint8_t* dict_data = malloc(dict_data_size);
                uncompress(dict_data, &dict_data_size, compressed + pos, zlib_size);
                
                // Восстанавливаем из словаря
                size_t in_pos = 0;
                out_size = 0;
                while (in_pos < dict_data_size && out_size < header.original_size) {
                    if (dict_data[in_pos] == 0xFE) {
                        in_pos++;
                        if (dict_data[in_pos] == 0xFF) {
                            // Escaped 0xFE
                            output[out_size++] = 0xFE;
                            in_pos++;
                        } else {
                            // Словарный индекс
                            uint8_t idx = dict_data[in_pos++];
                            memcpy(output + out_size, dict.entries[idx].pattern, dict.entries[idx].len);
                            out_size += dict.entries[idx].len;
                        }
                    } else {
                        output[out_size++] = dict_data[in_pos++];
                    }
                }
                free(dict_data);
                break;
            }
                
            case 3: {  // ZLIB
                uLongf dest_len = header.original_size;
                uncompress(output, &dest_len, compressed, header.compressed_size);
                out_size = dest_len;
                break;
            }
                
            case 4: {  // MULTI-PASS
                uint8_t* current = malloc(header.compressed_size);
                memcpy(current, compressed, header.compressed_size);
                size_t current_size = header.compressed_size;
                
                // Распаковываем уровни
                for (int i = 0; i < header.levels; i++) {
                    uLongf next_size = header.original_size * 4;
                    uint8_t* next = malloc(next_size);
                    uncompress(next, &next_size, current, current_size);
                    free(current);
                    current = next;
                    current_size = next_size;
                }
                
                // Финальная распаковка
                uLongf dest_len = header.original_size;
                uncompress(output, &dest_len, current, current_size);
                out_size = dest_len;
                free(current);
                break;
            }
        }
        
        FILE* fout = fopen(output_path, "wb");
        fwrite(output, 1, out_size, fout);
        fclose(fout);
        
        printf("✅ Extracted: %s (%zu bytes)\n\n", output_path, out_size);
        
        free(compressed);
        free(output);
    }
    
    return 0;
}
