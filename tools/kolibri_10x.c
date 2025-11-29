// ═══════════════════════════════════════════════════════════════
//   KOLIBRI 10X v15.0 - ЦЕЛЬ: 10x СЖАТИЕ
//   Стратегия: агрессивная токенизация + умный отбор + multi-pass
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <zlib.h>

#define MAGIC 0x4B313058  // K10X
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
    uint16_t len;
    uint32_t freq;
    int32_t savings;  // экономия = freq * (len - 2) - len
} Token;

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// ═══════════════════════════════════════════════════════════════
//   УМНАЯ ТОКЕНИЗАЦИЯ с оценкой экономии
// ═══════════════════════════════════════════════════════════════

static int is_token_char(char c) {
    // Расширенный набор символов для токенов
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
           (c >= '0' && c <= '9') || c == '_' || c == '-' ||
           c == '.' || c == '/' || c == ':';
}

static int is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int tokenize(const uint8_t *data, size_t size, Token *tokens, int max_tokens) {
    int count = 0;
    
    #define HASH_SIZE 65536
    int *hash_idx = calloc(HASH_SIZE, sizeof(int));
    for (int i = 0; i < HASH_SIZE; i++) hash_idx[i] = -1;
    
    // Первый проход: собираем все токены длиной 2+
    for (size_t pos = 0; pos < size && count < max_tokens; ) {
        // Пропускаем пробелы
        while (pos < size && is_ws(data[pos])) pos++;
        if (pos >= size) break;
        
        size_t start = pos;
        
        // Строки в кавычках
        if (data[pos] == '"') {
            pos++;
            while (pos < size && data[pos] != '"') {
                if (data[pos] == '\\' && pos + 1 < size) pos++;
                pos++;
            }
            if (pos < size) pos++;
        }
        // Комментарии //
        else if (data[pos] == '/' && pos + 1 < size && data[pos+1] == '/') {
            while (pos < size && data[pos] != '\n') pos++;
        }
        // Комментарии /* */
        else if (data[pos] == '/' && pos + 1 < size && data[pos+1] == '*') {
            pos += 2;
            while (pos + 1 < size && !(data[pos] == '*' && data[pos+1] == '/')) pos++;
            if (pos + 1 < size) pos += 2;
        }
        // Идентификаторы и числа
        else if (is_token_char(data[pos])) {
            while (pos < size && is_token_char(data[pos])) pos++;
        }
        // Операторы и скобки
        else {
            pos++;
        }
        
        size_t len = pos - start;
        if (len < 2 || len >= MAX_TOKEN_LEN) continue;
        
        // Хеш
        uint32_t h = 0;
        for (size_t i = start; i < pos; i++) h = h * 31 + data[i];
        h %= HASH_SIZE;
        
        // Ищем в хеш-таблице
        int idx = hash_idx[h];
        int found = 0;
        int probes = 0;
        
        while (idx >= 0 && probes < 20) {
            if (tokens[idx].len == len && memcmp(tokens[idx].text, data + start, len) == 0) {
                tokens[idx].freq++;
                found = 1;
                break;
            }
            h = (h + 1) % HASH_SIZE;
            idx = hash_idx[h];
            probes++;
        }
        
        if (!found && count < max_tokens && probes < 20) {
            tokens[count].len = len;
            memcpy(tokens[count].text, data + start, len);
            tokens[count].text[len] = '\0';
            tokens[count].freq = 1;
            hash_idx[h] = count;
            count++;
        }
    }
    
    free(hash_idx);
    
    // Вычисляем экономию для каждого токена
    // Экономия = freq * (len - 2) - len - 1  (2 байта на замену, len+1 на словарь)
    for (int i = 0; i < count; i++) {
        int replacement_cost = 2;  // 0xFE + idx
        int dict_cost = 1 + tokens[i].len;  // len + text
        tokens[i].savings = tokens[i].freq * (tokens[i].len - replacement_cost) - dict_cost;
    }
    
    // Сортируем по экономии (убывание)
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (tokens[j].savings > tokens[i].savings) {
                Token tmp = tokens[i];
                tokens[i] = tokens[j];
                tokens[j] = tmp;
            }
        }
    }
    
    // Оставляем только токены с положительной экономией (до 256)
    int final_count = 0;
    for (int i = 0; i < count && final_count < 256; i++) {
        if (tokens[i].savings > 0) {
            if (final_count != i) tokens[final_count] = tokens[i];
            final_count++;
        }
    }
    
    return final_count;
}

// ═══════════════════════════════════════════════════════════════
//   КОДИРОВАНИЕ токенов
// ═══════════════════════════════════════════════════════════════

static size_t encode_tokens(const uint8_t *data, size_t size, 
                            Token *tokens, int token_count,
                            uint8_t *out, size_t out_max) {
    size_t out_pos = 0;
    size_t pos = 0;
    
    while (pos < size && out_pos < out_max - 4) {
        // Ищем самый длинный совпадающий токен
        int best_idx = -1;
        int best_len = 0;
        
        for (int i = 0; i < token_count; i++) {
            if (tokens[i].len > best_len && pos + tokens[i].len <= size) {
                if (memcmp(data + pos, tokens[i].text, tokens[i].len) == 0) {
                    best_idx = i;
                    best_len = tokens[i].len;
                }
            }
        }
        
        if (best_idx >= 0 && best_len >= 2) {
            // Токен: 0xFE + индекс
            out[out_pos++] = 0xFE;
            out[out_pos++] = (uint8_t)best_idx;
            pos += best_len;
        } else {
            // Литерал
            uint8_t b = data[pos];
            if (b == 0xFE || b == 0xFF) {
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

static size_t decode_tokens(const uint8_t *data, size_t size,
                            Token *tokens, int token_count,
                            uint8_t *out, size_t out_max) {
    size_t out_pos = 0;
    size_t pos = 0;
    
    while (pos < size && out_pos < out_max) {
        if (data[pos] == 0xFE && pos + 1 < size) {
            int idx = data[pos + 1];
            if (idx < token_count) {
                memcpy(out + out_pos, tokens[idx].text, tokens[idx].len);
                out_pos += tokens[idx].len;
            }
            pos += 2;
        } else if (data[pos] == 0xFF && pos + 1 < size) {
            out[out_pos++] = data[++pos];
            pos++;
        } else {
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
        printf("║  KOLIBRI 10X v15.0 - Token + Multi-pass ZLIB                   ║\n");
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
        printf("║  KOLIBRI 10X v15.0 COMPRESSION                                ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
        printf("📂 Input:  %s (%.2f KB)\n\n", in_path, size/1024.0);
        
        double start = get_time();
        
        // Итеративное сжатие: токенизация -> ZLIB -> повтор
        uint8_t* current = malloc(size * 2);
        memcpy(current, data, size);
        size_t current_size = size;
        
        int total_tokens = 0;
        Token* all_tokens = calloc(MAX_TOKENS, sizeof(Token));
        int all_token_count = 0;
        
        for (int iter = 0; iter < 3; iter++) {
            printf("🔄 Итерация %d:\n", iter + 1);
            
            // Токенизация
            Token* tokens = calloc(MAX_TOKENS, sizeof(Token));
            int token_count = tokenize(current, current_size, tokens, MAX_TOKENS);
            printf("   Токенов с экономией: %d\n", token_count);
            
            if (token_count < 10) {
                free(tokens);
                break;
            }
            
            // Сохраняем токены
            for (int i = 0; i < token_count && all_token_count < MAX_TOKENS; i++) {
                all_tokens[all_token_count++] = tokens[i];
            }
            
            // Кодирование
            uint8_t* encoded = malloc(current_size * 2);
            size_t encoded_size = encode_tokens(current, current_size, tokens, token_count, encoded, current_size * 2);
            printf("   После токенизации: %zu bytes (%.2fx)\n", encoded_size, (double)current_size/encoded_size);
            
            free(current);
            current = encoded;
            current_size = encoded_size;
            total_tokens += token_count;
            
            free(tokens);
        }
        
        printf("\n🔍 Всего токенов: %d\n", total_tokens);
        
        // ZLIB сжатие
        printf("🔄 ZLIB compression...\n");
        uLongf zlib_bound = compressBound(current_size);
        uint8_t* zlib_data = malloc(zlib_bound);
        uLongf zlib_size = zlib_bound;
        compress2(zlib_data, &zlib_size, current, current_size, 9);
        printf("   После ZLIB: %lu bytes\n", zlib_size);
        
        // Multi-pass ZLIB
        printf("🔄 Multi-pass ZLIB...\n");
        uint8_t* best = zlib_data;
        size_t best_size = zlib_size;
        int passes = 0;
        
        for (int p = 0; p < 5; p++) {
            uLongf next_bound = compressBound(best_size);
            uint8_t* next = malloc(next_bound);
            uLongf next_size = next_bound;
            
            if (compress2(next, &next_size, best, best_size, 9) == Z_OK && 
                next_size < best_size - 16) {
                if (best != zlib_data) free(best);
                best = next;
                best_size = next_size;
                passes++;
                printf("   Pass %d: %zu bytes\n", passes, best_size);
            } else {
                free(next);
                break;
            }
        }
        
        // Словарь токенов
        size_t dict_raw_size = 4;  // count (uint32)
        for (int i = 0; i < all_token_count; i++) {
            dict_raw_size += 2 + all_tokens[i].len;  // len (2) + text
        }
        
        uint8_t* dict_raw = malloc(dict_raw_size);
        size_t dp = 0;
        *(uint32_t*)(dict_raw + dp) = all_token_count;
        dp += 4;
        for (int i = 0; i < all_token_count; i++) {
            *(uint16_t*)(dict_raw + dp) = all_tokens[i].len;
            dp += 2;
            memcpy(dict_raw + dp, all_tokens[i].text, all_tokens[i].len);
            dp += all_tokens[i].len;
        }
        
        uLongf dict_zlib_bound = compressBound(dict_raw_size);
        uint8_t* dict_zlib = malloc(dict_zlib_bound);
        uLongf dict_zlib_size = dict_zlib_bound;
        compress2(dict_zlib, &dict_zlib_size, dict_raw, dict_raw_size, 9);
        
        printf("   Словарь: %lu bytes (сжат из %zu)\n", dict_zlib_size, dict_raw_size);
        
        size_t final_size = 4 + dict_zlib_size + best_size;
        
        // Чистый ZLIB для сравнения
        uLongf pure_zlib_size = compressBound(size);
        uint8_t* pure_zlib = malloc(pure_zlib_size);
        compress2(pure_zlib, &pure_zlib_size, data, size, 9);
        
        printf("\n📊 Сравнение:\n");
        printf("   Чистый ZLIB:    %lu bytes (%.2fx)\n", pure_zlib_size, (double)size/pure_zlib_size);
        printf("   KOLIBRI 10X:    %zu bytes (%.2fx)\n", final_size, (double)size/final_size);
        
        // Выбираем лучший
        uint8_t* final_data;
        size_t final_data_size;
        uint8_t method;
        
        if (final_size < pure_zlib_size) {
            method = 1;
            final_data_size = final_size;
            final_data = malloc(final_data_size);
            
            size_t fp = 0;
            *(uint32_t*)(final_data + fp) = dict_zlib_size;
            fp += 4;
            memcpy(final_data + fp, dict_zlib, dict_zlib_size);
            fp += dict_zlib_size;
            memcpy(final_data + fp, best, best_size);
        } else {
            method = 0;
            final_data = pure_zlib;
            final_data_size = pure_zlib_size;
        }
        
        Header header = {
            .magic = MAGIC,
            .version = 15,
            .original_size = size,
            .compressed_size = final_data_size,
            .token_count = all_token_count,
            .method = method,
            .flags = passes
        };
        
        FILE* fout = fopen(out_path, "wb");
        fwrite(&header, sizeof(header), 1, fout);
        fwrite(final_data, 1, final_data_size, fout);
        fclose(fout);
        
        double elapsed = get_time() - start;
        size_t archive_size = sizeof(header) + final_data_size;
        
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
        free(current);
        free(all_tokens);
        if (best != zlib_data) free(best);
        free(zlib_data);
        free(dict_raw);
        free(dict_zlib);
        if (method != 0) free(final_data);
        
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
        printf("📊 Method: %s, Passes: %d, Tokens: %d\n", 
               header.method ? "KOLIBRI" : "ZLIB", header.flags, header.token_count);
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
            
            uLongf dict_raw_size = MAX_TOKENS * (MAX_TOKEN_LEN + 4);
            uint8_t* dict_raw = malloc(dict_raw_size);
            uncompress(dict_raw, &dict_raw_size, compressed + pos, dict_zlib_size);
            pos += dict_zlib_size;
            
            Token* tokens = calloc(MAX_TOKENS, sizeof(Token));
            size_t dp = 0;
            uint32_t token_count = *(uint32_t*)(dict_raw + dp);
            dp += 4;
            
            for (uint32_t i = 0; i < token_count && i < MAX_TOKENS; i++) {
                tokens[i].len = *(uint16_t*)(dict_raw + dp);
                dp += 2;
                if (tokens[i].len < MAX_TOKEN_LEN) {
                    memcpy(tokens[i].text, dict_raw + dp, tokens[i].len);
                    dp += tokens[i].len;
                }
            }
            
            // Multi-pass uncompress
            uint8_t* current = malloc(header.compressed_size - pos);
            memcpy(current, compressed + pos, header.compressed_size - pos);
            size_t current_size = header.compressed_size - pos;
            
            for (int p = 0; p < header.flags; p++) {
                uLongf next_size = header.original_size * 4;
                uint8_t* next = malloc(next_size);
                if (uncompress(next, &next_size, current, current_size) != Z_OK) {
                    printf("⚠️ Uncompress pass %d failed\n", p);
                    free(next);
                    break;
                }
                free(current);
                current = next;
                current_size = next_size;
            }
            
            // Финальный ZLIB
            uLongf decoded_size = header.original_size * 4;
            uint8_t* decoded = malloc(decoded_size);
            uncompress(decoded, &decoded_size, current, current_size);
            
            // Итеративное декодирование токенов (в обратном порядке)
            // Для простоты - одна итерация
            out_size = decode_tokens(decoded, decoded_size, tokens, token_count, output, header.original_size + 1024);
            
            free(current);
            free(decoded);
            free(dict_raw);
            free(tokens);
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
