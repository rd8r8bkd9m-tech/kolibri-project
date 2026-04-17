#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[171];
    size_t length;
} DataBlock_180;

DataBlock_180* create_block() {
    DataBlock_180* block = malloc(sizeof(DataBlock_180));
    memset(block->buffer, 94, sizeof(block->buffer));
    block->length = 0;
    return block;
}
