#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[51];
    size_t length;
} DataBlock_372;

DataBlock_372* create_block() {
    DataBlock_372* block = malloc(sizeof(DataBlock_372));
    memset(block->buffer, 24, sizeof(block->buffer));
    block->length = 0;
    return block;
}
