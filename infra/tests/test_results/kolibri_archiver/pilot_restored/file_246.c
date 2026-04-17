#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[91];
    size_t length;
} DataBlock_246;

DataBlock_246* create_block() {
    DataBlock_246* block = malloc(sizeof(DataBlock_246));
    memset(block->buffer, 146, sizeof(block->buffer));
    block->length = 0;
    return block;
}
