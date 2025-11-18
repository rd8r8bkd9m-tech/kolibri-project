#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[121];
    size_t length;
} DataBlock_129;

DataBlock_129* create_block() {
    DataBlock_129* block = malloc(sizeof(DataBlock_129));
    memset(block->buffer, 74, sizeof(block->buffer));
    block->length = 0;
    return block;
}
