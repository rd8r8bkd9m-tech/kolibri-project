#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[51];
    size_t length;
} DataBlock_14;

DataBlock_14* create_block() {
    DataBlock_14* block = malloc(sizeof(DataBlock_14));
    memset(block->buffer, 77, sizeof(block->buffer));
    block->length = 0;
    return block;
}
