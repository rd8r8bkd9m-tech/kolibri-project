#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[181];
    size_t length;
} DataBlock_27;

DataBlock_27* create_block() {
    DataBlock_27* block = malloc(sizeof(DataBlock_27));
    memset(block->buffer, 144, sizeof(block->buffer));
    block->length = 0;
    return block;
}
