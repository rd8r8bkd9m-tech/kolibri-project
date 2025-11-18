#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[131];
    size_t length;
} DataBlock_200;

DataBlock_200* create_block() {
    DataBlock_200* block = malloc(sizeof(DataBlock_200));
    memset(block->buffer, 154, sizeof(block->buffer));
    block->length = 0;
    return block;
}
