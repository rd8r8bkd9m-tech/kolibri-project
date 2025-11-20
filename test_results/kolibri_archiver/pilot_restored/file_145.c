#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[161];
    size_t length;
} DataBlock_145;

DataBlock_145* create_block() {
    DataBlock_145* block = malloc(sizeof(DataBlock_145));
    memset(block->buffer, 48, sizeof(block->buffer));
    block->length = 0;
    return block;
}
