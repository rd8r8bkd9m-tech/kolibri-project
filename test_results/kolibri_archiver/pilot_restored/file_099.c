#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[181];
    size_t length;
} DataBlock_99;

DataBlock_99* create_block() {
    DataBlock_99* block = malloc(sizeof(DataBlock_99));
    memset(block->buffer, 44, sizeof(block->buffer));
    block->length = 0;
    return block;
}
