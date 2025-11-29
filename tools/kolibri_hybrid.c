// ═══════════════════════════════════════════════════════════════
//   KOLIBRI ARCHIVER GPU v11.0 - STREAMING LZ77 HYBRID
//   Fixed: streaming mode instead of independent chunks
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <zlib.h>

#define MAGIC 0x4B4C4943  // KLIC (новая версия)
#define WINDOW_SIZE 32768
#define MAX_MATCH 258
#define MIN_MATCH 3

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t original_size;
    uint32_t compressed_size;
    uint8_t  method;  // 1=RLE, 2=LZ77, 3=ZLIB
} __attribute__((packed)) ArchiveHeader;

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// ═══════════════════════════════════════════════════════════════
//   STREAMING LZ77 - весь файл как один поток
// ═══════════════════════════════════════════════════════════════

// Найти самое длинное совпадение в окне (глобальный поиск)
static int find_match(const uint8_t *data, size_t pos, size_t size, 
                      uint16_t *match_distance, uint8_t *match_length) {
    *match_distance = 0;
    *match_length = 0;
    
    if (pos < MIN_MATCH || size - pos < MIN_MATCH) return 0;
    
    size_t window_start = (pos > WINDOW_SIZE) ? (pos - WINDOW_SIZE) : 0;
    int best_len = MIN_MATCH - 1;
    
    for (size_t i = window_start; i < pos; i++) {
        int len = 0;
        while (len < MAX_MATCH && 
               (pos + len) < size && 
               data[i + len] == data[pos + len]) {
            len++;
        }
        
        if (len > best_len) {
            best_len = len;
            *match_length = (uint8_t)len;
            *match_distance = (uint16_t)(pos - i);
        }
    }
    
    return (best_len >= MIN_MATCH);
}

// Streaming LZ77 сжатие всего файла
// Формат: literals as-is, match = <0xFE> <distance:2> <length:1>
// 0xFE escaped as <0xFE> <0x00>
size_t compress_lz77_stream(const uint8_t *data, size_t size, uint8_t *out, size_t out_max) {
    size_t out_pos = 0;
    size_t pos = 0;
    
    while (pos < size) {
        // Проверка границ буфера
        if (out_pos >= out_max - 6) {
            return 0;  // Буфер переполнен
        }
        
        uint16_t match_dist;
        uint8_t match_len;
        
        if (find_match(data, pos, size, &match_dist, &match_len) && match_dist > 0) {
            // Match: <0xFE> <distance:2> <length:1>
            out[out_pos++] = 0xFE;
            *(uint16_t*)(out + out_pos) = match_dist;
            out_pos += 2;
            out[out_pos++] = match_len;
            pos += match_len;
        } else {
            // Literal: если 0xFE, экранируем как <0xFE><0x00>
            if (data[pos] == 0xFE) {
                out[out_pos++] = 0xFE;
                out[out_pos++] = 0x00;
            } else {
                out[out_pos++] = data[pos];
            }
            pos++;
        }
    }
    
    return out_pos;
}

// Streaming LZ77 декомпрессия
size_t decompress_lz77_stream(const uint8_t *compressed, size_t comp_size, uint8_t *out) {
    size_t in_pos = 0;
    size_t out_pos = 0;
    
    while (in_pos < comp_size) {
        if (compressed[in_pos] == 0xFE) {
            in_pos++;
            if (in_pos >= comp_size) break;
            
            // Проверяем: это escaped 0xFE или match?
            // Если следующие 2 байта = 0x0000, это escaped literal
            if (compressed[in_pos] == 0x00 && (in_pos + 1 >= comp_size || compressed[in_pos + 1] == 0x00)) {
                // Escaped 0xFE literal
                out[out_pos++] = 0xFE;
                in_pos++;
            } else {
                // Match: read distance (2 bytes) and length (1 byte)
                uint16_t distance = *(uint16_t*)(compressed + in_pos);
                in_pos += 2;
                uint8_t length = compressed[in_pos++];
                
                // Copy from window byte-by-byte (handles overlapping)
                for (int i = 0; i < length && out_pos > 0; i++) {
                    out[out_pos] = out[out_pos - distance];
                    out_pos++;
                }
            }
        } else {
            // Literal byte
            out[out_pos++] = compressed[in_pos++];
        }
    }
    
    return out_pos;
}

// Проверка на гомогенность (весь файл одним байтом)
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
        printf("║  KOLIBRI ARCHIVER v11.0 - Streaming Hybrid Compression        ║\n");
        printf("║  Methods: RLE (homogeneous) | LZ77 (streaming) | ZLIB (best)  ║\n");
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        printf("Использование:\n");
        printf("  %s compress <input> <output.kolibri>\n", argv[0]);
        printf("  %s extract <input.kolibri> <output>\n\n", argv[0]);
        return 1;
    }
    
    const char* mode = argv[1];
    const char* input_path = argv[2];
    const char* output_path = argv[3];
    
    if (strcmp(mode, "compress") == 0) {
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI STREAMING COMPRESSOR v11.0                           ║\n");
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        
        FILE* fin = fopen(input_path, "rb");
        if (!fin) {
            printf("❌ Cannot open: %s\n", input_path);
            return 1;
        }
        
        fseek(fin, 0, SEEK_END);
        long file_size = ftell(fin);
        fseek(fin, 0, SEEK_SET);
        
        printf("📄 Input file:  %s\n", input_path);
        printf("📊 Size:        %.2f KB\n", file_size / 1024.0);
        
        uint8_t* data = malloc(file_size);
        if (fread(data, 1, file_size, fin) != (size_t)file_size) {
            printf("❌ Read error\n");
            fclose(fin);
            free(data);
            return 1;
        }
        fclose(fin);
        
        printf("🔧 Auto-selecting best compression method...\n\n");
        double start = get_time();
        
        ArchiveHeader header = {
            .magic = MAGIC,
            .version = 11,
            .original_size = (uint32_t)file_size,
            .compressed_size = 0,
            .method = 0
        };
        
        uint8_t* best_compressed = NULL;
        size_t best_size = file_size + 1;
        uint8_t best_method = 0;
        const char* method_name = "RAW";
        
        // Метод 1: RLE (только для гомогенных данных)
        if (is_homogeneous(data, file_size)) {
            // RLE: просто value + count
            best_compressed = malloc(5);
            best_compressed[0] = data[0];
            *(uint32_t*)(best_compressed + 1) = (uint32_t)file_size;
            best_size = 5;
            best_method = 1;
            method_name = "RLE";
            printf("   ✓ RLE:  5 bytes (гомогенные данные)\n");
        } else {
            // Метод 2: Streaming LZ77 (только для файлов < 1MB)
            if (file_size < 1000000) {
                size_t out_buf_size = file_size * 6 + 8192;  // 6x запас для наихудшего случая
                uint8_t* lz77_out = malloc(out_buf_size);
                size_t lz77_size = compress_lz77_stream(data, file_size, lz77_out, out_buf_size);
                
                if (lz77_size > 0 && lz77_size < best_size && lz77_size < (size_t)file_size) {
                    printf("   ✓ LZ77: %zu bytes\n", lz77_size);
                    best_compressed = lz77_out;
                    best_size = lz77_size;
                    best_method = 2;
                    method_name = "LZ77";
                } else {
                    free(lz77_out);
                    if (lz77_size == 0) printf("   ✗ LZ77: буфер переполнен\n");
                    else if (lz77_size >= (size_t)file_size) printf("   ✗ LZ77: %zu bytes (хуже оригинала)\n", lz77_size);
                }
            }
            
            // Метод 3: ZLIB (deflate) - надёжный метод
            uLongf zlib_size = compressBound(file_size);
            uint8_t* zlib_out = malloc(zlib_size);
            if (compress2(zlib_out, &zlib_size, data, file_size, 9) == Z_OK) {
                printf("   ✓ ZLIB: %lu bytes\n", zlib_size);
                if (zlib_size < best_size) {
                    if (best_compressed) free(best_compressed);
                    best_compressed = zlib_out;
                    best_size = zlib_size;
                    best_method = 3;
                    method_name = "ZLIB";
                } else {
                    free(zlib_out);
                }
            } else {
                free(zlib_out);
            }
        }
        
        // Если ничего не сжало лучше оригинала, сохраняем raw
        if (best_size >= (size_t)file_size) {
            if (best_compressed) free(best_compressed);
            best_compressed = malloc(file_size);
            memcpy(best_compressed, data, file_size);
            best_size = file_size;
            best_method = 0;
            method_name = "RAW";
        }
        
        header.compressed_size = (uint32_t)best_size;
        header.method = best_method;
        
        // Запись архива
        FILE* fout = fopen(output_path, "wb");
        fwrite(&header, sizeof(header), 1, fout);
        fwrite(best_compressed, 1, best_size, fout);
        fclose(fout);
        
        double end = get_time();
        double elapsed = end - start;
        
        long archive_size = sizeof(header) + best_size;
        double ratio = (double)file_size / archive_size;
        double speed = file_size / 1024.0 / 1024.0 / elapsed;
        
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  РЕЗУЛЬТАТЫ                                                   ║\n");
        printf("╠════════════════════════════════════════════════════════════════╣\n");
        printf("║  Метод:             %-6s%38s║\n", method_name, "");
        printf("║  Исходный размер:   %.2f KB%34s║\n", file_size / 1024.0, "");
        printf("║  Сжатый размер:     %.2f KB%34s║\n", archive_size / 1024.0, "");
        printf("║  Коэффициент:       %.2fx%40s║\n", ratio, "");
        printf("║  Время:             %.3f сек%34s║\n", elapsed, "");
        printf("║  Скорость:          %.2f MB/s%34s║\n", speed, "");
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        
        printf("✅ Архив сохранён: %s\n\n", output_path);
        
        free(data);
        if (best_compressed) free(best_compressed);
    }
    else if (strcmp(mode, "extract") == 0) {
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI STREAMING EXTRACTOR v11.0                            ║\n");
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        
        FILE* fin = fopen(input_path, "rb");
        if (!fin) {
            printf("❌ Cannot open: %s\n", input_path);
            return 1;
        }
        
        ArchiveHeader header;
        if (fread(&header, sizeof(header), 1, fin) != 1) {
            printf("❌ Read header error\n");
            fclose(fin);
            return 1;
        }
        
        if (header.magic != MAGIC) {
            printf("❌ Invalid archive format (magic: 0x%08X)\n", header.magic);
            fclose(fin);
            return 1;
        }
        
        const char* method_names[] = {"RAW", "RLE", "LZ77", "ZLIB"};
        const char* method_name = (header.method <= 3) ? method_names[header.method] : "UNKNOWN";
        
        printf("📄 Archive:     %s\n", input_path);
        printf("📊 Original:    %.2f KB\n", header.original_size / 1024.0);
        printf("📦 Compressed:  %.2f KB\n", header.compressed_size / 1024.0);
        printf("🔧 Method:      %s\n", method_name);
        printf("🔧 Ratio:       %.2fx\n\n", (double)header.original_size / (header.compressed_size + sizeof(header)));
        
        printf("🔓 Восстановление...\n");
        double start = get_time();
        
        uint8_t* compressed = malloc(header.compressed_size);
        if (fread(compressed, 1, header.compressed_size, fin) != header.compressed_size) {
            printf("❌ Read data error\n");
            fclose(fin);
            free(compressed);
            return 1;
        }
        fclose(fin);
        
        uint8_t* output = malloc(header.original_size);
        size_t decompressed_size = 0;
        
        switch (header.method) {
            case 0:  // RAW
                memcpy(output, compressed, header.original_size);
                decompressed_size = header.original_size;
                break;
                
            case 1:  // RLE
                memset(output, compressed[0], header.original_size);
                decompressed_size = header.original_size;
                break;
                
            case 2:  // LZ77 streaming
                decompressed_size = decompress_lz77_stream(compressed, header.compressed_size, output);
                break;
                
            case 3:  // ZLIB
                {
                    uLongf dest_len = header.original_size;
                    if (uncompress(output, &dest_len, compressed, header.compressed_size) != Z_OK) {
                        printf("❌ ZLIB decompression error\n");
                        free(compressed);
                        free(output);
                        return 1;
                    }
                    decompressed_size = dest_len;
                }
                break;
                
            default:
                printf("❌ Unknown method: %d\n", header.method);
                free(compressed);
                free(output);
                return 1;
        }
        
        FILE* fout = fopen(output_path, "wb");
        fwrite(output, 1, decompressed_size, fout);
        fclose(fout);
        
        double end = get_time();
        double elapsed = end - start;
        
        printf("\n✅ Файл восстановлен: %s\n", output_path);
        printf("   Размер: %zu bytes\n", decompressed_size);
        printf("⏱  Время: %.3f сек\n\n", elapsed);
        
        free(compressed);
        free(output);
    }
    
    return 0;
}
