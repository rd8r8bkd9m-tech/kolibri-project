#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[91];
    size_t length;
} DataBlock_360;

DataBlock_360* create_block() {
    DataBlock_360* block = malloc(sizeof(DataBlock_360));
    memset(block->buffer, 132, sizeof(block->buffer));
    block->length = 0;
    return block;
}
