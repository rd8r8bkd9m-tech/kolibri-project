#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[141];
    size_t length;
} DataBlock_263;

DataBlock_263* create_block() {
    DataBlock_263* block = malloc(sizeof(DataBlock_263));
    memset(block->buffer, 58, sizeof(block->buffer));
    block->length = 0;
    return block;
}
