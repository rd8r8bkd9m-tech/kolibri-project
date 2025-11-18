#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[241];
    size_t length;
} DataBlock_365;

DataBlock_365* create_block() {
    DataBlock_365* block = malloc(sizeof(DataBlock_365));
    memset(block->buffer, 59, sizeof(block->buffer));
    block->length = 0;
    return block;
}
