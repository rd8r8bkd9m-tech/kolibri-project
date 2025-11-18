#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[181];
    size_t length;
} DataBlock_223;

DataBlock_223* create_block() {
    DataBlock_223* block = malloc(sizeof(DataBlock_223));
    memset(block->buffer, 89, sizeof(block->buffer));
    block->length = 0;
    return block;
}
