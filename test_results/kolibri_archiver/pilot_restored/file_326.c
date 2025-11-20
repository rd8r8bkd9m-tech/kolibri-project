#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[91];
    size_t length;
} DataBlock_326;

DataBlock_326* create_block() {
    DataBlock_326* block = malloc(sizeof(DataBlock_326));
    memset(block->buffer, 12, sizeof(block->buffer));
    block->length = 0;
    return block;
}
