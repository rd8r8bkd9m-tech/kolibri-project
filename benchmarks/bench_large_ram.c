/* Copyright (c) 2025 Kochurov Vladislav Evgenievich */
#define _POSIX_C_SOURCE 200809L
#include "kolibri/compress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/resource.h>

static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static long get_peak_rss_kb(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}

static uint8_t *gen_text_corpus(size_t target_size) {
    const char *phrases[] = {
        "The quick brown fox jumps over the lazy dog. ",
        "int main(int argc, char **argv) { return 0; }\n",
        "static void process_data(const uint8_t *buf, size_t len) {\n",
        "    for (size_t i = 0; i < len; i++) {\n",
        "        if (buf[i] == 0xFF) break;\n    }\n}\n",
        "{\"name\": \"kolibri\", \"version\": 75, \"type\": \"compressor\"}\n",
        "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\n",
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ",
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. ",
        "typedef struct { int id; char name[256]; double value; } Record;\n",
        "while (pos < end && *pos != '\\0') { hash = hash * 31 + *pos++; }\n",
    };
    int nphrases = sizeof(phrases) / sizeof(phrases[0]);
    uint8_t *data = (uint8_t *)malloc(target_size + 1024);
    if (!data) return NULL;
    size_t pos = 0;
    unsigned seed = 42;
    while (pos < target_size) {
        seed = seed * 1103515245u + 12345u;
        int idx = (int)((seed >> 16) % nphrases);
        size_t plen = strlen(phrases[idx]);
        if (pos + plen > target_size) plen = target_size - pos;
        memcpy(data + pos, phrases[idx], plen);
        pos += plen;
    }
    return data;
}

static uint8_t *gen_binary_corpus(size_t target_size) {
    uint8_t *data = (uint8_t *)malloc(target_size);
    if (!data) return NULL;
    unsigned seed = 12345;
    for (size_t i = 0; i < target_size; i++) {
        seed = seed * 1103515245u + 12345u;
        if ((i & 0x1FF) < 0x100)
            data[i] = (uint8_t)(i & 0xFF);
        else
            data[i] = (uint8_t)((seed >> 16) & 0xFF);
    }
    return data;
}

typedef struct {
    const char *name;
    size_t original;
    size_t compressed;
    double ratio;
    double compress_ms;
    double decompress_ms;
    int roundtrip_ok;
} LargeResult;

static LargeResult bench_one(const char *label, const uint8_t *data,
                              size_t size, int use_turbo)
{
    LargeResult r = {0};
    r.name = label;
    r.original = size;

    uint32_t methods = use_turbo ? KOLIBRI_COMPRESS_TURBO : KOLIBRI_COMPRESS_ALL;
    KolibriCompressor *comp = kolibri_compressor_create(methods);
    if (!comp) { r.ratio = -1; return r; }

    uint8_t *compressed = NULL;
    size_t compressed_size = 0;
    KolibriCompressStats stats = {0};

    double t0 = get_time_ms();
    int ret = kolibri_compress(comp, data, size, &compressed, &compressed_size, &stats);
    double t1 = get_time_ms();
    r.compress_ms = t1 - t0;

    if (ret != 0 || !compressed) {
        kolibri_compressor_destroy(comp);
        r.ratio = -1;
        return r;
    }

    r.compressed = compressed_size;
    r.ratio = (compressed_size > 0) ? (double)size / (double)compressed_size : 0;

    uint8_t *decompressed = NULL;
    size_t decompressed_size = 0;
    t0 = get_time_ms();
    ret = kolibri_decompress(compressed, compressed_size,
                             &decompressed, &decompressed_size, NULL);
    t1 = get_time_ms();
    r.decompress_ms = t1 - t0;

    if (ret == 0 && decompressed && decompressed_size == size)
        r.roundtrip_ok = (memcmp(data, decompressed, size) == 0) ? 1 : 0;

    free(compressed);
    free(decompressed);
    kolibri_compressor_destroy(comp);
    return r;
}

int main(void) {
    printf("\n=== Kolibri v75 Large Corpus + RAM Benchmark ===\n\n");

    /* Part 1: Large corpus performance */
    printf("--- Part 1: Large corpus performance ---\n\n");
    printf("  %-28s  %10s  %10s  %8s  %10s  %10s  %4s\n",
           "Test", "Original", "Compressed", "Ratio", "Comp(ms)", "Dec(ms)", "R/T");
    printf("  %-28s  %10s  %10s  %8s  %10s  %10s  %4s\n",
           "---", "---", "---", "---", "---", "---", "---");

    size_t corpus_sizes[] = { 1*1024*1024, 5*1024*1024 };
    int n_corpus = 2;
    char label[64];

    for (int s = 0; s < n_corpus; s++) {
        size_t sz = corpus_sizes[s];
        uint8_t *text = gen_text_corpus(sz);
        uint8_t *bin = gen_binary_corpus(sz);

        if (text) {
            snprintf(label, sizeof(label), "Text %zuMB (CM)", sz/(1024*1024));
            LargeResult r = bench_one(label, text, sz, 0);
            printf("  %-28s  %8zuKB  %8zuKB  %6.2fx  %8.1fms  %8.1fms  %s\n",
                   r.name, r.original/1024, r.compressed/1024,
                   r.ratio, r.compress_ms, r.decompress_ms,
                   r.roundtrip_ok ? "OK" : "FAIL");

            snprintf(label, sizeof(label), "Text %zuMB (Fast)", sz/(1024*1024));
            r = bench_one(label, text, sz, 1);
            printf("  %-28s  %8zuKB  %8zuKB  %6.2fx  %8.1fms  %8.1fms  %s\n",
                   r.name, r.original/1024, r.compressed/1024,
                   r.ratio, r.compress_ms, r.decompress_ms,
                   r.roundtrip_ok ? "OK" : "FAIL");
            free(text);
        }
        if (bin) {
            snprintf(label, sizeof(label), "Bin %zuMB (CM)", sz/(1024*1024));
            LargeResult r = bench_one(label, bin, sz, 0);
            printf("  %-28s  %8zuKB  %8zuKB  %6.2fx  %8.1fms  %8.1fms  %s\n",
                   r.name, r.original/1024, r.compressed/1024,
                   r.ratio, r.compress_ms, r.decompress_ms,
                   r.roundtrip_ok ? "OK" : "FAIL");

            snprintf(label, sizeof(label), "Bin %zuMB (Fast)", sz/(1024*1024));
            r = bench_one(label, bin, sz, 1);
            printf("  %-28s  %8zuKB  %8zuKB  %6.2fx  %8.1fms  %8.1fms  %s\n",
                   r.name, r.original/1024, r.compressed/1024,
                   r.ratio, r.compress_ms, r.decompress_ms,
                   r.roundtrip_ok ? "OK" : "FAIL");
            free(bin);
        }
        printf("\n");
    }

    /* Part 2: Peak RAM */
    printf("--- Part 2: Peak RAM ---\n");
    printf("  Peak RSS: %ld KB (%.1f MB)\n\n",
           get_peak_rss_kb(), get_peak_rss_kb() / 1024.0);

    /* Part 3: Fast mode throughput scaling */
    printf("--- Part 3: Fast mode throughput scaling ---\n\n");
    printf("  %12s  %10s  %10s  %8s\n", "Size", "Time(ms)", "MB/s", "Ratio");
    printf("  %12s  %10s  %10s  %8s\n", "----", "-------", "----", "-----");

    size_t fast_sizes[] = { 1024, 10*1024, 100*1024, 1024*1024, 5*1024*1024 };
    int nfast = 5;

    for (int i = 0; i < nfast; i++) {
        size_t sz = fast_sizes[i];
        uint8_t *data = gen_text_corpus(sz);
        if (!data) continue;

        /* Warmup */
        bench_one("w", data, sz, 1);

        double best_ms = 1e9;
        double best_ratio = 0;
        for (int iter = 0; iter < 5; iter++) {
            LargeResult r = bench_one("f", data, sz, 1);
            if (r.ratio > 0 && r.compress_ms < best_ms) {
                best_ms = r.compress_ms;
                best_ratio = r.ratio;
            }
        }

        double mb_s = (best_ms > 0.001) ?
            ((double)sz / (1024.0*1024.0)) / (best_ms / 1000.0) : 0;

        if (sz < 1024*1024) {
            printf("  %10zuKB  %8.2fms  %8.1f    %6.2fx\n",
                   sz/1024, best_ms, mb_s, best_ratio);
        } else {
            printf("  %10zuMB  %8.2fms  %8.1f    %6.2fx\n",
                   sz/(1024*1024), best_ms, mb_s, best_ratio);
        }
        free(data);
    }

    printf("\n  Final Peak RSS: %ld KB (%.1f MB)\n\n",
           get_peak_rss_kb(), get_peak_rss_kb() / 1024.0);

    return 0;
}
