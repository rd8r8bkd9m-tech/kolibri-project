#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[211];
    size_t length;
} DataBlock_144;

DataBlock_144* create_block() {
    DataBlock_144* block = malloc(sizeof(DataBlock_144));
    memset(block->buffer, 93, sizeof(block->buffer));
    block->length = 0;
    return block;
}
