#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[101];
    size_t length;
} DataBlock_115;

DataBlock_115* create_block() {
    DataBlock_115* block = malloc(sizeof(DataBlock_115));
    memset(block->buffer, 79, sizeof(block->buffer));
    block->length = 0;
    return block;
}
