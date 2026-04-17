#include "kolibri/k_alloc.h"
#include <stdlib.h>
#include <string.h>

void* k_malloc(size_t size) {
    return malloc(size);
}

void k_free(void* ptr) {
    if (ptr) {
        free(ptr);
    }
}

k_mem_block* k_alloc_decimal_block(const char *text) {
    if (!text) return NULL;
    
    k_mem_block *block = (k_mem_block*)malloc(sizeof(k_mem_block));
    if (!block) return NULL;
    
    size_t text_len = strlen(text);
    /* 1 char = 3 digits => max digits = text_len * 3 */
    size_t max_digits = text_len * 3 + 1;
    block->data = (uint8_t*)malloc(max_digits);
    
    if (!block->data) {
        free(block);
        return NULL;
    }
    
    k_digit_stream stream;
    k_digit_stream_init(&stream, block->data, max_digits);
    k_transduce_utf8(&stream, (const unsigned char*)text, text_len);
    
    block->original_size = stream.length;
    
    /* Phase 1.1: Triplet Compression on-the-fly in allocator */
    int packed = k_triplet_pack(&stream);
    
    block->has_triplets = (packed > 0);
    block->packed_size = stream.length; /* In a real hardware compression this would be smaller */
    
    return block;
}

void k_free_decimal_block(k_mem_block *block) {
    if (block) {
        if (block->data) {
            free(block->data);
        }
        free(block);
    }
}
