#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[201];
    size_t length;
} DataBlock_265;

DataBlock_265* create_block() {
    DataBlock_265* block = malloc(sizeof(DataBlock_265));
    memset(block->buffer, 119, sizeof(block->buffer));
    block->length = 0;
    return block;
}
