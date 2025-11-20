#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[51];
    size_t length;
} DataBlock_354;

DataBlock_354* create_block() {
    DataBlock_354* block = malloc(sizeof(DataBlock_354));
    memset(block->buffer, 14, sizeof(block->buffer));
    block->length = 0;
    return block;
}
