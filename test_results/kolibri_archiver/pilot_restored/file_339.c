#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[181];
    size_t length;
} DataBlock_339;

DataBlock_339* create_block() {
    DataBlock_339* block = malloc(sizeof(DataBlock_339));
    memset(block->buffer, 65, sizeof(block->buffer));
    block->length = 0;
    return block;
}
