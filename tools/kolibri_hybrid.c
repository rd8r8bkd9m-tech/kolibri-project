// ═══════════════════════════════════════════════════════════════
//   KOLIBRI ARCHIVER GPU v10.0 - RLE + LZ77 HYBRID
//   Real compression: RLE for homogeneous + LZ77 for text/code
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>

#define CHUNK_SIZE 4096
#define MAGIC 0x4B4C4942
#define WINDOW_SIZE 4096
#define MAX_MATCH 258

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

// Найти самое длинное совпадение в окне
int find_longest_match(const uint8_t *data, size_t pos, size_t size, int *match_distance, int *match_length) {
    *match_distance = 0;
    *match_length = 0;
    
    int window_start = (pos > WINDOW_SIZE) ? (pos - WINDOW_SIZE) : 0;
    
    for (int i = window_start; i < pos; i++) {
        int len = 0;
        while (len < MAX_MATCH && 
               (pos + len) < size && 
               data[i + len] == data[pos + len]) {
            len++;
        }
        
        if (len > *match_length && len >= 3) {  // Минимум 3 байта для match
            *match_length = len;
            *match_distance = pos - i;
        }
    }
    
    return (*match_length >= 3);
}

// LZ77 сжатие chunk
size_t compress_lz77(const uint8_t *chunk, size_t chunk_size, uint8_t *out) {
    size_t out_pos = 0;
    size_t pos = 0;
    
    out[out_pos++] = 2;  // Marker: LZ77
    uint32_t *size_ptr = (uint32_t*)(out + out_pos);
    out_pos += 4;
    size_t compressed_start = out_pos;
    
    while (pos < chunk_size) {
        int match_dist, match_len;
        
        if (find_longest_match(chunk, pos, chunk_size, &match_dist, &match_len)) {
            // Match found: <1> <distance:2> <length:1>
            out[out_pos++] = 1;  // Match flag
            *(uint16_t*)(out + out_pos) = (uint16_t)match_dist;
            out_pos += 2;
            out[out_pos++] = (uint8_t)match_len;
            pos += match_len;
        } else {
            // Literal: <0> <byte>
            out[out_pos++] = 0;  // Literal flag
            out[out_pos++] = chunk[pos];
            pos++;
        }
    }
    
    *size_ptr = out_pos - compressed_start;
    return out_pos;
}

// LZ77 декомпрессия
size_t decompress_lz77(const uint8_t *compressed, uint8_t *out) {
    uint32_t compressed_size = *(uint32_t*)(compressed + 1);
    size_t in_pos = 5;
    size_t out_pos = 0;
    size_t in_end = 5 + compressed_size;
    
    while (in_pos < in_end) {
        uint8_t flag = compressed[in_pos++];
        
        if (flag == 1) {
            // Match
            uint16_t distance = *(uint16_t*)(compressed + in_pos);
            in_pos += 2;
            uint8_t length = compressed[in_pos++];
            
            // Copy from window (важно копировать по одному байту для overlapping)
            size_t copy_pos = out_pos - distance;
            for (int i = 0; i < length; i++) {
                out[out_pos] = out[copy_pos];
                out_pos++;
                copy_pos++;
            }
        } else {
            // Literal
            out[out_pos++] = compressed[in_pos++];
        }
    }
    
    return out_pos;
}

// RLE сжатие (для гомогенных данных)
size_t compress_rle(const uint8_t *chunk, size_t chunk_size, uint8_t *out) {
    out[0] = 1;  // RLE marker
    out[1] = chunk[0];
    *(uint32_t*)(out + 2) = (uint32_t)chunk_size;
    return 6;
}

// RLE декомпрессия
size_t decompress_rle(const uint8_t *compressed, uint8_t *out) {
    uint8_t value = compressed[1];
    uint32_t count = *(uint32_t*)(compressed + 2);
    memset(out, value, count);
    return count;
}

// Умное сжатие: выбирает RLE или LZ77
size_t compress_smart(const uint8_t *chunk, size_t chunk_size, uint8_t *out) {
    // Проверка на гомогенность
    int homogeneous = 1;
    uint8_t first = chunk[0];
    for (size_t i = 1; i < chunk_size; i++) {
        if (chunk[i] != first) {
            homogeneous = 0;
            break;
        }
    }
    
    if (homogeneous && chunk_size == CHUNK_SIZE) {
        return compress_rle(chunk, chunk_size, out);
    }
    
    // Пробуем LZ77
    uint8_t *lz77_out = malloc(CHUNK_SIZE * 2);
    size_t lz77_size = compress_lz77(chunk, chunk_size, lz77_out);
    
    // Если LZ77 не уменьшает размер, сохраняем raw
    if (lz77_size >= chunk_size + 5) {
        free(lz77_out);
        out[0] = 0;  // Raw marker
        *(uint32_t*)(out + 1) = (uint32_t)chunk_size;
        memcpy(out + 5, chunk, chunk_size);
        return 5 + chunk_size;
    }
    
    memcpy(out, lz77_out, lz77_size);
    free(lz77_out);
    return lz77_size;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI ARCHIVER GPU v10.0 - Hybrid Compression             ║\n");
        printf("║  RLE (homogeneous) + LZ77 (text/code) + Raw (random)         ║\n");
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
        printf("║  KOLIBRI HYBRID COMPRESSOR v10.0                              ║\n");
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
        
        uint8_t* data = malloc(file_size);
        fread(data, 1, file_size, fin);
        fclose(fin);
        
        printf("🔧 Smart compression: RLE + LZ77 hybrid\n\n");
        printf("🚀 Сжатие...\n");
        double start = get_time();
        
        FILE* fout = fopen(output_path, "wb");
        ArchiveHeader header = {
            .magic = MAGIC,
            .version = 2,
            .original_size = (uint32_t)file_size,
            .compressed_size = 0,
            .num_chunks = 0
        };
        fwrite(&header, sizeof(header), 1, fout);
        
        size_t num_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
        uint8_t* compress_buffer = malloc(CHUNK_SIZE * 2);
        size_t total_compressed = 0;
        size_t rle_chunks = 0;
        size_t lz77_chunks = 0;
        size_t raw_chunks = 0;
        
        for (size_t i = 0; i < num_chunks; i++) {
            size_t offset = i * CHUNK_SIZE;
            size_t chunk_size = (offset + CHUNK_SIZE > file_size) ? (file_size - offset) : CHUNK_SIZE;
            
            size_t compressed_size = compress_smart(data + offset, chunk_size, compress_buffer);
            fwrite(compress_buffer, 1, compressed_size, fout);
            
            // Статистика по типу сжатия
            uint8_t marker = compress_buffer[0];
            if (marker == 1) rle_chunks++;
            else if (marker == 2) lz77_chunks++;
            else raw_chunks++;
            
            total_compressed += compressed_size;
            
            if (i % 5000 == 0 && i > 0) {
                double progress = (double)i / num_chunks * 100;
                printf("  %.1f%% (RLE: %zu, LZ77: %zu, Raw: %zu)\n", 
                       progress, rle_chunks, lz77_chunks, raw_chunks);
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
        printf("║  RLE chunks:        %zu%44s║\n", rle_chunks, "");
        printf("║  LZ77 chunks:       %zu%44s║\n", lz77_chunks, "");
        printf("║  Raw chunks:        %zu%44s║\n", raw_chunks, "");
        printf("║  Время:             %.3f сек%34s║\n", elapsed, "");
        printf("║  Скорость:          %.2f MB/s%34s║\n", speed, "");
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        
        printf("✅ Архив сохранён: %s\n\n", output_path);
        
        free(data);
        free(compress_buffer);
    }
    else if (strcmp(mode, "extract") == 0) {
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI HYBRID EXTRACTOR v10.0                               ║\n");
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        
        FILE* fin = fopen(input_path, "rb");
        if (!fin) {
            printf("❌ Cannot open: %s\n", input_path);
            return 1;
        }
        
        ArchiveHeader header;
        fread(&header, sizeof(header), 1, fin);
        
        if (header.magic != MAGIC) {
            printf("❌ Invalid archive format\n");
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
            uint8_t marker;
            fread(&marker, 1, 1, fin);
            
            size_t bytes_decompressed;
            
            if (marker == 1) {
                // RLE
                uint8_t value;
                uint32_t count;
                fread(&value, 1, 1, fin);
                fread(&count, sizeof(uint32_t), 1, fin);
                memset(output + output_pos, value, count);
                bytes_decompressed = count;
            }
            else if (marker == 2) {
                // LZ77
                uint32_t size;
                fread(&size, sizeof(uint32_t), 1, fin);
                
                // Прочитать сжатые данные
                uint8_t* lz_data = malloc(size + 5);
                lz_data[0] = marker;
                *(uint32_t*)(lz_data + 1) = size;
                fread(lz_data + 5, 1, size, fin);
                
                bytes_decompressed = decompress_lz77(lz_data, output + output_pos);
                free(lz_data);
            }
            else {
                // Raw
                uint32_t size;
                fread(&size, sizeof(uint32_t), 1, fin);
                fread(output + output_pos, 1, size, fin);
                bytes_decompressed = size;
            }
            
            output_pos += bytes_decompressed;
            
            if (i % 5000 == 0 && i > 0) {
                double progress = (double)i / header.num_chunks * 100;
                printf("  %.1f%%\n", progress);
            }
        }
        
        fclose(fin);
        
        FILE* fout = fopen(output_path, "wb");
        fwrite(output, 1, header.original_size, fout);
        fclose(fout);
        
        double end = get_time();
        double elapsed = end - start;
        
        printf("\n✅ Файл восстановлен: %s\n", output_path);
        printf("⏱  Время: %.3f сек\n\n", elapsed);
        
        free(output);
        free(chunk_buffer);
    }
    
    return 0;
}
