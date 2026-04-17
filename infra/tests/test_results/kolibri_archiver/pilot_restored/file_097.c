#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[161];
    size_t length;
} DataBlock_97;

DataBlock_97* create_block() {
    DataBlock_97* block = malloc(sizeof(DataBlock_97));
    memset(block->buffer, 101, sizeof(block->buffer));
    block->length = 0;
    return block;
}
