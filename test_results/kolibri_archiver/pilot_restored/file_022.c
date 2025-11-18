#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[211];
    size_t length;
} DataBlock_22;

DataBlock_22* create_block() {
    DataBlock_22* block = malloc(sizeof(DataBlock_22));
    memset(block->buffer, 154, sizeof(block->buffer));
    block->length = 0;
    return block;
}
