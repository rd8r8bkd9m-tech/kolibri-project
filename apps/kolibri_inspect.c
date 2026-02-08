#include "kolibri/genome.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

// Must match ingest key
static const unsigned char KEY[] = "kolibri-secret-key";

// Helper to decode semantic digits back to human text
extern int k_decode_text(const char *digits, char *out, size_t out_len);

int main(int argc, char **argv) {
    const char *filename = (argc > 1) ? argv[1] : "genome.dat";
    KolibriGenome ctx;
    
    printf("=== Kolibri Genome Inspector ===\n");
    printf("Reading %s...\n\n", filename);

    if (kg_open(&ctx, filename, KEY, strlen((char*)KEY)) != 0) {
        printf("Failed to open %s (or empty).\n", filename);
        return 1;
    }

    // We need to iterate the blocks. 
    // Since kg_open reads the whole file to verify chain and set state,
    // we actually need to modify kg_open or just read manually if we want to iterate.
    // However, looking at genome.c, kg_open reads *all* blocks to verify validity 
    // but doesn't expose them unless we hook in.
    // 
    // Actually, let's just use fopen and parse manually using internal structs if header allows,
    // OR we use the file cursor.
    // But since I can't easily change the library now, I'll just write a raw parser 
    // based on the spec in genome.h/c.
    
    kg_close(&ctx); // Close the validated handle

    // RAW PARSER corresponding to backend/src/genome.c logic
    FILE *f = fopen(filename, "rb");
    if (!f) return 1;

    // Block size definition from genome.h
    // 8 (index) + 8 (ts) + 32 (prev) + 32 (hmac) + 32 (type) + 256 (payload) = 368 bytes
    #define BLOCK_SIZE 368
    #define HASH_SIZE 32
    #define TYPE_SIZE 32
    #define PAYLOAD_SIZE 256
    
    unsigned char buffer[BLOCK_SIZE];
    uint64_t count = 0;

    while (fread(buffer, 1, BLOCK_SIZE, f) == BLOCK_SIZE) {
        // Offsets
        // 0-7: Index
        // 8-15: TS
        // 16-47: Prev Hash
        // 48-79: HMAC
        // 80-111: Event Type
        // 112-367: Payload
        
        char event_type[TYPE_SIZE + 1];
        memcpy(event_type, buffer + 80, TYPE_SIZE);
        event_type[TYPE_SIZE] = 0;

        char payload_digits[PAYLOAD_SIZE + 1];
        memcpy(payload_digits, buffer + 112, PAYLOAD_SIZE);
        payload_digits[PAYLOAD_SIZE] = 0;
        
        // Decode payload
        char decoded[1024];
        k_decode_text(payload_digits, decoded, sizeof(decoded));
        
        printf("[Block %" PRIu64 "]\n", count++);
        printf("  Type: %s\n", event_type);
        printf("  Data: %s\n", decoded); // Expected: "U:url|G:gene"
        printf("--------------------------------------------------\n");
    }

    fclose(f);
    return 0;
}
