#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[101];
    size_t length;
} DataBlock_163;

DataBlock_163* create_block() {
    DataBlock_163* block = malloc(sizeof(DataBlock_163));
    memset(block->buffer, 26, sizeof(block->buffer));
    block->length = 0;
    return block;
}
