#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[121];
    size_t length;
} DataBlock_301;

DataBlock_301* create_block() {
    DataBlock_301* block = malloc(sizeof(DataBlock_301));
    memset(block->buffer, 26, sizeof(block->buffer));
    block->length = 0;
    return block;
}
