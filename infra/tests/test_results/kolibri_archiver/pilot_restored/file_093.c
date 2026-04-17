#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[201];
    size_t length;
} DataBlock_93;

DataBlock_93* create_block() {
    DataBlock_93* block = malloc(sizeof(DataBlock_93));
    memset(block->buffer, 16, sizeof(block->buffer));
    block->length = 0;
    return block;
}
