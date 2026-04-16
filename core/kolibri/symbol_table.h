#ifndef KOLIBRI_SYMBOL_TABLE_H
#define KOLIBRI_SYMBOL_TABLE_H

#include "kolibri/genome.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KOLIBRI_SYMBOL_MAX 256
#define KOLIBRI_SYMBOL_DIGITS 3

typedef struct {
    uint32_t codepoint;
    uint8_t digits[KOLIBRI_SYMBOL_DIGITS];
} KolibriSymbolEntry;

typedef struct {
    KolibriSymbolEntry entries[KOLIBRI_SYMBOL_MAX];
    size_t count;
    uint64_t version;
    KolibriGenome *genome;
} KolibriSymbolTable;

void kolibri_symbol_table_init(KolibriSymbolTable *table, KolibriGenome *genome);
void kolibri_symbol_table_load(KolibriSymbolTable *table);
void kolibri_symbol_table_seed_defaults(KolibriSymbolTable *table);
int kolibri_symbol_encode(KolibriSymbolTable *table, uint32_t codepoint, uint8_t out_digits[KOLIBRI_SYMBOL_DIGITS]);
int kolibri_symbol_decode(const KolibriSymbolTable *table,
                          const uint8_t digits[KOLIBRI_SYMBOL_DIGITS],
                          uint32_t *out_codepoint);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_SYMBOL_TABLE_H */
