/*
 * Kolibri OS Archiver - Command Line Interface
 * Archive creation, extraction, and management tool
 */

#include "kolibri/compress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

static void print_usage(const char *prog) {
    printf("Kolibri OS Archiver v40 - Advanced Compression System\n\n");
    printf("Usage: %s <command> [options]\n\n", prog);
    printf("Commands:\n");
    printf("  compress <input> <output>    Compress file or directory\n");
    printf("  decompress <input> <output>  Decompress file\n");
    printf("  create <archive>             Create new archive\n");
    printf("  add <archive> <file>         Add file to archive\n");
    printf("  extract <archive> <file>     Extract file from archive\n");
    printf("  list <archive>               List archive contents\n");
    printf("  test <input>                 Test compression ratio\n");
    printf("  version                      Show version information\n");
    printf("\nOptions:\n");
    printf("  --help                       Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s compress myfile.txt myfile.klb\n", prog);
    printf("  %s decompress myfile.klb myfile.txt\n", prog);
    printf("  %s create archive.kar\n", prog);
    printf("  %s add archive.kar document.pdf\n", prog);
    printf("  %s list archive.kar\n", prog);
}

static uint8_t *read_file(const char *filename, size_t *size) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(f);
        fprintf(stderr, "Error: Invalid file size\n");
        return NULL;
    }

    uint8_t *buffer = (uint8_t *)malloc(file_size);
    if (!buffer) {
        fclose(f);
        fprintf(stderr, "Error: Memory allocation failed\n");
        return NULL;
    }

    if (fread(buffer, 1, file_size, f) != (size_t)file_size) {
        free(buffer);
        fclose(f);
        fprintf(stderr, "Error: Failed to read file\n");
        return NULL;
    }

    fclose(f);
    *size = file_size;
    return buffer;
}

static int write_file(const char *filename, const uint8_t *data, size_t size) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Error: Cannot create file '%s'\n", filename);
        return -1;
    }

    if (fwrite(data, 1, size, f) != size) {
        fclose(f);
        fprintf(stderr, "Error: Failed to write file\n");
        return -1;
    }

    fclose(f);
    return 0;
}

static void print_file_type(KolibriFileType type) {
    switch (type) {
        case KOLIBRI_FILE_TEXT:
            printf("Text");
            break;
        case KOLIBRI_FILE_BINARY:
            printf("Binary");
            break;
        case KOLIBRI_FILE_IMAGE:
            printf("Image");
            break;
        default:
            printf("Unknown");
            break;
    }
}

static void print_methods(uint32_t methods) {
    int first = 1;
    if (methods & KOLIBRI_COMPRESS_MATH) {
        printf("Mathematical");
        first = 0;
    }
    if (methods & KOLIBRI_COMPRESS_LZ77) {
        if (!first) printf("+");
        printf("LZ77");
        first = 0;
    }
    if (methods & KOLIBRI_COMPRESS_RLE) {
        if (!first) printf("+");
        printf("RLE");
        first = 0;
    }
    if (methods & KOLIBRI_COMPRESS_HUFFMAN) {
        if (!first) printf("+");
        printf("Huffman");
        first = 0;
    }
    if (methods & KOLIBRI_COMPRESS_FORMULA) {
        if (!first) printf("+");
        printf("Formula");
        first = 0;
    }
    if (first) {
        printf("None");
    }
}

static int cmd_compress(const char *input, const char *output) {
    printf("Compressing '%s' to '%s'...\n", input, output);

    size_t input_size;
    uint8_t *input_data = read_file(input, &input_size);
    if (!input_data) {
        return 1;
    }

    KolibriCompressor *comp = kolibri_compressor_create(KOLIBRI_COMPRESS_ALL);
    if (!comp) {
        free(input_data);
        fprintf(stderr, "Error: Failed to create compressor\n");
        return 1;
    }

    uint8_t *output_data = NULL;
    size_t output_size = 0;
    KolibriCompressStats stats;

    int ret = kolibri_compress(comp, input_data, input_size, 
                                &output_data, &output_size, &stats);
    kolibri_compressor_destroy(comp);
    free(input_data);

    if (ret != 0) {
        fprintf(stderr, "Error: Compression failed\n");
        return 1;
    }

    ret = write_file(output, output_data, output_size);
    free(output_data);

    if (ret != 0) {
        return 1;
    }

    printf("\nCompression complete!\n");
    printf("Original size:    %zu bytes\n", stats.original_size);
    printf("Compressed size:  %zu bytes\n", stats.compressed_size);
    printf("Compression ratio: %.2fx\n", stats.compression_ratio);
    printf("File type:        ");
    print_file_type(stats.file_type);
    printf("\nMethods used:     ");
    print_methods(stats.methods_used);
    printf("\nCompression time: %.2f ms\n", stats.compression_time_ms);
    printf("Checksum:         0x%08X\n", stats.checksum);

    return 0;
}

static int cmd_decompress(const char *input, const char *output) {
    printf("Decompressing '%s' to '%s'...\n", input, output);

    size_t input_size;
    uint8_t *input_data = read_file(input, &input_size);
    if (!input_data) {
        return 1;
    }

    printf("Read %zu bytes from input file\n", input_size);

    uint8_t *output_data = NULL;
    size_t output_size = 0;
    KolibriCompressStats stats;

    int ret = kolibri_decompress(input_data, input_size, 
                                  &output_data, &output_size, &stats);
    free(input_data);

    if (ret != 0) {
        fprintf(stderr, "Error: Decompression failed with code %d\n", ret);
        return 1;
    }

    ret = write_file(output, output_data, output_size);
    free(output_data);

    if (ret != 0) {
        return 1;
    }

    printf("\nDecompression complete!\n");
    printf("Compressed size:   %zu bytes\n", stats.compressed_size);
    printf("Decompressed size: %zu bytes\n", stats.original_size);
    printf("Compression ratio: %.2fx\n", stats.compression_ratio);
    printf("Decompression time: %.2f ms\n", stats.decompression_time_ms);
    printf("Checksum verified: 0x%08X\n", stats.checksum);

    return 0;
}

static int cmd_create(const char *archive_name) {
    printf("Creating archive '%s'...\n", archive_name);

    KolibriArchive *archive = kolibri_archive_create(archive_name);
    if (!archive) {
        fprintf(stderr, "Error: Failed to create archive\n");
        return 1;
    }

    kolibri_archive_close(archive);
    printf("Archive created successfully.\n");

    return 0;
}

static int cmd_add(const char *archive_name, const char *filename) {
    printf("Adding '%s' to archive '%s'...\n", filename, archive_name);

    KolibriArchive *archive = kolibri_archive_open(archive_name);
    if (!archive) {
        fprintf(stderr, "Error: Cannot open archive '%s'\n", archive_name);
        return 1;
    }

    size_t file_size;
    uint8_t *file_data = read_file(filename, &file_size);
    if (!file_data) {
        kolibri_archive_close(archive);
        return 1;
    }

    int ret = kolibri_archive_add_file(archive, filename, file_data, file_size);
    free(file_data);
    kolibri_archive_close(archive);

    if (ret != 0) {
        fprintf(stderr, "Error: Failed to add file to archive\n");
        return 1;
    }

    printf("File added successfully.\n");
    return 0;
}

static int cmd_extract(const char *archive_name, const char *filename) {
    printf("Extracting '%s' from archive '%s'...\n", filename, archive_name);

    KolibriArchive *archive = kolibri_archive_open(archive_name);
    if (!archive) {
        fprintf(stderr, "Error: Cannot open archive '%s'\n", archive_name);
        return 1;
    }

    uint8_t *file_data = NULL;
    size_t file_size = 0;

    int ret = kolibri_archive_extract_file(archive, filename, &file_data, &file_size);
    kolibri_archive_close(archive);

    if (ret != 0) {
        fprintf(stderr, "Error: Failed to extract file from archive\n");
        return 1;
    }

    ret = write_file(filename, file_data, file_size);
    free(file_data);

    if (ret != 0) {
        return 1;
    }

    printf("File extracted successfully.\n");
    return 0;
}

static int cmd_list(const char *archive_name) {
    printf("Listing contents of archive '%s':\n\n", archive_name);

    KolibriArchive *archive = kolibri_archive_open(archive_name);
    if (!archive) {
        fprintf(stderr, "Error: Cannot open archive '%s'\n", archive_name);
        return 1;
    }

    KolibriArchiveEntry *entries = NULL;
    size_t count = 0;

    int ret = kolibri_archive_list(archive, &entries, &count);
    kolibri_archive_close(archive);

    if (ret != 0) {
        fprintf(stderr, "Error: Failed to list archive contents\n");
        return 1;
    }

    if (count == 0) {
        printf("Archive is empty.\n");
        return 0;
    }

    printf("%-40s %12s %12s %8s %s\n", 
           "Name", "Original", "Compressed", "Ratio", "Type");
    printf("--------------------------------------------------------------------------------\n");

    for (size_t i = 0; i < count; i++) {
        double ratio = (double)entries[i].original_size / (double)entries[i].compressed_size;
        printf("%-40s %12zu %12zu %7.2fx ", 
               entries[i].name,
               entries[i].original_size,
               entries[i].compressed_size,
               ratio);
        print_file_type(entries[i].type);
        printf("\n");
    }

    printf("--------------------------------------------------------------------------------\n");
    printf("Total files: %zu\n", count);

    free(entries);
    return 0;
}

static int cmd_version(void) {
    printf("Kolibri OS Archiver\n");
    printf("Version: v40.0.0\n");
    printf("Build date: %s %s\n", __DATE__, __TIME__);
    printf("\nSupported compression methods:\n");
    printf("  - LZ77 (Dictionary-based)\n");
    printf("  - RLE (Run-Length Encoding)\n");
    printf("  - Huffman (Entropy coding)\n");
    printf("  - Mathematical Analysis\n");
    printf("  - Formula-based Encoding\n");
    printf("  - LZMA (v40 new)\n");
    printf("  - Zstandard (v40 new)\n");
    printf("  - Adaptive Dictionary (v40 new)\n");
    printf("\nFeatures:\n");
    printf("  - Multi-layer compression\n");
    printf("  - Automatic file type detection\n");
    printf("  - CRC32 checksum validation\n");
    printf("  - Multi-file archive support\n");
    printf("  - Cross-platform compatibility\n");
    return 0;
}

static int cmd_test(const char *input) {
    printf("Testing compression on '%s'...\n", input);

    size_t input_size;
    uint8_t *input_data = read_file(input, &input_size);
    if (!input_data) {
        return 1;
    }

    KolibriCompressor *comp = kolibri_compressor_create(KOLIBRI_COMPRESS_ALL);
    if (!comp) {
        free(input_data);
        fprintf(stderr, "Error: Failed to create compressor\n");
        return 1;
    }

    uint8_t *compressed_data = NULL;
    size_t compressed_size = 0;
    KolibriCompressStats stats;

    int ret = kolibri_compress(comp, input_data, input_size, 
                                &compressed_data, &compressed_size, &stats);
    if (ret != 0) {
        kolibri_compressor_destroy(comp);
        free(input_data);
        fprintf(stderr, "Error: Compression failed\n");
        return 1;
    }

    /* Test decompression */
    uint8_t *decompressed_data = NULL;
    size_t decompressed_size = 0;

    ret = kolibri_decompress(compressed_data, compressed_size,
                             &decompressed_data, &decompressed_size, NULL);
    free(compressed_data);

    if (ret != 0) {
        kolibri_compressor_destroy(comp);
        free(input_data);
        fprintf(stderr, "Error: Decompression failed\n");
        return 1;
    }

    /* Verify integrity */
    int match = (decompressed_size == input_size &&
                 memcmp(input_data, decompressed_data, input_size) == 0);

    free(input_data);
    free(decompressed_data);
    kolibri_compressor_destroy(comp);

    printf("\nTest Results:\n");
    printf("Original size:     %zu bytes\n", stats.original_size);
    printf("Compressed size:   %zu bytes\n", stats.compressed_size);
    printf("Compression ratio: %.2fx\n", stats.compression_ratio);
    printf("File type:         ");
    print_file_type(stats.file_type);
    printf("\nMethods used:      ");
    print_methods(stats.methods_used);
    printf("\nCompression time:  %.2f ms\n", stats.compression_time_ms);
    printf("Data integrity:    %s\n", match ? "PASSED ✓" : "FAILED ✗");

    return match ? 0 : 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(cmd, "compress") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Usage: %s compress <input> <output>\n", argv[0]);
            return 1;
        }
        return cmd_compress(argv[2], argv[3]);
    }

    if (strcmp(cmd, "decompress") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Usage: %s decompress <input> <output>\n", argv[0]);
            return 1;
        }
        return cmd_decompress(argv[2], argv[3]);
    }

    if (strcmp(cmd, "create") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: %s create <archive>\n", argv[0]);
            return 1;
        }
        return cmd_create(argv[2]);
    }

    if (strcmp(cmd, "add") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Usage: %s add <archive> <file>\n", argv[0]);
            return 1;
        }
        return cmd_add(argv[2], argv[3]);
    }

    if (strcmp(cmd, "extract") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Usage: %s extract <archive> <file>\n", argv[0]);
            return 1;
        }
        return cmd_extract(argv[2], argv[3]);
    }

    if (strcmp(cmd, "list") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: %s list <archive>\n", argv[0]);
            return 1;
        }
        return cmd_list(argv[2]);
    }

    if (strcmp(cmd, "test") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: %s test <input>\n", argv[0]);
            return 1;
        }
        return cmd_test(argv[2]);
    }

    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "-v") == 0 || strcmp(cmd, "--version") == 0) {
        return cmd_version();
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    print_usage(argv[0]);
    return 1;
}
