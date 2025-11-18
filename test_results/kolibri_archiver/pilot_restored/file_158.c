#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[51];
    size_t length;
} DataBlock_158;

DataBlock_158* create_block() {
    DataBlock_158* block = malloc(sizeof(DataBlock_158));
    memset(block->buffer, 58, sizeof(block->buffer));
    block->length = 0;
    return block;
}
