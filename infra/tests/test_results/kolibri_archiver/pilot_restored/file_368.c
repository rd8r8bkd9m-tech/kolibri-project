#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[51];
    size_t length;
} DataBlock_368;

DataBlock_368* create_block() {
    DataBlock_368* block = malloc(sizeof(DataBlock_368));
    memset(block->buffer, 89, sizeof(block->buffer));
    block->length = 0;
    return block;
}
