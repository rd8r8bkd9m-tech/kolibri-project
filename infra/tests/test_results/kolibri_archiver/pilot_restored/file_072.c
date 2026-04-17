#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[131];
    size_t length;
} DataBlock_72;

DataBlock_72* create_block() {
    DataBlock_72* block = malloc(sizeof(DataBlock_72));
    memset(block->buffer, 79, sizeof(block->buffer));
    block->length = 0;
    return block;
}
