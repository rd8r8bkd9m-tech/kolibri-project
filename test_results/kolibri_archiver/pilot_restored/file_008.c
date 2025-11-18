#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[211];
    size_t length;
} DataBlock_8;

DataBlock_8* create_block() {
    DataBlock_8* block = malloc(sizeof(DataBlock_8));
    memset(block->buffer, 159, sizeof(block->buffer));
    block->length = 0;
    return block;
}
