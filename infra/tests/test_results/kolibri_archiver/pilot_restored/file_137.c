#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[81];
    size_t length;
} DataBlock_137;

DataBlock_137* create_block() {
    DataBlock_137* block = malloc(sizeof(DataBlock_137));
    memset(block->buffer, 24, sizeof(block->buffer));
    block->length = 0;
    return block;
}
