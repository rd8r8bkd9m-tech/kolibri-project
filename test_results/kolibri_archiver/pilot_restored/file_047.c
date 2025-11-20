#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[61];
    size_t length;
} DataBlock_47;

DataBlock_47* create_block() {
    DataBlock_47* block = malloc(sizeof(DataBlock_47));
    memset(block->buffer, 113, sizeof(block->buffer));
    block->length = 0;
    return block;
}
