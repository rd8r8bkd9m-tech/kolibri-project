#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[211];
    size_t length;
} DataBlock_56;

DataBlock_56* create_block() {
    DataBlock_56* block = malloc(sizeof(DataBlock_56));
    memset(block->buffer, 115, sizeof(block->buffer));
    block->length = 0;
    return block;
}
