#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[81];
    size_t length;
} DataBlock_45;

DataBlock_45* create_block() {
    DataBlock_45* block = malloc(sizeof(DataBlock_45));
    memset(block->buffer, 86, sizeof(block->buffer));
    block->length = 0;
    return block;
}
