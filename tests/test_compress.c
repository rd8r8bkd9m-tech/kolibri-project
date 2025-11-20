/*
 * Kolibri OS Archiver - Unit Tests
 * Tests for compression, decompression, and archive management
 */

#include "kolibri/compress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TEST_PASSED() printf("  ✓ PASSED\n")
#define TEST_FAILED(msg) do { \
    printf("  ✗ FAILED: %s\n", msg); \
    return 1; \
} while(0)

/* Test helpers */
static int compare_data(const uint8_t *a, const uint8_t *b, size_t size) {
    return memcmp(a, b, size) == 0;
}

/* Test 1: Basic compression and decompression */
static int test_basic_compress_decompress(void) {
    printf("Test 1: Basic compression/decompression...\n");

    const char *test_data = "Hello, Kolibri OS! This is a test of the compression system.";
    size_t test_size = strlen(test_data);

    KolibriCompressor *comp = kolibri_compressor_create(KOLIBRI_COMPRESS_ALL);
    if (!comp) TEST_FAILED("Failed to create compressor");

    uint8_t *compressed = NULL;
    size_t compressed_size = 0;
    KolibriCompressStats stats;

    int ret = kolibri_compress(comp, (const uint8_t *)test_data, test_size,
                                &compressed, &compressed_size, &stats);
    kolibri_compressor_destroy(comp);

    if (ret != 0) TEST_FAILED("Compression failed");
    if (!compressed) TEST_FAILED("Compressed data is NULL");

    uint8_t *decompressed = NULL;
    size_t decompressed_size = 0;

    ret = kolibri_decompress(compressed, compressed_size,
                             &decompressed, &decompressed_size, NULL);
    free(compressed);

    if (ret != 0) {
        free(decompressed);
        TEST_FAILED("Decompression failed");
    }

    if (decompressed_size != test_size) {
        free(decompressed);
        TEST_FAILED("Decompressed size mismatch");
    }

    if (!compare_data((const uint8_t *)test_data, decompressed, test_size)) {
        free(decompressed);
        TEST_FAILED("Decompressed data mismatch");
    }

    free(decompressed);
    TEST_PASSED();
    return 0;
}

/* Test 2: Compression ratio */
static int test_compression_ratio(void) {
    printf("Test 2: Compression ratio...\n");

    /* Create repetitive data that should compress well */
    size_t test_size = 10000;
    uint8_t *test_data = (uint8_t *)malloc(test_size);
    if (!test_data) TEST_FAILED("Memory allocation failed");

    for (size_t i = 0; i < test_size; i++) {
        test_data[i] = (uint8_t)(i % 10); /* Repetitive pattern */
    }

    KolibriCompressor *comp = kolibri_compressor_create(KOLIBRI_COMPRESS_ALL);
    if (!comp) {
        free(test_data);
        TEST_FAILED("Failed to create compressor");
    }

    uint8_t *compressed = NULL;
    size_t compressed_size = 0;
    KolibriCompressStats stats;

    int ret = kolibri_compress(comp, test_data, test_size,
                                &compressed, &compressed_size, &stats);
    kolibri_compressor_destroy(comp);

    if (ret != 0) {
        free(test_data);
        TEST_FAILED("Compression failed");
    }

    printf("    Original size: %zu bytes\n", stats.original_size);
    printf("    Compressed size: %zu bytes\n", stats.compressed_size);
    printf("    Ratio: %.2fx\n", stats.compression_ratio);

    /* Verify decompression */
    uint8_t *decompressed = NULL;
    size_t decompressed_size = 0;

    ret = kolibri_decompress(compressed, compressed_size,
                             &decompressed, &decompressed_size, NULL);
    free(compressed);

    if (ret != 0 || !compare_data(test_data, decompressed, test_size)) {
        free(test_data);
        free(decompressed);
        TEST_FAILED("Decompression verification failed");
    }

    free(test_data);
    free(decompressed);

    if (stats.compression_ratio < 1.5) {
        TEST_FAILED("Compression ratio too low for repetitive data");
    }

    TEST_PASSED();
    return 0;
}

/* Test 3: File type detection */
static int test_file_type_detection(void) {
    printf("Test 3: File type detection...\n");

    /* Test text detection */
    const char *text_data = "This is plain text content.\n";
    KolibriFileType type = kolibri_detect_file_type((const uint8_t *)text_data, strlen(text_data));
    if (type != KOLIBRI_FILE_TEXT) TEST_FAILED("Text detection failed");

    /* Test binary detection */
    uint8_t binary_data[] = {0x00, 0x01, 0x02, 0x03, 0xFF, 0xFE, 0xFD, 0xFC};
    type = kolibri_detect_file_type(binary_data, sizeof(binary_data));
    if (type != KOLIBRI_FILE_BINARY) TEST_FAILED("Binary detection failed");

    /* Test PNG detection */
    uint8_t png_data[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A};
    type = kolibri_detect_file_type(png_data, sizeof(png_data));
    if (type != KOLIBRI_FILE_IMAGE) TEST_FAILED("PNG detection failed");

    TEST_PASSED();
    return 0;
}

/* Test 4: Checksum verification */
static int test_checksum(void) {
    printf("Test 4: Checksum verification...\n");

    const char *data1 = "Test data for checksum";
    const char *data2 = "Test data for checksum";
    const char *data3 = "Different test data";

    uint32_t crc1 = kolibri_checksum((const uint8_t *)data1, strlen(data1));
    uint32_t crc2 = kolibri_checksum((const uint8_t *)data2, strlen(data2));
    uint32_t crc3 = kolibri_checksum((const uint8_t *)data3, strlen(data3));

    if (crc1 != crc2) TEST_FAILED("Identical data produced different checksums");
    if (crc1 == crc3) TEST_FAILED("Different data produced identical checksums");

    TEST_PASSED();
    return 0;
}

/* Test 5: Large data compression */
static int test_large_data(void) {
    printf("Test 5: Large data compression...\n");

    size_t test_size = 1024 * 1024; /* 1 MB */
    uint8_t *test_data = (uint8_t *)malloc(test_size);
    if (!test_data) TEST_FAILED("Memory allocation failed");

    /* Fill with pseudo-random but compressible data */
    for (size_t i = 0; i < test_size; i++) {
        test_data[i] = (uint8_t)((i * 7 + 13) % 256);
    }

    KolibriCompressor *comp = kolibri_compressor_create(KOLIBRI_COMPRESS_ALL);
    if (!comp) {
        free(test_data);
        TEST_FAILED("Failed to create compressor");
    }

    uint8_t *compressed = NULL;
    size_t compressed_size = 0;
    KolibriCompressStats stats;

    int ret = kolibri_compress(comp, test_data, test_size,
                                &compressed, &compressed_size, &stats);
    kolibri_compressor_destroy(comp);

    if (ret != 0) {
        free(test_data);
        TEST_FAILED("Compression failed");
    }

    printf("    Original size: %zu bytes (%.2f MB)\n", 
           stats.original_size, stats.original_size / (1024.0 * 1024.0));
    printf("    Compressed size: %zu bytes (%.2f MB)\n",
           stats.compressed_size, stats.compressed_size / (1024.0 * 1024.0));
    printf("    Ratio: %.2fx\n", stats.compression_ratio);

    /* Verify decompression */
    uint8_t *decompressed = NULL;
    size_t decompressed_size = 0;

    ret = kolibri_decompress(compressed, compressed_size,
                             &decompressed, &decompressed_size, NULL);
    free(compressed);

    if (ret != 0 || !compare_data(test_data, decompressed, test_size)) {
        free(test_data);
        free(decompressed);
        TEST_FAILED("Decompression verification failed");
    }

    free(test_data);
    free(decompressed);

    TEST_PASSED();
    return 0;
}

/* Test 6: Archive creation and extraction */
static int test_archive_operations(void) {
    printf("Test 6: Archive operations...\n");

    const char *archive_name = "/tmp/test_archive.kar";
    const char *file1_name = "test_file1.txt";
    const char *file1_data = "Content of file 1";
    const char *file2_name = "test_file2.txt";
    const char *file2_data = "Content of file 2 with more text";

    /* Create archive */
    KolibriArchive *archive = kolibri_archive_create(archive_name);
    if (!archive) TEST_FAILED("Failed to create archive");

    /* Add files */
    int ret = kolibri_archive_add_file(archive, file1_name,
                                       (const uint8_t *)file1_data,
                                       strlen(file1_data));
    if (ret != 0) {
        kolibri_archive_close(archive);
        TEST_FAILED("Failed to add file 1");
    }

    ret = kolibri_archive_add_file(archive, file2_name,
                                   (const uint8_t *)file2_data,
                                   strlen(file2_data));
    if (ret != 0) {
        kolibri_archive_close(archive);
        TEST_FAILED("Failed to add file 2");
    }

    kolibri_archive_close(archive);

    /* Open and list archive */
    archive = kolibri_archive_open(archive_name);
    if (!archive) TEST_FAILED("Failed to open archive");

    KolibriArchiveEntry *entries = NULL;
    size_t count = 0;
    ret = kolibri_archive_list(archive, &entries, &count);
    if (ret != 0 || count != 2) {
        kolibri_archive_close(archive);
        TEST_FAILED("Failed to list archive or wrong entry count");
    }
    free(entries);

    /* Extract and verify files */
    uint8_t *extracted_data = NULL;
    size_t extracted_size = 0;

    ret = kolibri_archive_extract_file(archive, file1_name,
                                       &extracted_data, &extracted_size);
    if (ret != 0) {
        kolibri_archive_close(archive);
        TEST_FAILED("Failed to extract file 1");
    }

    if (extracted_size != strlen(file1_data) ||
        !compare_data((const uint8_t *)file1_data, extracted_data, extracted_size)) {
        free(extracted_data);
        kolibri_archive_close(archive);
        TEST_FAILED("Extracted file 1 data mismatch");
    }
    free(extracted_data);

    ret = kolibri_archive_extract_file(archive, file2_name,
                                       &extracted_data, &extracted_size);
    if (ret != 0) {
        kolibri_archive_close(archive);
        TEST_FAILED("Failed to extract file 2");
    }

    if (extracted_size != strlen(file2_data) ||
        !compare_data((const uint8_t *)file2_data, extracted_data, extracted_size)) {
        free(extracted_data);
        kolibri_archive_close(archive);
        TEST_FAILED("Extracted file 2 data mismatch");
    }
    free(extracted_data);

    kolibri_archive_close(archive);

    /* Clean up */
    remove(archive_name);

    TEST_PASSED();
    return 0;
}

/* Test 7: Empty data handling */
static int test_empty_data(void) {
    printf("Test 7: Empty data handling...\n");

    uint8_t empty_data[1] = {0};
    
    KolibriCompressor *comp = kolibri_compressor_create(KOLIBRI_COMPRESS_ALL);
    if (!comp) TEST_FAILED("Failed to create compressor");

    uint8_t *compressed = NULL;
    size_t compressed_size = 0;

    int ret = kolibri_compress(comp, empty_data, 1,
                                &compressed, &compressed_size, NULL);
    kolibri_compressor_destroy(comp);

    if (ret != 0) TEST_FAILED("Compression of small data failed");

    uint8_t *decompressed = NULL;
    size_t decompressed_size = 0;

    ret = kolibri_decompress(compressed, compressed_size,
                             &decompressed, &decompressed_size, NULL);
    free(compressed);

    if (ret != 0) {
        free(decompressed);
        TEST_FAILED("Decompression failed");
    }

    free(decompressed);
    TEST_PASSED();
    return 0;
}

/* Test 8: Method selection */
static int test_method_selection(void) {
    printf("Test 8: Method selection...\n");

    const char *test_data = "Test data with some repetition: AAAAAAAAAA BBBBBBBBBB";
    size_t test_size = strlen(test_data);

    /* Test with only RLE */
    KolibriCompressor *comp = kolibri_compressor_create(KOLIBRI_COMPRESS_RLE);
    if (!comp) TEST_FAILED("Failed to create compressor");

    uint8_t *compressed = NULL;
    size_t compressed_size = 0;
    KolibriCompressStats stats;

    int ret = kolibri_compress(comp, (const uint8_t *)test_data, test_size,
                                &compressed, &compressed_size, &stats);
    kolibri_compressor_destroy(comp);

    if (ret != 0) TEST_FAILED("Compression with RLE only failed");

    /* Verify decompression works */
    uint8_t *decompressed = NULL;
    size_t decompressed_size = 0;

    ret = kolibri_decompress(compressed, compressed_size,
                             &decompressed, &decompressed_size, NULL);
    free(compressed);

    if (ret != 0 || !compare_data((const uint8_t *)test_data, decompressed, test_size)) {
        free(decompressed);
        TEST_FAILED("Decompression verification failed");
    }

    free(decompressed);
    TEST_PASSED();
    return 0;
}

int main(void) {
    printf("=== Kolibri OS Archiver Unit Tests ===\n\n");

    int failed = 0;

    failed += test_basic_compress_decompress();
    failed += test_compression_ratio();
    failed += test_file_type_detection();
    failed += test_checksum();
    failed += test_large_data();
    failed += test_archive_operations();
    failed += test_empty_data();
    failed += test_method_selection();

    printf("\n=== Test Summary ===\n");
    if (failed == 0) {
        printf("All tests passed! ✓\n");
        return 0;
    } else {
        printf("%d test(s) failed. ✗\n", failed);
        return 1;
    }
}
