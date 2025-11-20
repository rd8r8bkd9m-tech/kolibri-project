#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[81];
    size_t length;
} DataBlock_33;

DataBlock_33* create_block() {
    DataBlock_33* block = malloc(sizeof(DataBlock_33));
    memset(block->buffer, 34, sizeof(block->buffer));
    block->length = 0;
    return block;
}
