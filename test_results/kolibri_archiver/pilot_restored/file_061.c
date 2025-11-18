#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[121];
    size_t length;
} DataBlock_61;

DataBlock_61* create_block() {
    DataBlock_61* block = malloc(sizeof(DataBlock_61));
    memset(block->buffer, 11, sizeof(block->buffer));
    block->length = 0;
    return block;
}
