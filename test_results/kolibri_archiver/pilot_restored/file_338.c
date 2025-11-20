#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[171];
    size_t length;
} DataBlock_338;

DataBlock_338* create_block() {
    DataBlock_338* block = malloc(sizeof(DataBlock_338));
    memset(block->buffer, 56, sizeof(block->buffer));
    block->length = 0;
    return block;
}
