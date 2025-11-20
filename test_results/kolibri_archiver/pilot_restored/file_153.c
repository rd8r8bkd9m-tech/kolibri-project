#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[121];
    size_t length;
} DataBlock_153;

DataBlock_153* create_block() {
    DataBlock_153* block = malloc(sizeof(DataBlock_153));
    memset(block->buffer, 30, sizeof(block->buffer));
    block->length = 0;
    return block;
}
