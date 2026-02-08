/*
 * Kolibri Bulk Teach: generate many TEACH events quickly.
 *
 * Используется для нагрузочного теста/демо: "100000 сайтов" без сетевого IO.
 * Мы генерируем короткие payload-строки, чтобы помещались в KOLIBRI_PAYLOAD_SIZE
 * после кодирования в цифры.
 */

#include "kolibri/genome.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s [--out PATH] [--count N] [--start N] [--key-inline KEY] [--event EVENT]\n"
            "\n"
            "Defaults:\n"
            "  --out .kolibri/knowledge_genome_100k.dat\n"
            "  --count 100000\n"
            "  --start 0\n"
            "  --key-inline kolibri-secret-key\n"
            "  --event TEACH\n",
            argv0);
}

static int parse_u64(const char *text, unsigned long long *out) {
    if (!text || !out) {
        return -1;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || !end || *end != '\0') {
        return -1;
    }
    *out = value;
    return 0;
}

int main(int argc, char **argv) {
    const char *out_path = ".kolibri/knowledge_genome_100k.dat";
    unsigned long long count = 100000ULL;
    unsigned long long start = 0ULL;
    const char *key_inline = "kolibri-secret-key";
    const char *event = "TEACH";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            if (parse_u64(argv[++i], &count) != 0) {
                fprintf(stderr, "invalid --count\n");
                return 2;
            }
            continue;
        }
        if (strcmp(argv[i], "--start") == 0 && i + 1 < argc) {
            if (parse_u64(argv[++i], &start) != 0) {
                fprintf(stderr, "invalid --start\n");
                return 2;
            }
            continue;
        }
        if (strcmp(argv[i], "--key-inline") == 0 && i + 1 < argc) {
            key_inline = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--event") == 0 && i + 1 < argc) {
            event = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        fprintf(stderr, "unknown arg: %s\n", argv[i]);
        print_usage(argv[0]);
        return 2;
    }

    unsigned char key[KOLIBRI_HMAC_KEY_SIZE];
    size_t key_len = 0U;
    if (!key_inline || key_inline[0] == '\0') {
        fprintf(stderr, "empty key\n");
        return 2;
    }
    key_len = strlen(key_inline);
    if (key_len > sizeof(key)) {
        key_len = sizeof(key);
    }
    memcpy(key, key_inline, key_len);

    KolibriGenome g;
    if (kg_open(&g, out_path, key, key_len) != 0) {
        fprintf(stderr, "failed to open %s\n", out_path);
        return 1;
    }

    /*
     * Важно: payload должен быть достаточно коротким (<= ~85 байт), иначе
     * kg_encode_payload() не сможет закодировать строку в 256 цифр.
     */
    char payload_utf8[128];
    char payload_digits[KOLIBRI_PAYLOAD_SIZE];

    unsigned long long ok = 0ULL;
    for (unsigned long long i = 0; i < count; ++i) {
        unsigned long long n = start + i;
        /* Синтетический "сайт": короткий URL + короткий ответ */
        snprintf(payload_utf8, sizeof(payload_utf8),
                 "q=https://s%06llu/ a=doc%06llu",
                 n, n);

        if (kg_encode_payload(payload_utf8, payload_digits, sizeof(payload_digits)) != 0) {
            fprintf(stderr, "encode failed at %llu\n", n);
            kg_close(&g);
            return 1;
        }
        if (kg_append(&g, event, payload_digits, NULL) != 0) {
            fprintf(stderr, "append failed at %llu\n", n);
            kg_close(&g);
            return 1;
        }
        ok += 1ULL;
    }

    kg_close(&g);
    printf("[bulk_teach] wrote %llu events to %s\n", ok, out_path);
    return 0;
}
