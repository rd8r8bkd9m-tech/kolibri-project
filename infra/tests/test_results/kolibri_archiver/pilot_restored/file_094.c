#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[211];
    size_t length;
} DataBlock_94;

DataBlock_94* create_block() {
    DataBlock_94* block = malloc(sizeof(DataBlock_94));
    memset(block->buffer, 143, sizeof(block->buffer));
    block->length = 0;
    return block;
}
