#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[121];
    size_t length;
} DataBlock_9;

DataBlock_9* create_block() {
    DataBlock_9* block = malloc(sizeof(DataBlock_9));
    memset(block->buffer, 139, sizeof(block->buffer));
    block->length = 0;
    return block;
}
