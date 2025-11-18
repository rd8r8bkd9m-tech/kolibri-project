#include <string.h>
#include <stdlib.h>

typedef struct {
    char buffer[91];
    size_t length;
} DataBlock_296;

DataBlock_296* create_block() {
    DataBlock_296* block = malloc(sizeof(DataBlock_296));
    memset(block->buffer, 95, sizeof(block->buffer));
    block->length = 0;
    return block;
}
