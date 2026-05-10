#include "backend/include/kolibri/compress.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t methods;
    uint32_t compressed_size;
    uint32_t original_size;
    uint32_t checksum;
    uint32_t file_type;
    uint8_t reserved[36];
} KolibriCompressHeader;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.klb>\n", argv[0]);
        return 1;
    }
    
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "Cannot open file: %s\n", argv[1]);
        return 1;
    }
    
    KolibriCompressHeader header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        fprintf(stderr, "Cannot read header\n");
        fclose(f);
        return 1;
    }
    
    printf("=== Kolibri Compressed File Header ===\n");
    printf("Magic:           0x%08X (%c%c%c%c)\n", header.magic,
           (header.magic >> 0) & 0xFF,
           (header.magic >> 8) & 0xFF,
           (header.magic >> 16) & 0xFF,
           (header.magic >> 24) & 0xFF);
    printf("Version:         %u\n", header.version);
    printf("Methods:         0x%08X\n", header.methods);
    printf("Compressed size: %u bytes\n", header.compressed_size);
    printf("Original size:   %u bytes\n", header.original_size);
    printf("Checksum:        0x%08X\n", header.checksum);
    printf("File type:       %u\n", header.file_type);
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    printf("\nActual file size: %ld bytes\n", file_size);
    printf("Header size:      %zu bytes\n", sizeof(header));
    printf("Data size:        %ld bytes\n", file_size - sizeof(header));
    
    fclose(f);
    return 0;
}
