#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[161];
    size_t length;
} DataBlock_77;

DataBlock_77* create_block() {
    DataBlock_77* block = malloc(sizeof(DataBlock_77));
    memset(block->buffer, 22, sizeof(block->buffer));
    block->length = 0;
    return block;
}
