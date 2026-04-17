#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[211];
    size_t length;
} DataBlock_356;

DataBlock_356* create_block() {
    DataBlock_356* block = malloc(sizeof(DataBlock_356));
    memset(block->buffer, 27, sizeof(block->buffer));
    block->length = 0;
    return block;
}
