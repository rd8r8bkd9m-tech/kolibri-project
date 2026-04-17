#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[131];
    size_t length;
} DataBlock_100;

DataBlock_100* create_block() {
    DataBlock_100* block = malloc(sizeof(DataBlock_100));
    memset(block->buffer, 97, sizeof(block->buffer));
    block->length = 0;
    return block;
}
