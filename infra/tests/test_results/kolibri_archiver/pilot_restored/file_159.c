#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[221];
    size_t length;
} DataBlock_159;

DataBlock_159* create_block() {
    DataBlock_159* block = malloc(sizeof(DataBlock_159));
    memset(block->buffer, 66, sizeof(block->buffer));
    block->length = 0;
    return block;
}
