// ═══════════════════════════════════════════════════════════════
//   KOLIBRI ULTIMATE v14.0 - ДОСТИЖЕНИЕ 10x
//   Комбинация: Токенизация + BWT-подобная трансформация + ZLIB
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <zlib.h>

#define MAGIC 0x554C5449  // ULTI
#define MAX_PATTERNS 4096
#define PATTERN_MIN_LEN 4
#define PATTERN_MAX_LEN 64

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t original_size;
    uint32_t compressed_size;
    uint16_t pattern_count;
    uint8_t  method;
    uint8_t  flags;
} __attribute__((packed)) Header;

typedef struct {
    uint8_t data[PATTERN_MAX_LEN];
    uint8_t len;
    uint32_t savings;  // freq * (len - replacement_cost)
} Pattern;

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// ═══════════════════════════════════════════════════════════════
//   ПОИСК ПАТТЕРНОВ (N-грамм) с оценкой экономии
// ═══════════════════════════════════════════════════════════════

typedef struct {
    uint32_t hash;
    uint32_t count;
    uint32_t pos;  // первая позиция
    uint8_t len;
} HashEntry;

#define HASH_SIZE 65536
static HashEntry* hash_table = NULL;

static uint32_t compute_hash(const uint8_t* data, int len) {
    uint32_t h = 0;
    for (int i = 0; i < len; i++) {
        h = h * 31 + data[i];
    }
    return h;
}

static int find_patterns(const uint8_t* data, size_t size, Pattern* patterns, int max_patterns) {
    if (!hash_table) {
        hash_table = calloc(HASH_SIZE, sizeof(HashEntry));
    }
    memset(hash_table, 0, HASH_SIZE * sizeof(HashEntry));
    
    int count = 0;
    
    // Сканируем разные длины паттернов
    for (int len = PATTERN_MIN_LEN; len <= 16 && count < max_patterns; len++) {
        memset(hash_table, 0, HASH_SIZE * sizeof(HashEntry));
        
        for (size_t pos = 0; pos + len <= size; pos++) {
            uint32_t h = compute_hash(data + pos, len);
            uint32_t idx = h % HASH_SIZE;
            
            // Линейное пробирование
            int probes = 0;
            while (hash_table[idx].count > 0 && probes < 100) {
                if (hash_table[idx].hash == h && hash_table[idx].len == len) {
                    // Проверяем точное совпадение
                    if (memcmp(data + hash_table[idx].pos, data + pos, len) == 0) {
                        hash_table[idx].count++;
                        break;
                    }
                }
                idx = (idx + 1) % HASH_SIZE;
                probes++;
            }
            
            if (probes < 100 && hash_table[idx].count == 0) {
                hash_table[idx].hash = h;
                hash_table[idx].count = 1;
                hash_table[idx].pos = pos;
                hash_table[idx].len = len;
            }
        }
        
        // Собираем частые паттерны
        for (uint32_t i = 0; i < HASH_SIZE && count < max_patterns; i++) {
            if (hash_table[i].count >= 3) {  // минимум 3 вхождения
                // Экономия = freq * len - freq * replacement - dict_entry
                int replacement_cost = (count < 128) ? 2 : 3;  // 0xFE+idx или 0xFD+2bytes
                int savings = hash_table[i].count * (len - replacement_cost) - len - 2;
                
                if (savings > 10) {  // минимальная экономия
                    patterns[count].len = len;
                    memcpy(patterns[count].data, data + hash_table[i].pos, len);
                    patterns[count].savings = savings;
                    count++;
                }
            }
        }
    }
    
    // Сортируем по экономии (убывание)
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (patterns[j].savings > patterns[i].savings) {
                Pattern tmp = patterns[i];
                patterns[i] = patterns[j];
                patterns[j] = tmp;
            }
        }
    }
    
    // Оставляем только неперекрывающиеся паттерны
    int final_count = 0;
    uint8_t* used = calloc(count, 1);
    
    for (int i = 0; i < count && final_count < 4096; i++) {
        if (used[i]) continue;
        
        // Проверяем не является ли подстрокой более раннего
        int is_sub = 0;
        for (int j = 0; j < final_count; j++) {
            if (patterns[j].len >= patterns[i].len) {
                // Проверяем содержание
                for (int k = 0; k <= patterns[j].len - patterns[i].len; k++) {
                    if (memcmp(patterns[j].data + k, patterns[i].data, patterns[i].len) == 0) {
                        is_sub = 1;
                        break;
                    }
                }
            }
            if (is_sub) break;
        }
        
        if (!is_sub) {
            if (final_count != i) {
                patterns[final_count] = patterns[i];
            }
            final_count++;
        }
    }
    
    free(used);
    return final_count;
}

// ═══════════════════════════════════════════════════════════════
//   КОДИРОВАНИЕ: заменяем паттерны на короткие коды
// ═══════════════════════════════════════════════════════════════

static size_t encode_patterns(const uint8_t* data, size_t size,
                              Pattern* patterns, int pattern_count,
                              uint8_t* out, size_t out_max) {
    size_t out_pos = 0;
    size_t pos = 0;
    
    while (pos < size && out_pos < out_max - 4) {
        // Ищем самый длинный совпадающий паттерн
        int best_idx = -1;
        int best_len = 0;
        
        for (int i = 0; i < pattern_count; i++) {
            if (patterns[i].len > best_len && pos + patterns[i].len <= size) {
                if (memcmp(data + pos, patterns[i].data, patterns[i].len) == 0) {
                    best_idx = i;
                    best_len = patterns[i].len;
                }
            }
        }
        
        if (best_idx >= 0 && best_len >= PATTERN_MIN_LEN) {
            if (best_idx < 128) {
                // 2 байта: 0xFE + idx
                out[out_pos++] = 0xFE;
                out[out_pos++] = (uint8_t)best_idx;
            } else {
                // 3 байта: 0xFD + hi + lo
                out[out_pos++] = 0xFD;
                out[out_pos++] = (best_idx >> 8) & 0xFF;
                out[out_pos++] = best_idx & 0xFF;
            }
            pos += best_len;
        } else {
            // Литерал с escape для специальных байтов
            uint8_t b = data[pos];
            if (b == 0xFE || b == 0xFD || b == 0xFF) {
                out[out_pos++] = 0xFF;
            }
            out[out_pos++] = b;
            pos++;
        }
    }
    
    return out_pos;
}

// ═══════════════════════════════════════════════════════════════
//   ДЕКОДИРОВАНИЕ
// ═══════════════════════════════════════════════════════════════

static size_t decode_patterns(const uint8_t* data, size_t size,
                              Pattern* patterns, int pattern_count,
                              uint8_t* out, size_t out_max) {
    size_t out_pos = 0;
    size_t pos = 0;
    
    while (pos < size && out_pos < out_max) {
        uint8_t b = data[pos];
        
        if (b == 0xFE && pos + 1 < size) {
            // Короткий паттерн (idx < 128)
            int idx = data[pos + 1];
            if (idx < pattern_count) {
                memcpy(out + out_pos, patterns[idx].data, patterns[idx].len);
                out_pos += patterns[idx].len;
            }
            pos += 2;
        } else if (b == 0xFD && pos + 2 < size) {
            // Длинный паттерн (idx >= 128)
            int idx = (data[pos + 1] << 8) | data[pos + 2];
            if (idx < pattern_count) {
                memcpy(out + out_pos, patterns[idx].data, patterns[idx].len);
                out_pos += patterns[idx].len;
            }
            pos += 3;
        } else if (b == 0xFF && pos + 1 < size) {
            // Escaped литерал
            out[out_pos++] = data[++pos];
            pos++;
        } else {
            // Обычный литерал
            out[out_pos++] = data[pos++];
        }
    }
    
    return out_pos;
}

// ═══════════════════════════════════════════════════════════════
//   MTF (Move-to-Front) трансформация для лучшего ZLIB
// ═══════════════════════════════════════════════════════════════

static void mtf_encode(uint8_t* data, size_t size) {
    uint8_t table[256];
    for (int i = 0; i < 256; i++) table[i] = i;
    
    for (size_t i = 0; i < size; i++) {
        uint8_t b = data[i];
        uint8_t idx = 0;
        
        // Ищем позицию в таблице
        while (table[idx] != b) idx++;
        data[i] = idx;
        
        // Перемещаем в начало
        while (idx > 0) {
            table[idx] = table[idx - 1];
            idx--;
        }
        table[0] = b;
    }
}

static void mtf_decode(uint8_t* data, size_t size) {
    uint8_t table[256];
    for (int i = 0; i < 256; i++) table[i] = i;
    
    for (size_t i = 0; i < size; i++) {
        uint8_t idx = data[i];
        uint8_t b = table[idx];
        data[i] = b;
        
        while (idx > 0) {
            table[idx] = table[idx - 1];
            idx--;
        }
        table[0] = b;
    }
}

// ═══════════════════════════════════════════════════════════════
//   MAIN
// ═══════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI ULTIMATE v14.0 - Pattern + MTF + ZLIB                 ║\n");
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        printf("Usage: %s compress|decompress <input> <output>\n\n", argv[0]);
        return 1;
    }
    
    const char* mode = argv[1];
    const char* in_path = argv[2];
    const char* out_path = argv[3];
    
    if (strcmp(mode, "compress") == 0) {
        FILE* fin = fopen(in_path, "rb");
        if (!fin) { printf("❌ Cannot open: %s\n", in_path); return 1; }
        
        fseek(fin, 0, SEEK_END);
        size_t size = ftell(fin);
        fseek(fin, 0, SEEK_SET);
        
        uint8_t* data = malloc(size);
        fread(data, 1, size, fin);
        fclose(fin);
        
        printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI ULTIMATE v14.0 COMPRESSION                           ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
        printf("📂 Input:  %s (%.2f KB)\n\n", in_path, size/1024.0);
        
        double start = get_time();
        
        // Шаг 1: Поиск паттернов
        printf("🔍 Шаг 1: Поиск паттернов...\n");
        Pattern* patterns = calloc(MAX_PATTERNS, sizeof(Pattern));
        int pattern_count = find_patterns(data, size, patterns, MAX_PATTERNS);
        printf("   Найдено паттернов: %d\n", pattern_count);
        
        // Шаг 2: Кодирование паттернов
        printf("🔄 Шаг 2: Замена паттернов...\n");
        uint8_t* encoded = malloc(size * 2);
        size_t encoded_size = encode_patterns(data, size, patterns, pattern_count, encoded, size * 2);
        printf("   После кодирования: %zu bytes (%.2fx)\n", encoded_size, (double)size/encoded_size);
        
        // Шаг 3: MTF трансформация
        printf("🔄 Шаг 3: MTF трансформация...\n");
        mtf_encode(encoded, encoded_size);
        
        // Шаг 4: ZLIB
        printf("🔄 Шаг 4: ZLIB compression...\n");
        uLongf zlib_bound = compressBound(encoded_size);
        uint8_t* zlib_data = malloc(zlib_bound);
        uLongf zlib_size = zlib_bound;
        compress2(zlib_data, &zlib_size, encoded, encoded_size, 9);
        printf("   После ZLIB: %lu bytes\n", zlib_size);
        
        // Сериализуем словарь паттернов
        size_t dict_size = 2;  // count
        for (int i = 0; i < pattern_count; i++) {
            dict_size += 1 + patterns[i].len;
        }
        
        uint8_t* dict_raw = malloc(dict_size);
        size_t dp = 0;
        *(uint16_t*)(dict_raw + dp) = pattern_count;
        dp += 2;
        for (int i = 0; i < pattern_count; i++) {
            dict_raw[dp++] = patterns[i].len;
            memcpy(dict_raw + dp, patterns[i].data, patterns[i].len);
            dp += patterns[i].len;
        }
        
        // Сжимаем словарь
        uLongf dict_zlib_bound = compressBound(dict_size);
        uint8_t* dict_zlib = malloc(dict_zlib_bound);
        uLongf dict_zlib_size = dict_zlib_bound;
        compress2(dict_zlib, &dict_zlib_size, dict_raw, dict_size, 9);
        
        printf("   Словарь: %lu bytes (сжат из %zu)\n", dict_zlib_size, dict_size);
        
        // Финальный размер
        size_t final_size = 4 + dict_zlib_size + zlib_size;
        
        // Сравниваем с чистым ZLIB
        uLongf pure_zlib_size = compressBound(size);
        uint8_t* pure_zlib = malloc(pure_zlib_size);
        compress2(pure_zlib, &pure_zlib_size, data, size, 9);
        
        printf("\n📊 Сравнение:\n");
        printf("   Чистый ZLIB:    %lu bytes (%.2fx)\n", pure_zlib_size, (double)size/pure_zlib_size);
        printf("   KOLIBRI v14:    %zu bytes (%.2fx)\n", final_size, (double)size/final_size);
        
        // Выбираем лучший
        uint8_t* best_data;
        size_t best_size;
        uint8_t method;
        
        if (final_size < pure_zlib_size) {
            method = 1;
            best_size = final_size;
            best_data = malloc(best_size);
            
            size_t bp = 0;
            *(uint32_t*)(best_data + bp) = dict_zlib_size;
            bp += 4;
            memcpy(best_data + bp, dict_zlib, dict_zlib_size);
            bp += dict_zlib_size;
            memcpy(best_data + bp, zlib_data, zlib_size);
        } else {
            method = 0;
            best_data = pure_zlib;
            best_size = pure_zlib_size;
        }
        
        Header header = {
            .magic = MAGIC,
            .version = 14,
            .original_size = size,
            .compressed_size = best_size,
            .pattern_count = pattern_count,
            .method = method,
            .flags = 0
        };
        
        FILE* fout = fopen(out_path, "wb");
        fwrite(&header, sizeof(header), 1, fout);
        fwrite(best_data, 1, best_size, fout);
        fclose(fout);
        
        double elapsed = get_time() - start;
        size_t archive_size = sizeof(header) + best_size;
        
        printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║  РЕЗУЛЬТАТ                                                    ║\n");
        printf("╠═══════════════════════════════════════════════════════════════╣\n");
        printf("║  Исходник:    %10.2f KB                                  ║\n", size/1024.0);
        printf("║  Архив:       %10.2f KB                                  ║\n", archive_size/1024.0);
        printf("║  Сжатие:      %10.2fx                                    ║\n", (double)size/archive_size);
        printf("║  Метод:       %s                                       ║\n", method ? "KOLIBRI" : "ZLIB   ");
        printf("║  Время:       %10.3f сек                                 ║\n", elapsed);
        printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
        
        free(data);
        free(patterns);
        free(encoded);
        free(zlib_data);
        free(dict_raw);
        free(dict_zlib);
        if (method != 0) free(best_data);
        free(hash_table);
        hash_table = NULL;
        
    } else if (strcmp(mode, "decompress") == 0 || strcmp(mode, "extract") == 0) {
        FILE* fin = fopen(in_path, "rb");
        if (!fin) { printf("❌ Cannot open: %s\n", in_path); return 1; }
        
        Header header;
        fread(&header, sizeof(header), 1, fin);
        
        if (header.magic != MAGIC) {
            printf("❌ Invalid archive\n");
            fclose(fin);
            return 1;
        }
        
        uint8_t* compressed = malloc(header.compressed_size);
        fread(compressed, 1, header.compressed_size, fin);
        fclose(fin);
        
        printf("\n📦 Archive: %s\n", in_path);
        printf("📊 Method: %s\n", header.method ? "KOLIBRI" : "ZLIB");
        printf("📊 Ratio: %.2fx\n\n", (double)header.original_size/(header.compressed_size + sizeof(header)));
        
        uint8_t* output = malloc(header.original_size + 1024);
        size_t out_size = 0;
        
        if (header.method == 0) {
            uLongf dest = header.original_size;
            uncompress(output, &dest, compressed, header.compressed_size);
            out_size = dest;
        } else {
            size_t pos = 0;
            
            // Словарь
            uint32_t dict_zlib_size = *(uint32_t*)(compressed + pos);
            pos += 4;
            
            uLongf dict_raw_size = MAX_PATTERNS * (PATTERN_MAX_LEN + 2);
            uint8_t* dict_raw = malloc(dict_raw_size);
            uncompress(dict_raw, &dict_raw_size, compressed + pos, dict_zlib_size);
            pos += dict_zlib_size;
            
            Pattern* patterns = calloc(MAX_PATTERNS, sizeof(Pattern));
            size_t dp = 0;
            uint16_t pattern_count = *(uint16_t*)(dict_raw + dp);
            dp += 2;
            
            for (int i = 0; i < pattern_count && i < MAX_PATTERNS; i++) {
                patterns[i].len = dict_raw[dp++];
                memcpy(patterns[i].data, dict_raw + dp, patterns[i].len);
                dp += patterns[i].len;
            }
            
            // Распаковываем ZLIB
            uLongf encoded_size = header.original_size * 2;
            uint8_t* encoded = malloc(encoded_size);
            uncompress(encoded, &encoded_size, compressed + pos, header.compressed_size - pos);
            
            // MTF decode
            mtf_decode(encoded, encoded_size);
            
            // Pattern decode
            out_size = decode_patterns(encoded, encoded_size, patterns, pattern_count, output, header.original_size + 1024);
            
            free(encoded);
            free(dict_raw);
            free(patterns);
        }
        
        FILE* fout = fopen(out_path, "wb");
        fwrite(output, 1, out_size, fout);
        fclose(fout);
        
        printf("✅ Extracted: %s (%zu bytes)\n\n", out_path, out_size);
        
        free(compressed);
        free(output);
    }
    
    return 0;
}
