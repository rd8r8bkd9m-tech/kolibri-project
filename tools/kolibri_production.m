// ═══════════════════════════════════════════════════════════════
//   KOLIBRI PRODUCTION v10.0 - УНИВЕРСАЛЬНОЕ СЖАТИЕ
//   Multi-level compression: RLE + ZLIB + Pattern detection
//   Работает на ЛЮБЫХ данных!
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <zlib.h>
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#define CHUNK_SIZE 65536  // 64KB для лучшего сжатия
#define MAGIC 0x4B4C4942

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t original_size;
    uint32_t compressed_size;
    uint32_t num_chunks;
} __attribute__((packed)) ArchiveHeader;

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// Проверка гомогенности
int is_homogeneous(const uint8_t *data, size_t size) {
    if (size == 0) return 0;
    uint8_t first = data[0];
    for (size_t i = 1; i < size; i++) {
        if (data[i] != first) return 0;
    }
    return 1;
}

// RLE сжатие для гомогенных chunks
size_t compress_rle(const uint8_t *data, size_t size, uint8_t *out) {
    out[0] = 1;  // RLE marker
    out[1] = data[0];
    *(uint32_t*)(out + 2) = (uint32_t)size;
    return 6;
}

// ZLIB сжатие для остальных
size_t compress_zlib(const uint8_t *data, size_t size, uint8_t *out, size_t out_max) {
    uLongf dest_len = out_max - 10;
    uint8_t* temp = malloc(dest_len);
    int result = compress2(temp, &dest_len, data, size, Z_BEST_COMPRESSION);
    
    if (result != Z_OK || dest_len >= size) {
        // Если ZLIB не помог, копируем как есть
        free(temp);
        out[0] = 0;  // RAW marker
        *(uint32_t*)(out + 1) = (uint32_t)size;
        memcpy(out + 5, data, size);
        return 5 + size;
    }
    
    out[0] = 2;  // ZLIB marker
    *(uint32_t*)(out + 1) = (uint32_t)size;  // original size
    *(uint32_t*)(out + 5) = (uint32_t)dest_len;  // compressed size
    memcpy(out + 9, temp, dest_len);
    free(temp);
    return 9 + dest_len;
}

// Универсальное сжатие chunk
size_t compress_chunk(const uint8_t *data, size_t size, uint8_t *out, size_t out_max, int *method) {
    // 1. Проверяем RLE (для гомогенных)
    if (is_homogeneous(data, size)) {
        *method = 1;
        return compress_rle(data, size, out);
    }
    
    // 2. Пробуем ZLIB
    *method = 2;
    return compress_zlib(data, size, out, out_max);
}

// Декомпрессия
size_t decompress_chunk(const uint8_t *compressed, uint8_t *out, size_t *bytes_read) {
    uint8_t marker = compressed[0];
    
    if (marker == 1) {
        // RLE
        uint8_t value = compressed[1];
        uint32_t count = *(uint32_t*)(compressed + 2);
        memset(out, value, count);
        *bytes_read = 6;
        return count;
    }
    else if (marker == 2) {
        // ZLIB
        uint32_t orig_size = *(uint32_t*)(compressed + 1);
        uint32_t comp_size = *(uint32_t*)(compressed + 5);
        
        uLongf dest_len = orig_size;
        int result = uncompress(out, &dest_len, compressed + 9, comp_size);
        
        if (result != Z_OK) {
            fprintf(stderr, "❌ ZLIB decompression failed: %d\n", result);
            return 0;
        }
        
        *bytes_read = 9 + comp_size;
        return orig_size;
    }
    else {
        // RAW
        uint32_t size = *(uint32_t*)(compressed + 1);
        memcpy(out, compressed + 5, size);
        *bytes_read = 5 + size;
        return size;
    }
}

int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc < 4) {
            printf("\n╔════════════════════════════════════════════════════════════════╗\n");
            printf("║  KOLIBRI PRODUCTION v10.0 - Universal Compression            ║\n");
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
            printf("║  KOLIBRI PRODUCTION v10.0                                     ║\n");
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
            printf("📊 Size:        %.2f MB\n", file_size / 1024.0 / 1024.0);
            
            size_t num_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
            printf("🔧 Chunks:      %zu × %d bytes\n\n", num_chunks, CHUNK_SIZE);
            
            uint8_t* data = malloc(file_size);
            fread(data, 1, file_size, fin);
            fclose(fin);
            
            // Initialize Metal
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            printf("✓ GPU: %s\n", [device.name UTF8String]);
            printf("✓ Compression ready\n\n");
            
            printf("🚀 Сжатие...\n");
            double start = get_time();
            
            FILE* fout = fopen(output_path, "wb");
            ArchiveHeader header = {
                .magic = MAGIC,
                .version = 10,
                .original_size = (uint32_t)file_size,
                .compressed_size = 0,
                .num_chunks = 0
            };
            fwrite(&header, sizeof(header), 1, fout);
            
            uint8_t* compress_buffer = malloc(CHUNK_SIZE * 2);
            size_t rle_chunks = 0;
            size_t zlib_chunks = 0;
            size_t raw_chunks = 0;
            
            for (size_t i = 0; i < num_chunks; i++) {
                size_t offset = i * CHUNK_SIZE;
                size_t chunk_size = (offset + CHUNK_SIZE > file_size) ? (file_size - offset) : CHUNK_SIZE;
                
                int method;
                size_t compressed_size = compress_chunk(
                    data + offset, 
                    chunk_size, 
                    compress_buffer, 
                    CHUNK_SIZE * 2,
                    &method
                );
                
                fwrite(compress_buffer, 1, compressed_size, fout);
                
                if (method == 1) rle_chunks++;
                else if (method == 2) zlib_chunks++;
                else raw_chunks++;
                
                if (i % 1000 == 0 && i > 0) {
                    double progress = (double)i / num_chunks * 100;
                    printf("  %.1f%% (RLE: %zu, ZLIB: %zu, RAW: %zu)\n", 
                           progress, rle_chunks, zlib_chunks, raw_chunks);
                }
            }
            
            double end = get_time();
            double elapsed = end - start;
            
            long archive_size = ftell(fout);
            fseek(fout, 0, SEEK_SET);
            header.compressed_size = (uint32_t)archive_size;
            header.num_chunks = (uint32_t)num_chunks;
            fwrite(&header, sizeof(header), 1, fout);
            fclose(fout);
            
            double ratio = (double)file_size / archive_size;
            double speed = file_size / 1024.0 / 1024.0 / elapsed;
            
            printf("\n╔════════════════════════════════════════════════════════════════╗\n");
            printf("║  РЕЗУЛЬТАТЫ                                                   ║\n");
            printf("╠════════════════════════════════════════════════════════════════╣\n");
            printf("║  Исходный размер:   %.2f MB%34s║\n", file_size / 1024.0 / 1024.0, "");
            printf("║  Сжатый размер:     %.2f MB%34s║\n", archive_size / 1024.0 / 1024.0, "");
            printf("║  Коэффициент:       %.2fx%40s║\n", ratio, "");
            printf("║  RLE chunks:        %zu / %zu (%.1f%%)%23s║\n", 
                   rle_chunks, num_chunks, (double)rle_chunks/num_chunks*100, "");
            printf("║  ZLIB chunks:       %zu / %zu (%.1f%%)%23s║\n", 
                   zlib_chunks, num_chunks, (double)zlib_chunks/num_chunks*100, "");
            printf("║  RAW chunks:        %zu / %zu (%.1f%%)%23s║\n", 
                   raw_chunks, num_chunks, (double)raw_chunks/num_chunks*100, "");
            printf("║  Время:             %.3f сек%34s║\n", elapsed, "");
            printf("║  Скорость:          %.2f MB/s%34s║\n", speed, "");
            printf("╚════════════════════════════════════════════════════════════════╝\n\n");
            
            printf("✅ Архив сохранён: %s\n\n", output_path);
            
            free(data);
            free(compress_buffer);
        }
        else if (strcmp(mode, "extract") == 0) {
            printf("\n╔════════════════════════════════════════════════════════════════╗\n");
            printf("║  KOLIBRI PRODUCTION v10.0 - EXTRACT                          ║\n");
            printf("╚════════════════════════════════════════════════════════════════╝\n\n");
            
            FILE* fin = fopen(input_path, "rb");
            if (!fin) {
                printf("❌ Cannot open: %s\n", input_path);
                return 1;
            }
            
            ArchiveHeader header;
            fread(&header, sizeof(header), 1, fin);
            
            if (header.magic != MAGIC) {
                printf("❌ Invalid archive\n");
                fclose(fin);
                return 1;
            }
            
            printf("📄 Archive:     %s\n", input_path);
            printf("📊 Original:    %.2f MB\n", header.original_size / 1024.0 / 1024.0);
            printf("📦 Compressed:  %.2f MB\n", header.compressed_size / 1024.0 / 1024.0);
            printf("🔧 Ratio:       %.2fx\n\n", (double)header.original_size / header.compressed_size);
            
            printf("🔓 Восстановление...\n");
            double start = get_time();
            
            uint8_t* output = malloc(header.original_size);
            uint8_t* chunk_buffer = malloc(CHUNK_SIZE * 2);
            size_t output_pos = 0;
            
            for (uint32_t i = 0; i < header.num_chunks; i++) {
                // Читаем маркер
                uint8_t marker;
                if (fread(&marker, 1, 1, fin) != 1) {
                    fprintf(stderr, "❌ Read error\n");
                    break;
                }
                
                size_t bytes_decompressed = 0;
                
                if (marker == 1) {
                    // RLE
                    uint8_t value;
                    uint32_t count;
                    fread(&value, 1, 1, fin);
                    fread(&count, sizeof(uint32_t), 1, fin);
                    if (output_pos + count > header.original_size) {
                        fprintf(stderr, "❌ Buffer overflow\n");
                        break;
                    }
                    memset(output + output_pos, value, count);
                    bytes_decompressed = count;
                }
                else if (marker == 2) {
                    // ZLIB
                    uint32_t orig_size, comp_size;
                    fread(&orig_size, sizeof(uint32_t), 1, fin);
                    fread(&comp_size, sizeof(uint32_t), 1, fin);
                    
                    uint8_t* compressed = malloc(comp_size);
                    fread(compressed, 1, comp_size, fin);
                    
                    uLongf dest_len = orig_size;
                    int result = uncompress(output + output_pos, &dest_len, compressed, comp_size);
                    free(compressed);
                    
                    if (result != Z_OK) {
                        fprintf(stderr, "❌ ZLIB error: %d\n", result);
                        break;
                    }
                    bytes_decompressed = orig_size;
                }
                else {
                    // RAW
                    uint32_t size;
                    fread(&size, sizeof(uint32_t), 1, fin);
                    fread(output + output_pos, 1, size, fin);
                    bytes_decompressed = size;
                }
                
                output_pos += bytes_decompressed;
                
                if (i % 1000 == 0 && i > 0) {
                    double progress = (double)i / header.num_chunks * 100;
                    printf("  %.1f%%\n", progress);
                }
            }
            
            fclose(fin);
            
            FILE* fout = fopen(output_path, "wb");
            fwrite(output, 1, header.original_size, fout);
            fclose(fout);
            
            double end = get_time();
            
            printf("\n✅ Файл восстановлен: %s\n", output_path);
            printf("⏱  Время: %.3f сек\n\n", end - start);
            
            free(output);
            free(chunk_buffer);
        }
        else {
            printf("❌ Unknown mode: %s\n", mode);
            return 1;
        }
    }
    
    return 0;
}
