#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[211];
    size_t length;
} DataBlock_366;

DataBlock_366* create_block() {
    DataBlock_366* block = malloc(sizeof(DataBlock_366));
    memset(block->buffer, 112, sizeof(block->buffer));
    block->length = 0;
    return block;
}
