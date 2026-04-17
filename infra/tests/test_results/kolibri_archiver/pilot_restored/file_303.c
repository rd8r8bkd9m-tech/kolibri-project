#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[141];
    size_t length;
} DataBlock_303;

DataBlock_303* create_block() {
    DataBlock_303* block = malloc(sizeof(DataBlock_303));
    memset(block->buffer, 43, sizeof(block->buffer));
    block->length = 0;
    return block;
}
