#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[91];
    size_t length;
} DataBlock_98;

DataBlock_98* create_block() {
    DataBlock_98* block = malloc(sizeof(DataBlock_98));
    memset(block->buffer, 104, sizeof(block->buffer));
    block->length = 0;
    return block;
}
