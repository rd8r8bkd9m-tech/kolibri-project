#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[51];
    size_t length;
} DataBlock_396;

DataBlock_396* create_block() {
    DataBlock_396* block = malloc(sizeof(DataBlock_396));
    memset(block->buffer, 114, sizeof(block->buffer));
    block->length = 0;
    return block;
}
