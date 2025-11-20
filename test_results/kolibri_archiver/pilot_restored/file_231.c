#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[141];
    size_t length;
} DataBlock_231;

DataBlock_231* create_block() {
    DataBlock_231* block = malloc(sizeof(DataBlock_231));
    memset(block->buffer, 140, sizeof(block->buffer));
    block->length = 0;
    return block;
}
