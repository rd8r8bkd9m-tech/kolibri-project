/* Kolibri v75 — Fast/Turbo CLI для бенчмарков
 * Использование: kolibri_fast c|d input output
 */
#include "kolibri/compress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s c|d input output\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[2], "rb");
    if (!f) { perror(argv[2]); return 1; }
    fseek(f, 0, SEEK_END);
    size_t sz = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) { fclose(f); return 1; }
    fread(buf, 1, sz, f);
    fclose(f);

    uint8_t *out = NULL;
    size_t out_sz = 0;
    int ret;

    KolibriCompressStats st;
    if (argv[1][0] == 'c') {
        KolibriCompressor *c = kolibri_compressor_create(KOLIBRI_COMPRESS_TURBO);
        if (!c) { free(buf); return 1; }
        ret = kolibri_compress(c, buf, sz, &out, &out_sz, &st);
        kolibri_compressor_destroy(c);
    } else {
        ret = kolibri_decompress(buf, sz, &out, &out_sz, &st);
    }
    free(buf);
    if (ret != 0 || !out) { fprintf(stderr, "Error: operation failed\n"); return 1; }

    f = fopen(argv[3], "wb");
    if (!f) { free(out); return 1; }
    fwrite(out, 1, out_sz, f);
    fclose(f);
    free(out);
    return 0;
}
