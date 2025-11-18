#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[181];
    size_t length;
} DataBlock_383;

DataBlock_383* create_block() {
    DataBlock_383* block = malloc(sizeof(DataBlock_383));
    memset(block->buffer, 26, sizeof(block->buffer));
    block->length = 0;
    return block;
}
