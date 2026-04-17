#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[211];
    size_t length;
} DataBlock_316;

DataBlock_316* create_block() {
    DataBlock_316* block = malloc(sizeof(DataBlock_316));
    memset(block->buffer, 55, sizeof(block->buffer));
    block->length = 0;
    return block;
}
