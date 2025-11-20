#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[91];
    size_t length;
} DataBlock_2;

DataBlock_2* create_block() {
    DataBlock_2* block = malloc(sizeof(DataBlock_2));
    memset(block->buffer, 123, sizeof(block->buffer));
    block->length = 0;
    return block;
}
