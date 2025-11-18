#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[101];
    size_t length;
} DataBlock_343;

DataBlock_343* create_block() {
    DataBlock_343* block = malloc(sizeof(DataBlock_343));
    memset(block->buffer, 81, sizeof(block->buffer));
    block->length = 0;
    return block;
}
