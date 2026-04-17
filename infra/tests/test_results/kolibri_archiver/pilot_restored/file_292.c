#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[171];
    size_t length;
} DataBlock_292;

DataBlock_292* create_block() {
    DataBlock_292* block = malloc(sizeof(DataBlock_292));
    memset(block->buffer, 75, sizeof(block->buffer));
    block->length = 0;
    return block;
}
