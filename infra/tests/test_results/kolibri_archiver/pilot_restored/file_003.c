#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[61];
    size_t length;
} DataBlock_3;

DataBlock_3* create_block() {
    DataBlock_3* block = malloc(sizeof(DataBlock_3));
    memset(block->buffer, 49, sizeof(block->buffer));
    block->length = 0;
    return block;
}
