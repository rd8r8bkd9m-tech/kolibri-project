#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[181];
    size_t length;
} DataBlock_55;

DataBlock_55* create_block() {
    DataBlock_55* block = malloc(sizeof(DataBlock_55));
    memset(block->buffer, 105, sizeof(block->buffer));
    block->length = 0;
    return block;
}
