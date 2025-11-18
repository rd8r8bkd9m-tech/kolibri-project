#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[51];
    size_t length;
} DataBlock_236;

DataBlock_236* create_block() {
    DataBlock_236* block = malloc(sizeof(DataBlock_236));
    memset(block->buffer, 70, sizeof(block->buffer));
    block->length = 0;
    return block;
}
