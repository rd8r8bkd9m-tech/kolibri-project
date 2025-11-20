#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[161];
    size_t length;
} DataBlock_13;

DataBlock_13* create_block() {
    DataBlock_13* block = malloc(sizeof(DataBlock_13));
    memset(block->buffer, 72, sizeof(block->buffer));
    block->length = 0;
    return block;
}
