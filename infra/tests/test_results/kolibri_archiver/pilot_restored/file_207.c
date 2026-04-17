#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[101];
    size_t length;
} DataBlock_207;

DataBlock_207* create_block() {
    DataBlock_207* block = malloc(sizeof(DataBlock_207));
    memset(block->buffer, 15, sizeof(block->buffer));
    block->length = 0;
    return block;
}
