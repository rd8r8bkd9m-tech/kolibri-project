#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[51];
    size_t length;
} DataBlock_270;

DataBlock_270* create_block() {
    DataBlock_270* block = malloc(sizeof(DataBlock_270));
    memset(block->buffer, 27, sizeof(block->buffer));
    block->length = 0;
    return block;
}
