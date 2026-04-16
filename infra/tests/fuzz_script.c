#include "kolibri/formula.h"
#include "kolibri/genome.h"
#include "kolibri/script.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    KolibriFormulaPool pool;
    kf_pool_init(&pool, 0U);

    KolibriGenome genome;
    memset(&genome, 0, sizeof(genome));

    KolibriScript script;
    if (ks_init(&script, &pool, &genome) != 0) {
        return 0;
    }

    /* Copy fuzzer input into a null-terminated buffer. */
    enum { BUFFER_CAP = 1024 };
    char buffer[BUFFER_CAP];
    size_t copy = size < (BUFFER_CAP - 1U) ? size : (BUFFER_CAP - 1U);
    memcpy(buffer, data, copy);
    buffer[copy] = '\0';

    if (ks_load_text(&script, buffer) == 0) {
        (void)ks_execute(&script);
    }

    ks_free(&script);
    return 0;
}
