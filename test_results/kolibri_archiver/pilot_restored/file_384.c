#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[211];
    size_t length;
} DataBlock_384;

DataBlock_384* create_block() {
    DataBlock_384* block = malloc(sizeof(DataBlock_384));
    memset(block->buffer, 62, sizeof(block->buffer));
    block->length = 0;
    return block;
}
