#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[181];
    size_t length;
} DataBlock_119;

DataBlock_119* create_block() {
    DataBlock_119* block = malloc(sizeof(DataBlock_119));
    memset(block->buffer, 105, sizeof(block->buffer));
    block->length = 0;
    return block;
}
