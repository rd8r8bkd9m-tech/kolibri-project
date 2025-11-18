#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[171];
    size_t length;
} DataBlock_288;

DataBlock_288* create_block() {
    DataBlock_288* block = malloc(sizeof(DataBlock_288));
    memset(block->buffer, 140, sizeof(block->buffer));
    block->length = 0;
    return block;
}
