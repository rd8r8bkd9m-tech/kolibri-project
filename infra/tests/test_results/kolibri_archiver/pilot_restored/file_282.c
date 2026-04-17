#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[51];
    size_t length;
} DataBlock_282;

DataBlock_282* create_block() {
    DataBlock_282* block = malloc(sizeof(DataBlock_282));
    memset(block->buffer, 97, sizeof(block->buffer));
    block->length = 0;
    return block;
}
