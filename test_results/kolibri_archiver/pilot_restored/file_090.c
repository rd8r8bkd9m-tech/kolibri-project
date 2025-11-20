#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[51];
    size_t length;
} DataBlock_90;

DataBlock_90* create_block() {
    DataBlock_90* block = malloc(sizeof(DataBlock_90));
    memset(block->buffer, 47, sizeof(block->buffer));
    block->length = 0;
    return block;
}
