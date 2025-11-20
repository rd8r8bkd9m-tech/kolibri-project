#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[51];
    size_t length;
} DataBlock_388;

DataBlock_388* create_block() {
    DataBlock_388* block = malloc(sizeof(DataBlock_388));
    memset(block->buffer, 99, sizeof(block->buffer));
    block->length = 0;
    return block;
}
