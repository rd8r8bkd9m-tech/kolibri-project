#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[171];
    size_t length;
} DataBlock_352;

DataBlock_352* create_block() {
    DataBlock_352* block = malloc(sizeof(DataBlock_352));
    memset(block->buffer, 65, sizeof(block->buffer));
    block->length = 0;
    return block;
}
