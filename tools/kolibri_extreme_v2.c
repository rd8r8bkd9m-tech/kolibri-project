// ═══════════════════════════════════════════════════════════════
//   KOLIBRI EXTREME v13.1 - ТОКЕНИЗАЦИЯ + ZLIB
//   Правильная реализация без конфликтов маркеров
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <zlib.h>

#define MAGIC 0x4B584D45  // KXME
#define MAX_TOKENS 8192
#define MAX_TOKEN_LEN 128

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t original_size;
    uint32_t compressed_size;
    uint16_t token_count;
    uint8_t  method;
    uint8_t  flags;
} __attribute__((packed)) Header;

typedef struct {
    char text[MAX_TOKEN_LEN];
    uint8_t len;
    uint32_t freq;
} Token;

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// ═══════════════════════════════════════════════════════════════
//   ТОКЕНИЗАЦИЯ
// ═══════════════════════════════════════════════════════════════

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
           (c >= '0' && c <= '9') || c == '_';
}

static int tokenize(const uint8_t *data, size_t size, Token *tokens, int max_tokens) {
    int count = 0;
    size_t pos = 0;
    
    #define HASH_SIZE 32768
    int *hash_idx = malloc(HASH_SIZE * sizeof(int));
    memset(hash_idx, -1, HASH_SIZE * sizeof(int));
    
    while (pos < size && count < max_tokens) {
        // Пропускаем пробелы
        while (pos < size && (data[pos] == ' ' || data[pos] == '\t' || 
                              data[pos] == '\n' || data[pos] == '\r')) {
            pos++;
        }
        if (pos >= size) break;
        
        size_t start = pos;
        
        if (is_ident_char(data[pos])) {
            while (pos < size && is_ident_char(data[pos])) pos++;
        } else {
            pos++;
        }
        
        size_t len = pos - start;
        if (len == 0 || len >= MAX_TOKEN_LEN) continue;
        
        // Только длинные токены (3+) имеют смысл
        if (len < 3) continue;
        
        uint32_t h = 0;
        for (size_t i = start; i < pos; i++) h = h * 31 + data[i];
        h %= HASH_SIZE;
        
        int idx = hash_idx[h];
        int found = 0;
        
        while (idx >= 0 && idx < count) {
            if (tokens[idx].len == len && 
                memcmp(tokens[idx].text, data + start, len) == 0) {
                tokens[idx].freq++;
                found = 1;
                break;
            }
            idx = (idx + 1) % max_tokens;
            if (idx == (int)(hash_idx[h])) break;
        }
        
        if (!found && count < max_tokens) {
            tokens[count].len = len;
            memcpy(tokens[count].text, data + start, len);
            tokens[count].text[len] = '\0';
            tokens[count].freq = 1;
            hash_idx[h] = count;
            count++;
        }
    }
    
    free(hash_idx);
    
    // Сортируем по: freq * (len - 2) - чтобы короткие частые не доминировали
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            int score_i = tokens[i].freq * (tokens[i].len > 2 ? tokens[i].len - 2 : 0);
            int score_j = tokens[j].freq * (tokens[j].len > 2 ? tokens[j].len - 2 : 0);
            if (score_j > score_i) {
                Token tmp = tokens[i];
                tokens[i] = tokens[j];
                tokens[j] = tmp;
            }
        }
    }
    
    return count;
}

// ═══════════════════════════════════════════════════════════════
//   КОДИРОВАНИЕ: короткие индексы для частых токенов
// ═══════════════════════════════════════════════════════════════

static size_t encode_tokens(const uint8_t *data, size_t size, 
                            Token *tokens, int token_count,
                            uint8_t *out, size_t out_max) {
    size_t out_pos = 0;
    size_t pos = 0;
    
    while (pos < size && out_pos < out_max - 4) {
        // Ищем совпадение с токеном
        int best_idx = -1;
        int best_len = 0;
        
        // Проверяем только если это начало идентификатора
        if (is_ident_char(data[pos])) {
            for (int i = 0; i < token_count && i < 4096; i++) {
                if (tokens[i].len > best_len && pos + tokens[i].len <= size) {
                    if (memcmp(data + pos, tokens[i].text, tokens[i].len) == 0) {
                        // Проверяем что это полный токен (не часть слова)
                        if (pos + tokens[i].len >= size || !is_ident_char(data[pos + tokens[i].len])) {
                            best_idx = i;
                            best_len = tokens[i].len;
                        }
                    }
                }
            }
        }
        
        if (best_idx >= 0 && best_len >= 3) {
            // Кодируем токен
            if (best_idx < 128) {
                // 1 байт: 0x80 | idx (топ-128 токенов)
                out[out_pos++] = 0x80 | best_idx;
            } else if (best_idx < 4096) {
                // 2 байта: 0xC0 | (idx >> 6), idx & 0x3F (следующие ~4000)
                out[out_pos++] = 0xC0 | ((best_idx >> 6) & 0x3F);
                out[out_pos++] = best_idx & 0x3F;
            }
            pos += best_len;
        } else {
            // Литерал
            if (data[pos] >= 0x80) {
                // Escape для байтов >= 0x80
                out[out_pos++] = 0xFF;
            }
            out[out_pos++] = data[pos++];
        }
    }
    
    return out_pos;
}

// ═══════════════════════════════════════════════════════════════
//   ДЕКОДИРОВАНИЕ
// ═══════════════════════════════════════════════════════════════

static size_t decode_tokens(const uint8_t *data, size_t size,
                            Token *tokens, int token_count,
                            uint8_t *out, size_t out_max) {
    size_t out_pos = 0;
    size_t pos = 0;
    
    while (pos < size && out_pos < out_max) {
        uint8_t b = data[pos];
        
        if (b == 0xFF && pos + 1 < size) {
            // Escaped literal >= 0x80
            out[out_pos++] = data[++pos];
            pos++;
        } else if ((b & 0xC0) == 0x80) {
            // 1-byte token (топ-128)
            int idx = b & 0x7F;
            if (idx < token_count) {
                memcpy(out + out_pos, tokens[idx].text, tokens[idx].len);
                out_pos += tokens[idx].len;
            }
            pos++;
        } else if ((b & 0xC0) == 0xC0 && pos + 1 < size) {
            // 2-byte token (idx 128-4095)
            int idx = ((b & 0x3F) << 6) | (data[pos + 1] & 0x3F);
            if (idx < token_count) {
                memcpy(out + out_pos, tokens[idx].text, tokens[idx].len);
                out_pos += tokens[idx].len;
            }
            pos += 2;
        } else {
            // Обычный байт < 0x80
            out[out_pos++] = data[pos++];
        }
    }
    
    return out_pos;
}

// ═══════════════════════════════════════════════════════════════
//   MAIN
// ═══════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI EXTREME v13.1 - Token Compression                     ║\n");
        printf("║  Methods: Tokenization + Multi-pass ZLIB                       ║\n");
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
        printf("║  KOLIBRI EXTREME v13.1 COMPRESSION                            ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
        printf("📂 Input:  %s (%.2f KB)\n\n", in_path, size/1024.0);
        
        double start = get_time();
        
        // Шаг 1: Токенизация
        printf("🔍 Шаг 1: Токенизация...\n");
        Token* tokens = calloc(MAX_TOKENS, sizeof(Token));
        int token_count = tokenize(data, size, tokens, MAX_TOKENS);
        printf("   Найдено уникальных токенов (len>=3): %d\n", token_count);
        
        // Шаг 2: Кодирование токенов
        printf("🔄 Шаг 2: Замена токенов...\n");
        uint8_t* encoded = malloc(size * 2);
        size_t encoded_size = encode_tokens(data, size, tokens, token_count, encoded, size * 2);
        printf("   После токенизации: %zu bytes (%.2fx)\n", encoded_size, (double)size/encoded_size);
        
        // Шаг 3: ZLIB
        printf("🔄 Шаг 3: ZLIB compression...\n");
        uLongf zlib_bound = compressBound(encoded_size);
        uint8_t* zlib_data = malloc(zlib_bound);
        uLongf zlib_size = zlib_bound;
        compress2(zlib_data, &zlib_size, encoded, encoded_size, 9);
        printf("   После ZLIB: %lu bytes\n", zlib_size);
        
        // Шаг 4: Multi-pass
        printf("🔄 Шаг 4: Multi-pass...\n");
        uint8_t* current = zlib_data;
        size_t current_size = zlib_size;
        int passes = 0;
        
        for (int p = 0; p < 5; p++) {
            uLongf next_bound = compressBound(current_size);
            uint8_t* next = malloc(next_bound);
            uLongf next_size = next_bound;
            
            if (compress2(next, &next_size, current, current_size, 9) == Z_OK && 
                next_size < current_size - 16) {
                if (current != zlib_data) free(current);
                current = next;
                current_size = next_size;
                passes++;
                printf("   Pass %d: %zu bytes\n", passes, current_size);
            } else {
                free(next);
                break;
            }
        }
        
        // Сериализуем словарь
        int dict_count = token_count > 4096 ? 4096 : token_count;
        size_t dict_raw_size = 2;  // count (uint16)
        for (int i = 0; i < dict_count; i++) {
            dict_raw_size += 1 + tokens[i].len;  // len + text
        }
        
        uint8_t* dict_raw = malloc(dict_raw_size);
        size_t dp = 0;
        *(uint16_t*)(dict_raw + dp) = dict_count;
        dp += 2;
        for (int i = 0; i < dict_count; i++) {
            dict_raw[dp++] = tokens[i].len;
            memcpy(dict_raw + dp, tokens[i].text, tokens[i].len);
            dp += tokens[i].len;
        }
        
        // Сжимаем словарь
        uLongf dict_zlib_bound = compressBound(dict_raw_size);
        uint8_t* dict_zlib = malloc(dict_zlib_bound);
        uLongf dict_zlib_size = dict_zlib_bound;
        compress2(dict_zlib, &dict_zlib_size, dict_raw, dict_raw_size, 9);
        
        printf("   Словарь: %lu bytes (сжат из %zu)\n", dict_zlib_size, dict_raw_size);
        
        // Финальный размер
        size_t final_size = 4 + dict_zlib_size + current_size;
        
        // Сравниваем с чистым ZLIB
        uLongf pure_zlib_size = compressBound(size);
        uint8_t* pure_zlib = malloc(pure_zlib_size);
        compress2(pure_zlib, &pure_zlib_size, data, size, 9);
        
        printf("\n📊 Сравнение:\n");
        printf("   Чистый ZLIB:    %lu bytes (%.2fx)\n", pure_zlib_size, (double)size/pure_zlib_size);
        printf("   KOLIBRI v13.1:  %zu bytes (%.2fx)\n", final_size, (double)size/final_size);
        
        // Выбираем лучший
        uint8_t* best_data;
        size_t best_size;
        uint8_t method;
        
        if (final_size < pure_zlib_size) {
            method = 1;  // KOLIBRI
            best_size = final_size;
            best_data = malloc(best_size);
            
            size_t bp = 0;
            *(uint32_t*)(best_data + bp) = dict_zlib_size;
            bp += 4;
            memcpy(best_data + bp, dict_zlib, dict_zlib_size);
            bp += dict_zlib_size;
            memcpy(best_data + bp, current, current_size);
        } else {
            method = 0;  // ZLIB
            best_data = pure_zlib;
            best_size = pure_zlib_size;
        }
        
        Header header = {
            .magic = MAGIC,
            .version = 131,  // v13.1
            .original_size = size,
            .compressed_size = best_size,
            .token_count = dict_count,
            .method = method,
            .flags = passes
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
        free(tokens);
        free(encoded);
        if (current != zlib_data) free(current);
        free(zlib_data);
        free(dict_raw);
        free(dict_zlib);
        if (method != 0) free(best_data);
        
    } else if (strcmp(mode, "decompress") == 0 || strcmp(mode, "extract") == 0) {
        FILE* fin = fopen(in_path, "rb");
        if (!fin) { printf("❌ Cannot open: %s\n", in_path); return 1; }
        
        Header header;
        fread(&header, sizeof(header), 1, fin);
        
        if (header.magic != MAGIC) {
            printf("❌ Invalid archive (magic=%08X)\n", header.magic);
            fclose(fin);
            return 1;
        }
        
        uint8_t* compressed = malloc(header.compressed_size);
        fread(compressed, 1, header.compressed_size, fin);
        fclose(fin);
        
        printf("\n📦 Archive: %s\n", in_path);
        printf("📊 Method: %s, Passes: %d\n", header.method ? "KOLIBRI" : "ZLIB", header.flags);
        printf("📊 Ratio: %.2fx\n\n", (double)header.original_size/(header.compressed_size + sizeof(header)));
        
        uint8_t* output = malloc(header.original_size + 1024);
        size_t out_size = 0;
        
        if (header.method == 0) {
            // Чистый ZLIB
            uLongf dest = header.original_size;
            uncompress(output, &dest, compressed, header.compressed_size);
            out_size = dest;
        } else {
            // KOLIBRI
            size_t pos = 0;
            
            // Читаем размер словаря
            uint32_t dict_zlib_size = *(uint32_t*)(compressed + pos);
            pos += 4;
            
            // Распаковываем словарь
            uLongf dict_raw_size = 256 * 1024;  // достаточно для 4K токенов
            uint8_t* dict_raw = malloc(dict_raw_size);
            uncompress(dict_raw, &dict_raw_size, compressed + pos, dict_zlib_size);
            pos += dict_zlib_size;
            
            // Парсим словарь
            Token* tokens = calloc(MAX_TOKENS, sizeof(Token));
            size_t dp = 0;
            uint16_t dict_count = *(uint16_t*)(dict_raw + dp);
            dp += 2;
            
            for (int i = 0; i < dict_count && i < MAX_TOKENS; i++) {
                tokens[i].len = dict_raw[dp++];
                if (tokens[i].len > 0 && tokens[i].len < MAX_TOKEN_LEN) {
                    memcpy(tokens[i].text, dict_raw + dp, tokens[i].len);
                    tokens[i].text[tokens[i].len] = '\0';
                    dp += tokens[i].len;
                }
            }
            
            // Распаковываем данные (multi-pass reverse)
            uint8_t* current = malloc(header.compressed_size - pos);
            memcpy(current, compressed + pos, header.compressed_size - pos);
            size_t current_size = header.compressed_size - pos;
            
            for (int p = 0; p < header.flags; p++) {
                uLongf next_size = header.original_size * 4;
                uint8_t* next = malloc(next_size);
                int ret = uncompress(next, &next_size, current, current_size);
                if (ret != Z_OK) {
                    printf("⚠️ Uncompress pass %d failed: %d\n", p, ret);
                    free(next);
                    break;
                }
                free(current);
                current = next;
                current_size = next_size;
            }
            
            // Финальная распаковка ZLIB
            uLongf encoded_size = header.original_size * 2;
            uint8_t* encoded = malloc(encoded_size);
            int ret = uncompress(encoded, &encoded_size, current, current_size);
            if (ret != Z_OK) {
                printf("⚠️ Final uncompress failed: %d\n", ret);
            }
            
            // Декодируем токены
            out_size = decode_tokens(encoded, encoded_size, tokens, dict_count, output, header.original_size + 1024);
            
            free(current);
            free(encoded);
            free(dict_raw);
            free(tokens);
        }
        
        FILE* fout = fopen(out_path, "wb");
        fwrite(output, 1, out_size, fout);
        fclose(fout);
        
        printf("✅ Extracted: %s (%zu bytes)\n\n", out_path, out_size);
        
        free(compressed);
        free(output);
    } else {
        printf("❌ Unknown mode: %s (use compress/decompress)\n", mode);
        return 1;
    }
    
    return 0;
}
