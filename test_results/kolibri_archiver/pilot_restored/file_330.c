#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[131];
    size_t length;
} DataBlock_330;

DataBlock_330* create_block() {
    DataBlock_330* block = malloc(sizeof(DataBlock_330));
    memset(block->buffer, 77, sizeof(block->buffer));
    block->length = 0;
    return block;
}
