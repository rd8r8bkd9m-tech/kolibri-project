#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[131];
    size_t length;
} DataBlock_394;

DataBlock_394* create_block() {
    DataBlock_394* block = malloc(sizeof(DataBlock_394));
    memset(block->buffer, 77, sizeof(block->buffer));
    block->length = 0;
    return block;
}
