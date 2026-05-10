#include "kolibri/compress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    // Простой тест сжатия/распаковки
    const char *test_data = "Hello, World! This is a test string for compression.";
    size_t input_size = strlen(test_data) + 1;
    
    printf("=== Kolibri Archiver Debug Test ===\n");
    printf("Input: '%s'\n", test_data);
    printf("Input size: %zu bytes\n\n", input_size);
    
    // Создание компрессора
    KolibriCompressor *comp = kolibri_compressor_create(KOLIBRI_COMPRESS_ALL);
    if (!comp) {
        printf("ERROR: Failed to create compressor\n");
        return 1;
    }
    
    // Сжатие
    uint8_t *compressed = NULL;
    size_t compressed_size = 0;
    KolibriCompressStats stats;
    
    printf("Compressing...\n");
    int ret = kolibri_compress(comp, (const uint8_t*)test_data, input_size,
                               &compressed, &compressed_size, &stats);
    
    if (ret != 0) {
        printf("ERROR: Compression failed with code %d\n", ret);
        kolibri_compressor_destroy(comp);
        return 1;
    }
    
    printf("Compressed size: %zu bytes\n", compressed_size);
    printf("Compression ratio: %.2fx\n", stats.compression_ratio);
    printf("Methods used: 0x%08x\n", stats.methods_used);
    printf("Checksum: 0x%08X\n\n", stats.checksum);
    
    // Распаковка
    uint8_t *decompressed = NULL;
    size_t decompressed_size = 0;
    
    printf("Decompressing...\n");
    ret = kolibri_decompress(compressed, compressed_size,
                             &decompressed, &decompressed_size, NULL);
    
    if (ret != 0) {
        printf("ERROR: Decompression failed with code %d\n", ret);
        
        // Выведем заголовок для отладки
        if (compressed_size >= sizeof(KolibriCompressHeader)) {
            const KolibriCompressHeader *header = (const KolibriCompressHeader *)compressed;
            printf("\nHeader analysis:\n");
            printf("  Magic: 0x%08X (expected: 0x%08X)\n", header->magic, KOLIBRI_COMPRESS_MAGIC);
            printf("  Version: %u\n", header->version);
            printf("  Methods: 0x%08X\n", header->methods);
            printf("  Original size: %zu\n", (size_t)header->original_size);
            printf("  Compressed size: %zu\n", (size_t)header->compressed_size);
            printf("  Checksum: 0x%08X\n", header->checksum);
            printf("  File type: %u\n", header->file_type);
        }
        
        free(compressed);
        kolibri_compressor_destroy(comp);
        return 1;
    }
    
    printf("Decompressed size: %zu bytes\n", decompressed_size);
    printf("Decompressed: '%s'\n\n", (char*)decompressed);
    
    // Проверка
    if (decompressed_size != input_size) {
        printf("ERROR: Size mismatch! Expected %zu, got %zu\n", input_size, decompressed_size);
        free(compressed);
        free(decompressed);
        kolibri_compressor_destroy(comp);
        return 1;
    }
    
    if (memcmp(test_data, decompressed, input_size) != 0) {
        printf("ERROR: Data mismatch!\n");
        free(compressed);
        free(decompressed);
        kolibri_compressor_destroy(comp);
        return 1;
    }
    
    printf("SUCCESS: Compression and decompression work correctly!\n");
    
    // Очистка
    free(compressed);
    free(decompressed);
    kolibri_compressor_destroy(comp);
    
    return 0;
}
