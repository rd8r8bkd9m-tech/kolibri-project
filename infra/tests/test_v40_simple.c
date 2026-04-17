#include "backend/include/kolibri/compress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    printf("=== Kolibri v40 Simple Test ===\n\n");
    
    const char *test_data = "Hello, World! This is a test of Kolibri v40 archiver.";
    size_t test_size = strlen(test_data);
    
    printf("Input data: \"%s\"\n", test_data);
    printf("Input size: %zu bytes\n\n", test_size);
    
    // Compress
    KolibriCompressor *comp = kolibri_compressor_create(KOLIBRI_COMPRESS_ALL);
    if (!comp) {
        fprintf(stderr, "Failed to create compressor\n");
        return 1;
    }
    
    uint8_t *compressed = NULL;
    size_t compressed_size = 0;
    KolibriCompressStats stats;
    
    int ret = kolibri_compress(comp, (const uint8_t *)test_data, test_size,
                                &compressed, &compressed_size, &stats);
    kolibri_compressor_destroy(comp);
    
    if (ret != 0) {
        fprintf(stderr, "Compression failed\n");
        return 1;
    }
    
    printf("Compression successful!\n");
    printf("  Original size: %zu bytes\n", stats.original_size);
    printf("  Compressed size: %zu bytes\n", stats.compressed_size);
    printf("  Ratio: %.2fx\n", stats.compression_ratio);
    printf("  Methods: 0x%08X\n", stats.methods_used);
    printf("  Checksum: 0x%08X\n\n", stats.checksum);
    
    // Decompress
    uint8_t *decompressed = NULL;
    size_t decompressed_size = 0;
    
    ret = kolibri_decompress(compressed, compressed_size,
                             &decompressed, &decompressed_size, NULL);
    
    if (ret != 0) {
        fprintf(stderr, "Decompression failed with code %d\n", ret);
        free(compressed);
        return 1;
    }
    
    printf("Decompression successful!\n");
    printf("  Decompressed size: %zu bytes\n", decompressed_size);
    
    // Verify
    if (decompressed_size != test_size) {
        fprintf(stderr, "Size mismatch: expected %zu, got %zu\n", test_size, decompressed_size);
        free(compressed);
        free(decompressed);
        return 1;
    }
    
    if (memcmp(test_data, decompressed, test_size) != 0) {
        fprintf(stderr, "Data mismatch!\n");
        fprintf(stderr, "Expected: \"%s\"\n", test_data);
        fprintf(stderr, "Got: \"");
        fwrite(decompressed, 1, decompressed_size, stderr);
        fprintf(stderr, "\"\n");
        free(compressed);
        free(decompressed);
        return 1;
    }
    
    printf("  Data verified: MATCH ✓\n\n");
    printf("=== All tests passed! ===\n");
    
    free(compressed);
    free(decompressed);
    
    return 0;
}
