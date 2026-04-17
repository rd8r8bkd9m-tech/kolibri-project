#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[91];
    size_t length;
} DataBlock_264;

DataBlock_264* create_block() {
    DataBlock_264* block = malloc(sizeof(DataBlock_264));
    memset(block->buffer, 139, sizeof(block->buffer));
    block->length = 0;
    return block;
}
