#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[91];
    size_t length;
} DataBlock_374;

DataBlock_374* create_block() {
    DataBlock_374* block = malloc(sizeof(DataBlock_374));
    memset(block->buffer, 21, sizeof(block->buffer));
    block->length = 0;
    return block;
}
