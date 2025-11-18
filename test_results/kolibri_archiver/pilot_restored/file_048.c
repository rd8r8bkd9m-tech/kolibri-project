#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[211];
    size_t length;
} DataBlock_48;

DataBlock_48* create_block() {
    DataBlock_48* block = malloc(sizeof(DataBlock_48));
    memset(block->buffer, 49, sizeof(block->buffer));
    block->length = 0;
    return block;
}
