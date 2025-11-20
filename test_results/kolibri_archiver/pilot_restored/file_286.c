#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[91];
    size_t length;
} DataBlock_286;

DataBlock_286* create_block() {
    DataBlock_286* block = malloc(sizeof(DataBlock_286));
    memset(block->buffer, 73, sizeof(block->buffer));
    block->length = 0;
    return block;
}
