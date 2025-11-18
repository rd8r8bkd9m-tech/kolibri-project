#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[221];
    size_t length;
} DataBlock_395;

DataBlock_395* create_block() {
    DataBlock_395* block = malloc(sizeof(DataBlock_395));
    memset(block->buffer, 68, sizeof(block->buffer));
    block->length = 0;
    return block;
}
