#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kolibri_core.h"

void print_usage(const char* program_name) {
    printf("Kolibri AI CLI - High-performance compression tool\n");
    printf("Usage: %s [options] <input_file> <output_file>\n\n", program_name);
    printf("Options:\n");
    printf("  -c, --compress    Compress the input file (default)\n");
    printf("  -d, --decompress  Decompress the input file\n");
    printf("  -o, --optimize    Optimization level (0-3, default: 2)\n");
    printf("  -t, --threads     Number of threads (default: 1)\n");
    printf("  -s, --stats       Show statistics after operation\n");
    printf("  -h, --help        Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s -c document.txt document.kgen\n", program_name);
    printf("  %s -d document.kgen document_restored.txt\n", program_name);
    printf("  %s -c -o 3 -t 4 large_file.bin compressed.kgen\n", program_name);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    KolibriOperation operation = KOLIBRI_OP_COMPRESS;
    int optimization_level = 2;
    size_t thread_count = 1;
    bool show_stats = false;
    const char* input_file = NULL;
    const char* output_file = NULL;

    // Парсинг аргументов
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--compress") == 0) {
            operation = KOLIBRI_OP_COMPRESS;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--decompress") == 0) {
            operation = KOLIBRI_OP_DECOMPRESS;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--optimize") == 0) {
            if (i + 1 < argc) {
                optimization_level = atoi(argv[++i]);
                if (optimization_level < 0 || optimization_level > 3) {
                    fprintf(stderr, "Error: Optimization level must be between 0 and 3\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: Missing value for optimization level\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) {
            if (i + 1 < argc) {
                thread_count = (size_t)atol(argv[++i]);
                if (thread_count == 0) {
                    fprintf(stderr, "Error: Thread count must be at least 1\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: Missing value for thread count\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--stats") == 0) {
            show_stats = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            if (!input_file) {
                input_file = argv[i];
            } else if (!output_file) {
                output_file = argv[i];
            } else {
                fprintf(stderr, "Error: Too many file arguments\n");
                return 1;
            }
        } else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!input_file || !output_file) {
        fprintf(stderr, "Error: Both input and output files must be specified\n");
        print_usage(argv[0]);
        return 1;
    }

    // Чтение входного файла
    FILE* f_in = fopen(input_file, "rb");
    if (!f_in) {
        fprintf(stderr, "Error: Cannot open input file '%s'\n", input_file);
        return 1;
    }

    fseek(f_in, 0, SEEK_END);
    long file_size = ftell(f_in);
    fseek(f_in, 0, SEEK_SET);

    if (file_size <= 0 || file_size > KOLIBRI_MAX_BLOCK_SIZE) {
        fprintf(stderr, "Error: File size must be between 1 and %d bytes\n", KOLIBRI_MAX_BLOCK_SIZE);
        fclose(f_in);
        return 1;
    }

    uint8_t* input_data = (uint8_t*)malloc(file_size);
    if (!input_data) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(f_in);
        return 1;
    }

    size_t bytes_read = fread(input_data, 1, file_size, f_in);
    fclose(f_in);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "Error: Failed to read entire file\n");
        free(input_data);
        return 1;
    }

    printf("Kolibri AI v%d.%d.%d\n", KOLIBRI_VERSION_MAJOR, KOLIBRI_VERSION_MINOR, KOLIBRI_VERSION_PATCH);
    printf("Processing: %s -> %s\n", input_file, output_file);
    printf("Operation: %s | Optimization: %d | Threads: %zu\n", 
           operation == KOLIBRI_OP_COMPRESS ? "Compress" : "Decompress",
           optimization_level, thread_count);

    // Инициализация движка
    KolibriEngine engine;
    KolibriStatus status = kolibri_init(&engine, optimization_level);
    if (status != KOLIBRI_STATUS_OK) {
        fprintf(stderr, "Error: Failed to initialize engine: %s\n", kolibri_status_to_string(status));
        free(input_data);
        return 1;
    }

    // Установка количества потоков
    status = kolibri_set_thread_count(&engine, thread_count);
    if (status != KOLIBRI_STATUS_OK) {
        fprintf(stderr, "Warning: Failed to set thread count: %s\n", kolibri_status_to_string(status));
    }

    // Выполнение операции
    KolibriResult result = kolibri_process(&engine, input_data, bytes_read, operation);
    
    if (result.status != KOLIBRI_STATUS_OK) {
        fprintf(stderr, "Error: Processing failed: %s\n", result.error_message);
        kolibri_cleanup(&engine);
        free(input_data);
        return 1;
    }

    // Запись выходного файла
    FILE* f_out = fopen(output_file, "wb");
    if (!f_out) {
        fprintf(stderr, "Error: Cannot open output file '%s'\n", output_file);
        kolibri_free_result(&result);
        kolibri_cleanup(&engine);
        free(input_data);
        return 1;
    }

    size_t bytes_written = fwrite(result.data, 1, result.size, f_out);
    fclose(f_out);

    if (bytes_written != result.size) {
        fprintf(stderr, "Error: Failed to write entire output file\n");
        kolibri_free_result(&result);
        kolibri_cleanup(&engine);
        free(input_data);
        return 1;
    }

    // Вывод результатов
    printf("Success!\n");
    printf("Original size: %zu bytes\n", result.original_size);
    printf("Result size: %zu bytes\n", result.size);
    printf("Compression ratio: %.2f\n", result.compression_ratio);
    printf("Processing time: %.2f ms\n", result.processing_time_ms);

    if (show_stats) {
        KolibriStats stats = kolibri_get_stats(&engine);
        printf("\n--- Statistics ---\n");
        printf("Total operations: %zu\n", stats.total_operations);
        printf("Total compressed: %zu\n", stats.total_compressed);
        printf("Total decompressed: %zu\n", stats.total_decompressed);
        printf("Bytes saved: %zu\n", stats.total_bytes_saved);
        printf("Average compression ratio: %.2f\n", stats.avg_compression_ratio);
        printf("Total processing time: %.2f ms\n", stats.total_processing_time_ms);
    }

    // Очистка
    kolibri_free_result(&result);
    kolibri_cleanup(&engine);
    free(input_data);

    return 0;
}
