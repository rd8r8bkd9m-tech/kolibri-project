#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[141];
    size_t length;
} DataBlock_279;

DataBlock_279* create_block() {
    DataBlock_279* block = malloc(sizeof(DataBlock_279));
    memset(block->buffer, 33, sizeof(block->buffer));
    block->length = 0;
    return block;
}
