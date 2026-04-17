#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[241];
    size_t length;
} DataBlock_353;

DataBlock_353* create_block() {
    DataBlock_353* block = malloc(sizeof(DataBlock_353));
    memset(block->buffer, 131, sizeof(block->buffer));
    block->length = 0;
    return block;
}
