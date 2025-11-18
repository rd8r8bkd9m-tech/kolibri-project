// ═══════════════════════════════════════════════════════════════
//   KOLIBRI ARCHIVER v11.0 - PRODUCTION READY
//   RLE (homogeneous) + ZLIB (everything else)
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <zlib.h>

#define CHUNK_SIZE 4096
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

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI ARCHIVER v11.0 - Production Ready                    ║\n");
        printf("║  RLE (homogeneous) + ZLIB (text/code/random)                 ║\n");
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
        printf("║  KOLIBRI PRODUCTION COMPRESSOR v11.0                          ║\n");
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
        
        printf("🔧 Hybrid: RLE + ZLIB (level 6)\n\n");
        printf("🚀 Сжатие...\n");
        double start = get_time();
        
        FILE* fout = fopen(output_path, "wb");
        ArchiveHeader header = {
            .magic = MAGIC,
            .version = 11,
            .original_size = (uint32_t)file_size,
            .compressed_size = 0,
            .num_chunks = 0
        };
        fwrite(&header, sizeof(header), 1, fout);
        
        size_t num_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
        uint8_t* compress_buffer = malloc(compressBound(CHUNK_SIZE));
        size_t rle_chunks = 0;
        size_t zlib_chunks = 0;
        
        for (size_t i = 0; i < num_chunks; i++) {
            size_t offset = i * CHUNK_SIZE;
            size_t chunk_size = (offset + CHUNK_SIZE > file_size) ? (file_size - offset) : CHUNK_SIZE;
            
            // Check homogeneous
            int homogeneous = 1;
            uint8_t first = data[offset];
            for (size_t j = 1; j < chunk_size; j++) {
                if (data[offset + j] != first) {
                    homogeneous = 0;
                    break;
                }
            }
            
            if (homogeneous && chunk_size == CHUNK_SIZE) {
                // RLE
                uint8_t marker = 1;
                fwrite(&marker, 1, 1, fout);
                fwrite(&first, 1, 1, fout);
                uint32_t count = (uint32_t)chunk_size;
                fwrite(&count, sizeof(uint32_t), 1, fout);
                rle_chunks++;
            } else {
                // ZLIB
                uLongf compressed_size = compressBound(chunk_size);
                int ret = compress2(compress_buffer, &compressed_size, 
                                   data + offset, chunk_size, 6);
                
                if (ret == Z_OK && compressed_size < chunk_size) {
                    uint8_t marker = 2;
                    fwrite(&marker, 1, 1, fout);
                    uint32_t csize = (uint32_t)compressed_size;
                    fwrite(&csize, sizeof(uint32_t), 1, fout);
                    fwrite(compress_buffer, 1, compressed_size, fout);
                    zlib_chunks++;
                } else {
                    // Raw fallback
                    uint8_t marker = 0;
                    fwrite(&marker, 1, 1, fout);
                    uint32_t rsize = (uint32_t)chunk_size;
                    fwrite(&rsize, sizeof(uint32_t), 1, fout);
                    fwrite(data + offset, 1, chunk_size, fout);
                }
            }
            
            if (i % 5000 == 0 && i > 0) {
                double progress = (double)i / num_chunks * 100;
                printf("  %.1f%% (RLE: %zu, ZLIB: %zu)\n", progress, rle_chunks, zlib_chunks);
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
        printf("║  ZLIB chunks:       %zu%44s║\n", zlib_chunks, "");
        printf("║  Время:             %.3f сек%34s║\n", elapsed, "");
        printf("║  Скорость:          %.2f MB/s%34s║\n", speed, "");
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        
        printf("✅ Архив сохранён: %s\n\n", output_path);
        
        free(data);
        free(compress_buffer);
    }
    else if (strcmp(mode, "extract") == 0) {
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI PRODUCTION EXTRACTOR v11.0                           ║\n");
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
        uint8_t* decompress_buffer = malloc(CHUNK_SIZE * 2);
        size_t output_pos = 0;
        
        for (uint32_t i = 0; i < header.num_chunks; i++) {
            uint8_t marker;
            fread(&marker, 1, 1, fin);
            
            if (marker == 1) {
                // RLE
                uint8_t value;
                uint32_t count;
                fread(&value, 1, 1, fin);
                fread(&count, sizeof(uint32_t), 1, fin);
                memset(output + output_pos, value, count);
                output_pos += count;
            }
            else if (marker == 2) {
                // ZLIB
                uint32_t compressed_size;
                fread(&compressed_size, sizeof(uint32_t), 1, fin);
                fread(decompress_buffer, 1, compressed_size, fin);
                
                uLongf decompressed_size = CHUNK_SIZE;
                uncompress(output + output_pos, &decompressed_size, 
                          decompress_buffer, compressed_size);
                output_pos += decompressed_size;
            }
            else {
                // Raw
                uint32_t size;
                fread(&size, sizeof(uint32_t), 1, fin);
                fread(output + output_pos, 1, size, fin);
                output_pos += size;
            }
            
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
        free(decompress_buffer);
    }
    
    return 0;
}
