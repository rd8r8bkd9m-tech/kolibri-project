#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[81];
    size_t length;
} DataBlock_89;

DataBlock_89* create_block() {
    DataBlock_89* block = malloc(sizeof(DataBlock_89));
    memset(block->buffer, 140, sizeof(block->buffer));
    block->length = 0;
    return block;
}
