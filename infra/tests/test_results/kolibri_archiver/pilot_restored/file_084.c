#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[131];
    size_t length;
} DataBlock_84;

DataBlock_84* create_block() {
    DataBlock_84* block = malloc(sizeof(DataBlock_84));
    memset(block->buffer, 85, sizeof(block->buffer));
    block->length = 0;
    return block;
}
