/*
 * Kolibri Ingestion Engine
 * Step 1: Data Ingestion & Normalization
 * 
 * Implements:
 * - Density Check: TextBytes / TotalBytes > 0.6
 * - Normalization: UTF-8 -> Decimal Stream
 * - Segmentation: Line/Paragraph splitting
 */

#include "kolibri/corpus.h"
#include "kolibri/digit_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>

// --- Configuration ---
#define DENSITY_THRESHOLD 0.6
#define WINDOW_SIZE 1024
#define BUFFER_SIZE 65536

// --- Stats ---
typedef struct {
    size_t files_processed;
    size_t bytes_ingested;
    size_t bytes_kept;
    size_t segments_created;
} IngestStats;

IngestStats global_stats = {0};

// --- Density Logic ---

int is_text_char(unsigned char c) {
    // Basic heuristic: common text ranges, excluding control chars (except \n, \t)
    if (c == 0x09 || c == 0x0A || c == 0x0D) return 1;
    if (c >= 0x20 && c <= 0x7E) return 1; // ASCII printable
    if (c >= 0xC0) return 1; // UTF-8 start bytes
    // (Ignoring continuation bytes for density calculation for simplicity, 
    // or counting them as valid part of text if we assume valid UTF-8 input)
    return 0;
}

double calculate_density(const char *buffer, size_t length) {
    size_t text_bytes = 0;
    for (size_t i = 0; i < length; i++) {
        if (is_text_char((unsigned char)buffer[i])) {
            text_bytes++;
        }
    }
    return (double)text_bytes / (double)length;
}

// --- Source Processing ---

void process_buffer(const char *buffer, size_t length, FILE *output_stream) {
    // Sliding window density check would be here. 
    // For this prototype, we check the whole block if it's small, 
    // or chunks of WINDOW_SIZE.
    
    size_t processed = 0;
    while (processed < length) {
        size_t chunk_size = (length - processed > WINDOW_SIZE) ? WINDOW_SIZE : (length - processed);
        double density = calculate_density(buffer + processed, chunk_size);
        
        if (density > DENSITY_THRESHOLD) {
            // Valid text block -> Normalize and Store
            
            // 1. Segmentation (Split by newline for now)
            // Real implementation would have smarter segmentation
            
            // Create a temporary null-terminated string for the chunk to parse lines
            char *chunk_copy = (char*)malloc(chunk_size + 1);
            memcpy(chunk_copy, buffer + processed, chunk_size);
            chunk_copy[chunk_size] = '\0';
            
            char *line = strtok(chunk_copy, "\n");
            while (line != NULL) {
                if (strlen(line) > 10) { // Ignore minimal lines
                    KolibriDigitText digit_text;
                    kolibri_digit_text_init(&digit_text);
                    
                    // 2. Normalization (UTF-8 -> Decimal)
                    if (kolibri_digit_text_assign_utf8(&digit_text, line) == 0) {
                        // 3. Storage (Write to output stream in raw decimal format)
                        // Format: [Length:4b][Digits...]
                        uint32_t len = (uint32_t)digit_text.length;
                        fwrite(&len, sizeof(uint32_t), 1, output_stream);
                        fwrite(digit_text.digits, 1, len, output_stream);
                        
                        global_stats.bytes_kept += len; // In decimal space
                        global_stats.segments_created++;
                    }
                    
                    kolibri_digit_text_free(&digit_text);
                }
                line = strtok(NULL, "\n");
            }
            free(chunk_copy);
        }
        
        processed += chunk_size;
    }
}

void process_file(const char *filepath, FILE *output_stream) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open: %s\n", filepath);
        return;
    }
    
    char buffer[BUFFER_SIZE];
    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, BUFFER_SIZE, f)) > 0) {
        process_buffer(buffer, read_bytes, output_stream);
        global_stats.bytes_ingested += read_bytes;
    }
    
    fclose(f);
    global_stats.files_processed++;
    printf("Processed: %s\n", filepath);
}

// --- Main ---

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <output_file> <input_file1> [input_file2 ...]\n", argv[0]);
        printf("  Processes input files, filters by density, and writes normalized decimal stream to output.\n");
        return 1;
    }
    
    const char *output_path = argv[1];
    FILE *out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "Cannot open output file: %s\n", output_path);
        return 1;
    }
    
    // Write Header
    // "KOLIBRI_RAW_V1"
    fwrite("KOLIBRI_RAW_V1", 1, 14, out);
    
    for (int i = 2; i < argc; i++) {
        process_file(argv[i], out);
    }
    
    fclose(out);
    
    printf("\n=== Ingestion Complete ===\n");
    printf("Files Processed:  %zu\n", global_stats.files_processed);
    printf("Bytes Ingested:   %zu\n", global_stats.bytes_ingested);
    printf("Segments Saved:   %zu\n", global_stats.segments_created);
    printf("Decimal Bytes:    %zu\n", global_stats.bytes_kept);
    
    return 0;
}
