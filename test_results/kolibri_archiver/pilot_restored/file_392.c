#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[91];
    size_t length;
} DataBlock_392;

DataBlock_392* create_block() {
    DataBlock_392* block = malloc(sizeof(DataBlock_392));
    memset(block->buffer, 114, sizeof(block->buffer));
    block->length = 0;
    return block;
}
