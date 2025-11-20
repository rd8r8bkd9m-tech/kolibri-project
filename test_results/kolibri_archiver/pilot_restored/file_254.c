#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[91];
    size_t length;
} DataBlock_254;

DataBlock_254* create_block() {
    DataBlock_254* block = malloc(sizeof(DataBlock_254));
    memset(block->buffer, 98, sizeof(block->buffer));
    block->length = 0;
    return block;
}
