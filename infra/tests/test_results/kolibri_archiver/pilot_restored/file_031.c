#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[181];
    size_t length;
} DataBlock_31;

DataBlock_31* create_block() {
    DataBlock_31* block = malloc(sizeof(DataBlock_31));
    memset(block->buffer, 126, sizeof(block->buffer));
    block->length = 0;
    return block;
}
