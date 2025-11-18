#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[91];
    size_t length;
} DataBlock_376;

DataBlock_376* create_block() {
    DataBlock_376* block = malloc(sizeof(DataBlock_376));
    memset(block->buffer, 82, sizeof(block->buffer));
    block->length = 0;
    return block;
}
