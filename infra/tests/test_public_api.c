#include "kolibri/formula.h"
#include "kolibri/genome.h"
#include "kolibri/script.h"

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void test_script_smoke(void) {
    KolibriFormulaPool *pool = malloc(sizeof(KolibriFormulaPool));
    assert(pool);
    kf_pool_init(pool, 1234U);

    KolibriGenome genome;
    memset(&genome, 0, sizeof(genome));

    KolibriScript script;
    int rc = ks_init(&script, pool, &genome);
    assert(rc == 0);

    FILE *capture = tmpfile();
    assert(capture);
    ks_set_output(&script, capture);

    rc = ks_load_text(&script, "начало:\n    показать \"API\"\nконец.\n");
    assert(rc == 0);

    rc = ks_execute(&script);
    assert(rc == 0);

    fclose(capture);
    ks_free(&script);
    free(pool);
}

static void test_genome_smoke(void) {
    unsigned char key[KOLIBRI_HMAC_KEY_SIZE];
    memset(key, 1, sizeof(key));

    char path[L_tmpnam];
    assert(tmpnam(path));

    KolibriGenome genome;
    int rc = kg_open(&genome, path, key, sizeof(key));
    assert(rc == 0);
    kg_close(&genome);
    remove(path);
}

static void test_symbol_table_cyrillic(void) {
    KolibriSymbolTable table;
    kolibri_symbol_table_init(&table, NULL);
    kolibri_symbol_table_seed_defaults(&table);

    size_t seeded = table.count;
    kolibri_symbol_table_seed_defaults(&table);
    assert(table.count == seeded);

    uint8_t digits[KOLIBRI_SYMBOL_DIGITS];
    uint32_t decoded = 0U;

    assert(kolibri_symbol_encode(&table, 0x043FU, digits) == 0); /* п */
    assert(kolibri_symbol_decode(&table, digits, &decoded) == 0);
    assert(decoded == 0x043FU);

    assert(kolibri_symbol_encode(&table, 0x0451U, digits) == 0); /* ё */
    assert(kolibri_symbol_decode(&table, digits, &decoded) == 0);
    assert(decoded == 0x0451U);

    assert(kolibri_symbol_encode(&table, 0x0020U, digits) == 0); /* пробел */
    assert(kolibri_symbol_encode(&table, 0x041FU, digits) == 0); /* П */

    size_t before = table.count;
    assert(kolibri_symbol_encode(&table, 0x2728U, digits) == 0); /* новая точка */
    assert(table.count == before + 1U);
}

void test_public_api(void) {
    test_script_smoke();
    test_genome_smoke();
    test_symbol_table_cyrillic();
}
