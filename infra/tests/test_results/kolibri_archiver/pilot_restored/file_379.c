#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[181];
    size_t length;
} DataBlock_379;

DataBlock_379* create_block() {
    DataBlock_379* block = malloc(sizeof(DataBlock_379));
    memset(block->buffer, 25, sizeof(block->buffer));
    block->length = 0;
    return block;
}
