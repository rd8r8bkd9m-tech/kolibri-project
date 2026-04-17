#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[91];
    size_t length;
} DataBlock_112;

DataBlock_112* create_block() {
    DataBlock_112* block = malloc(sizeof(DataBlock_112));
    memset(block->buffer, 129, sizeof(block->buffer));
    block->length = 0;
    return block;
}
