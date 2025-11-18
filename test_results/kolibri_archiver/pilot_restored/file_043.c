#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[181];
    size_t length;
} DataBlock_43;

DataBlock_43* create_block() {
    DataBlock_43* block = malloc(sizeof(DataBlock_43));
    memset(block->buffer, 28, sizeof(block->buffer));
    block->length = 0;
    return block;
}
