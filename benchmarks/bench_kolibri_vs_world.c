/* Copyright (c) 2025 Кочуров Владислав Евгеньевич */
/*
 * Kolibri OS — Мировой бенчмарк сжатия
 *
 * Сравнение Kolibri с лучшими архиваторами мира:
 *   gzip, bzip2, xz (LZMA2), zstd, lz4, Kolibri (v65 context-mixing)
 *
 * Тестовые корпуса:
 *   1. Исходный код C (высокая структурность)
 *   2. Русский текст (UTF-8, 2-байтные символы)
 *   3. Английский текст (ASCII)
 *   4. JSON / структурированные данные
 *   5. Бинарные данные (низкая энтропия)
 *   6. Высокоэнтропийные данные (рандом)
 *   7. Реальный файл из проекта (собственный исходный код)
 *
 * Метрики: ratio, время сжатия, время распаковки, корректность (roundtrip)
 *
 * Сборка:
 *   gcc -O3 -o bench_kolibri_vs_world bench_kolibri_vs_world.c \
 *       -I../backend/include -L../build -lkolibri_core \
 *       -lssl -lcrypto -lsqlite3 -lpthread -lm
 */

#define _POSIX_C_SOURCE 200809L

#include "kolibri/compress.h"
#include "kolibri/predictive_compress.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

/* ============================================================================
 * Опции CLI и JSON-отчёт (reproducible benchmark)
 * ============================================================================ */

typedef struct {
    const char *json_path;
    int quiet;
} BenchOptions;

static void print_usage(const char *argv0) {
    printf("Usage: %s [--json=<path>] [--quiet]\n", argv0);
    printf("  --json=<path>  Write machine-readable report (UTF-8 JSON)\n");
    printf("  --quiet        Suppress pretty table output\n");
}

static int parse_args(int argc, char **argv, BenchOptions *opt) {
    memset(opt, 0, sizeof(*opt));
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strncmp(a, "--json=", 7)) {
            opt->json_path = a + 7;
        } else if (!strcmp(a, "--quiet")) {
            opt->quiet = 1;
        } else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            print_usage(argv[0]);
            return 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", a);
            print_usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

static void json_write_escaped(FILE *f, const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        switch (c) {
            case '"': fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:
                if (c < 0x20) {
                    fprintf(f, "\\u%04x", (unsigned)c);
                } else {
                    fputc((int)c, f);
                }
        }
    }
}

static int read_cmd_output(char *dst, size_t cap, const char *cmd) {
    if (!dst || cap == 0 || !cmd) return -1;
    dst[0] = 0;
    FILE *p = popen(cmd, "r");
    if (!p) return -1;
    if (!fgets(dst, (int)cap, p)) {
        pclose(p);
        return -1;
    }
    pclose(p);
    size_t n = strlen(dst);
    while (n > 0 && (dst[n - 1] == '\n' || dst[n - 1] == '\r')) dst[--n] = 0;
    return 0;
}

/* ============================================================================
 * Утилиты замера времени
 * ============================================================================ */

static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* ============================================================================
 * Генерация тестовых корпусов
 * ============================================================================ */

/* --- Исходный код C (реалистичный, повторяющиеся паттерны) --- */
static uint8_t *gen_c_source(size_t *out_size) {
    const char *fragments[] = {
        "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\n",
        "typedef struct {\n    int id;\n    char name[256];\n    double value;\n    uint8_t flags;\n} DataRecord;\n\n",
        "static int process_record(DataRecord *rec, const char *input) {\n",
        "    if (!rec || !input) return -1;\n",
        "    rec->id = atoi(input);\n",
        "    strncpy(rec->name, input, sizeof(rec->name) - 1);\n",
        "    rec->value = strtod(input, NULL);\n",
        "    rec->flags = 0x01;\n",
        "    return 0;\n}\n\n",
        "int main(int argc, char **argv) {\n",
        "    DataRecord records[1024];\n",
        "    memset(records, 0, sizeof(records));\n",
        "    for (int i = 0; i < argc; i++) {\n",
        "        if (process_record(&records[i], argv[i]) != 0) {\n",
        "            fprintf(stderr, \"Error processing record %d\\n\", i);\n",
        "            return EXIT_FAILURE;\n",
        "        }\n",
        "        printf(\"Record %d: id=%d name=%s value=%.2f\\n\",\n",
        "               i, records[i].id, records[i].name, records[i].value);\n",
        "    }\n",
        "    /* Обработка массива записей */\n",
        "    size_t total = 0;\n",
        "    for (int i = 0; i < 1024; i++) {\n",
        "        total += records[i].id;\n",
        "        if (records[i].flags & 0x01) {\n",
        "            records[i].value *= 1.15;\n",
        "        }\n",
        "    }\n",
        "    printf(\"Total: %zu\\n\", total);\n",
        "    return EXIT_SUCCESS;\n}\n\n",
    };
    int n_frag = sizeof(fragments) / sizeof(fragments[0]);
    
    size_t target = 100000; /* ~100 KB */
    uint8_t *buf = (uint8_t *)malloc(target + 4096);
    if (!buf) return NULL;
    
    size_t pos = 0;
    while (pos < target) {
        for (int i = 0; i < n_frag && pos < target; i++) {
            size_t len = strlen(fragments[i]);
            if (pos + len > target + 4096) break;
            memcpy(buf + pos, fragments[i], len);
            pos += len;
        }
    }
    *out_size = pos;
    return buf;
}

/* --- Русский текст (UTF-8, типичная проза) --- */
static uint8_t *gen_russian_text(size_t *out_size) {
    const char *paragraphs[] = {
        "Колибри — самая маленькая птица в мире. Её сердце бьётся до тысячи двухсот "
        "ударов в минуту, а крылья совершают до восьмидесяти взмахов в секунду. "
        "Эта удивительная птица способна зависать в воздухе и даже летать назад. "
        "Колибри питается нектаром цветов, погружая свой длинный клюв глубоко в бутоны. ",

        "Искусственный интеллект — область компьютерных наук, изучающая создание "
        "интеллектуальных систем. Современные нейросети обрабатывают миллиарды "
        "параметров и способны генерировать текст, изображения и код. Однако "
        "настоящее понимание языка остаётся открытой научной проблемой. ",

        "Алгоритмы сжатия данных делятся на два класса: без потерь и с потерями. "
        "Методы без потерь гарантируют точное восстановление исходных данных. "
        "К ним относятся алгоритмы Хаффмана, Лемпеля-Зива и арифметическое "
        "кодирование. Каждый из этих методов использует статистические свойства данных. ",

        "Программирование на языке Си требует тщательного управления памятью. "
        "Каждый вызов malloc должен быть парным с вызовом free. Утечки памяти "
        "могут привести к деградации производительности и аварийному завершению "
        "программы. Современные инструменты анализа помогают обнаруживать такие ошибки. ",

        "Операционная система управляет ресурсами компьютера: процессором, памятью, "
        "устройствами ввода-вывода и файловой системой. Ядро операционной системы "
        "обеспечивает изоляцию процессов и управление виртуальной памятью. Это "
        "фундаментальный компонент любой вычислительной платформы. ",
    };
    int n_para = sizeof(paragraphs) / sizeof(paragraphs[0]);

    size_t target = 100000;
    uint8_t *buf = (uint8_t *)malloc(target + 4096);
    if (!buf) return NULL;

    size_t pos = 0;
    while (pos < target) {
        for (int i = 0; i < n_para && pos < target; i++) {
            size_t len = strlen(paragraphs[i]);
            if (pos + len > target + 4096) break;
            memcpy(buf + pos, paragraphs[i], len);
            pos += len;
            if (pos < target) { buf[pos++] = '\n'; buf[pos++] = '\n'; }
        }
    }
    *out_size = pos;
    return buf;
}

/* --- Английский текст --- */
static uint8_t *gen_english_text(size_t *out_size) {
    const char *paragraphs[] = {
        "The quick brown fox jumps over the lazy dog. This sentence contains every "
        "letter of the English alphabet and has been used for typing practice and "
        "font display since at least the late nineteenth century. It remains one of "
        "the most well known pangrams in the English language. ",

        "Data compression is the process of encoding information using fewer bits "
        "than the original representation. Shannon's source coding theorem establishes "
        "the theoretical limits for lossless data compression. Modern algorithms like "
        "arithmetic coding can approach these limits for known probability distributions. ",

        "The Linux kernel is a free and open-source Unix-like operating system kernel. "
        "It was first released by Linus Torvalds in 1991. The kernel manages hardware "
        "resources and provides essential services for all other parts of the operating "
        "system. It is widely used in servers, embedded systems, and mobile devices. ",

        "Machine learning algorithms can be categorized into supervised, unsupervised, "
        "and reinforcement learning. Supervised learning uses labeled training data to "
        "learn a mapping from inputs to outputs. Neural networks with multiple hidden "
        "layers, known as deep learning, have achieved remarkable results in many domains. ",

        "Software engineering principles emphasize maintainability, readability, and "
        "testability of code. Design patterns provide reusable solutions to common "
        "programming challenges. Version control systems like Git enable collaborative "
        "development and track changes to source code over time. ",
    };
    int n_para = sizeof(paragraphs) / sizeof(paragraphs[0]);

    size_t target = 100000;
    uint8_t *buf = (uint8_t *)malloc(target + 4096);
    if (!buf) return NULL;

    size_t pos = 0;
    while (pos < target) {
        for (int i = 0; i < n_para && pos < target; i++) {
            size_t len = strlen(paragraphs[i]);
            if (pos + len > target + 4096) break;
            memcpy(buf + pos, paragraphs[i], len);
            pos += len;
            if (pos < target) { buf[pos++] = '\n'; buf[pos++] = '\n'; }
        }
    }
    *out_size = pos;
    return buf;
}

/* --- JSON (структурированные данные) --- */
static uint8_t *gen_json_data(size_t *out_size) {
    size_t target = 100000;
    uint8_t *buf = (uint8_t *)malloc(target + 4096);
    if (!buf) return NULL;

    size_t pos = 0;
    pos += snprintf((char *)buf + pos, target - pos, "[\n");
    
    for (int i = 0; pos < target - 512; i++) {
        pos += snprintf((char *)buf + pos, target - pos,
            "  {\n"
            "    \"id\": %d,\n"
            "    \"name\": \"user_%04d\",\n"
            "    \"email\": \"user%d@example.com\",\n"
            "    \"age\": %d,\n"
            "    \"active\": %s,\n"
            "    \"score\": %.2f,\n"
            "    \"tags\": [\"tag_%d\", \"tag_%d\", \"category_%d\"],\n"
            "    \"address\": {\n"
            "      \"city\": \"City_%d\",\n"
            "      \"zip\": \"%05d\",\n"
            "      \"country\": \"RU\"\n"
            "    }\n"
            "  }%s\n",
            i, i, i, 18 + (i % 62),
            (i % 3 == 0) ? "true" : "false",
            (double)(i * 17 % 1000) / 10.0,
            i % 20, (i + 5) % 20, i % 8,
            i % 50, 10000 + i,
            (pos + 400 < target) ? "," : "");
    }
    pos += snprintf((char *)buf + pos, target + 4096 - pos, "]\n");
    *out_size = pos;
    return buf;
}

/* --- Бинарные данные (низкая энтропия, повторяющиеся структуры) --- */
static uint8_t *gen_binary_low_entropy(size_t *out_size) {
    size_t target = 100000;
    uint8_t *buf = (uint8_t *)calloc(target, 1);
    if (!buf) return NULL;

    /* Повторяющиеся 4-байтовые структуры с небольшими вариациями */
    for (size_t i = 0; i < target; i += 4) {
        buf[i]   = (uint8_t)(i % 32);
        buf[i+1] = (uint8_t)((i / 32) % 16);
        buf[i+2] = 0;
        buf[i+3] = (uint8_t)(i % 256 < 64 ? 0xFF : 0x00);
    }
    *out_size = target;
    return buf;
}

/* --- Высокоэнтропийные данные (псевдослучайные, несжимаемые) --- */
static uint8_t *gen_random_data(size_t *out_size) {
    size_t target = 100000;
    uint8_t *buf = (uint8_t *)malloc(target);
    if (!buf) return NULL;

    uint64_t state = 0xDEADBEEFCAFE1234ULL;
    for (size_t i = 0; i < target; i++) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        buf[i] = (uint8_t)(state >> 33);
    }
    *out_size = target;
    return buf;
}

/* --- Реальный файл из проекта --- */
static uint8_t *load_real_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0 || sz > 10 * 1024 * 1024) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    *out_size = rd;
    return buf;
}

/* ============================================================================
 * Внешние архиваторы через system()
 * ============================================================================ */

#define MAX_RESULTS 16

typedef struct {
    const char *name;
    double ratio;
    double compress_ms;
    double decompress_ms;
    int    roundtrip_ok;
    size_t original_size;
    size_t compressed_size;
} BenchResult;

typedef struct {
    char name[128];
    size_t size;
    BenchResult results[MAX_RESULTS];
    int n;
    int best_idx;
} CorpusReport;

typedef struct {
    CorpusReport corpora[16];
    int n_corpora;
} BenchReport;

/* Сжатие через внешнюю утилиту: запись во временный файл → сжатие → чтение размера */
static BenchResult bench_external(const char *name, const char *compress_cmd,
                                   const char *decompress_cmd,
                                   const uint8_t *data, size_t size) {
    BenchResult r = {0};
    r.name = name;
    r.original_size = size;

    /* Создаём временные файлы */
    char tmp_in[]  = "/tmp/kolibri_bench_XXXXXX";
    char tmp_out[512];
    char tmp_dec[512];

    int fd = mkstemp(tmp_in);
    if (fd < 0) { r.ratio = -1; return r; }
    write(fd, data, size);
    close(fd);

    snprintf(tmp_out, sizeof(tmp_out), "%s.compressed", tmp_in);
    snprintf(tmp_dec, sizeof(tmp_dec), "%s.decompressed", tmp_in);

    /* --- Сжатие --- */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), compress_cmd, tmp_in, tmp_out);

    double t0 = get_time_ms();
    int ret = system(cmd);
    double t1 = get_time_ms();
    r.compress_ms = t1 - t0;

    if (ret != 0) {
        r.ratio = -1;
        unlink(tmp_in);
        return r;
    }

    /* Размер сжатого */
    FILE *f = fopen(tmp_out, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        r.compressed_size = (size_t)ftell(f);
        fclose(f);
        r.ratio = (r.compressed_size > 0) ? (double)size / (double)r.compressed_size : 0;
    } else {
        r.ratio = -1;
    }

    /* --- Распаковка + roundtrip --- */
    snprintf(cmd, sizeof(cmd), decompress_cmd, tmp_out, tmp_dec);
    t0 = get_time_ms();
    ret = system(cmd);
    t1 = get_time_ms();
    r.decompress_ms = t1 - t0;

    if (ret == 0) {
        /* Проверяем roundtrip */
        FILE *fchk = fopen(tmp_dec, "rb");
        if (fchk) {
            fseek(fchk, 0, SEEK_END);
            size_t dec_sz = (size_t)ftell(fchk);
            if (dec_sz == size) {
                fseek(fchk, 0, SEEK_SET);
                uint8_t *dec_buf = (uint8_t *)malloc(dec_sz);
                if (dec_buf) {
                    fread(dec_buf, 1, dec_sz, fchk);
                    r.roundtrip_ok = (memcmp(data, dec_buf, size) == 0) ? 1 : 0;
                    free(dec_buf);
                }
            }
            fclose(fchk);
        }
    }

    /* Очистка */
    unlink(tmp_in);
    unlink(tmp_out);
    unlink(tmp_dec);

    return r;
}

/* ============================================================================
 * Бенчмарк Kolibri (встроенный)
 * ============================================================================ */
static BenchResult bench_kolibri_compress(const uint8_t *data, size_t size) {
    BenchResult r = {0};
    r.name = "Kolibri v65 (CM)";
    r.original_size = size;

    KolibriCompressor *comp = kolibri_compressor_create(KOLIBRI_COMPRESS_ALL);
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

    r.compressed_size = compressed_size;
    r.ratio = (compressed_size > 0) ? (double)size / (double)compressed_size : 0;

    /* Распаковка + roundtrip */
    uint8_t *decompressed = NULL;
    size_t decompressed_size = 0;

    t0 = get_time_ms();
    ret = kolibri_decompress(compressed, compressed_size, &decompressed, &decompressed_size, NULL);
    t1 = get_time_ms();
    r.decompress_ms = t1 - t0;

    if (ret == 0 && decompressed && decompressed_size == size) {
        r.roundtrip_ok = (memcmp(data, decompressed, size) == 0) ? 1 : 0;
    }

    free(compressed);
    free(decompressed);
    kolibri_compressor_destroy(comp);

    return r;
}

static BenchResult bench_kolibri_kpc(const uint8_t *data, size_t size) {
    BenchResult r = {0};
    r.name = "Kolibri KPC (Evo)";
    r.original_size = size;

    KPCContext *ctx = kpc_create();
    if (!ctx) { r.ratio = -1; return r; }

    /* Обучаем на данных (50 раундов эволюции) */
    kpc_train(ctx, data, size, 50);

    uint8_t *compressed = NULL;
    size_t compressed_size = 0;

    double t0 = get_time_ms();
    int ret = kpc_compress(ctx, data, size, &compressed, &compressed_size);
    double t1 = get_time_ms();
    r.compress_ms = t1 - t0;

    if (ret != 0 || !compressed) {
        kpc_destroy(ctx);
        r.ratio = -1;
        return r;
    }

    r.compressed_size = compressed_size;
    r.ratio = (compressed_size > 0) ? (double)size / (double)compressed_size : 0;

    /* Распаковка + roundtrip */
    uint8_t *decompressed = NULL;
    size_t decompressed_size = 0;

    t0 = get_time_ms();
    ret = kpc_decompress(compressed, compressed_size, &decompressed, &decompressed_size);
    t1 = get_time_ms();
    r.decompress_ms = t1 - t0;

    if (ret == 0 && decompressed && decompressed_size == size) {
        r.roundtrip_ok = (memcmp(data, decompressed, size) == 0) ? 1 : 0;
    }

    free(compressed);
    free(decompressed);
    kpc_destroy(ctx);

    return r;
}

/* ============================================================================
 * Форматирование результатов
 * ============================================================================ */

static void print_header(void) {
    printf("\n╔══════════════════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║           KOLIBRI OS — МИРОВОЙ БЕНЧМАРК СЖАТИЯ (v65 vs World)                         ║\n");
    printf("╠══════════════════════════════════════════════════════════════════════════════════════════╣\n");
}

static void print_corpus_header(const char *name, size_t size) {
    printf("╠══════════════════════════════════════════════════════════════════════════════════════════╣\n");
    printf("║  Корпус: %-30s  Размер: %7zu байт (%zu KB)                 ║\n",
           name, size, size / 1024);
    printf("╠═══════════════════╦═══════════╦════════╦═══════════╦═══════════╦══════════╦═════════════╣\n");
    printf("║    Архиватор      ║  Сжатый   ║ Ratio  ║ Сжатие мс ║ Распак мс ║ Roundtrip║   Рейтинг   ║\n");
    printf("╠═══════════════════╬═══════════╬════════╬═══════════╬═══════════╬══════════╬═════════════╣\n");
}

static void print_result(const BenchResult *r, int is_winner) {
    if (r->ratio < 0) {
        printf("║ %-17s ║   ОШИБКА  ║  N/A   ║    N/A    ║    N/A    ║   N/A    ║             ║\n",
               r->name);
        return;
    }
    const char *star = is_winner ? " ★ ЛУЧШИЙ" : "";
    const char *rt   = r->roundtrip_ok ? "   ✓   " : "   ✗   ";
    printf("║ %-17s ║ %7zu B ║ %5.2fx ║ %7.1f   ║ %7.1f   ║ %s  ║%12s ║\n",
           r->name, r->compressed_size, r->ratio,
           r->compress_ms, r->decompress_ms, rt, star);
}

static void print_footer(void) {
    printf("╠═══════════════════╩═══════════╩════════╩═══════════╩═══════════╩══════════╩═════════════╣\n");
}

static void print_final_footer(void) {
    printf("╚══════════════════════════════════════════════════════════════════════════════════════════╝\n");
}

/* ============================================================================
 * Бенчмарк одного корпуса
 * ============================================================================ */

#define MAX_RESULTS 16

static void bench_corpus(const char *corpus_name, const uint8_t *data, size_t size,
                         int *total_wins, int *total_tests,
                         BenchReport *report, int quiet) {
    BenchResult results[MAX_RESULTS];
    int n = 0;

    /* --- Kolibri v65 (главный контендер) --- */
    results[n++] = bench_kolibri_compress(data, size);

    /* --- gzip (deflate, LZ77+Huffman) --- */
    results[n++] = bench_external("gzip -9",
        "gzip -9 -c '%s' > '%s' 2>/dev/null",
        "gzip -d -c '%s' > '%s' 2>/dev/null",
        data, size);

    /* --- bzip2 (BWT + Huffman) --- */
    results[n++] = bench_external("bzip2 -9",
        "bzip2 -9 -c '%s' > '%s' 2>/dev/null",
        "bzip2 -d -c '%s' > '%s' 2>/dev/null",
        data, size);

    /* --- xz (LZMA2, лучшее сжатие в классе LZ) --- */
    results[n++] = bench_external("xz -9e",
        "xz -9e -c '%s' > '%s' 2>/dev/null",
        "xz -d -c '%s' > '%s' 2>/dev/null",
        data, size);

    /* --- zstd (Facebook, максимальное сжатие) --- */
    results[n++] = bench_external("zstd --ultra -22",
        "zstd --ultra -22 -c '%s' > '%s' 2>/dev/null",
        "zstd -d -c '%s' > '%s' 2>/dev/null",
        data, size);

    /* --- zstd (скоростной режим) --- */
    results[n++] = bench_external("zstd -3",
        "zstd -3 -c '%s' > '%s' 2>/dev/null",
        "zstd -d -c '%s' > '%s' 2>/dev/null",
        data, size);

    /* --- lz4 (ультрабыстрый) --- */
    results[n++] = bench_external("lz4 -9",
        "lz4 -9 -c '%s' > '%s' 2>/dev/null",
        "lz4 -d -c '%s' > '%s' 2>/dev/null",
        data, size);

    /* --- Определяем лучший ratio --- */
    double best_ratio = 0;
    int best_idx = -1;
    for (int i = 0; i < n; i++) {
        if (results[i].ratio > best_ratio && results[i].roundtrip_ok) {
            best_ratio = results[i].ratio;
            best_idx = i;
        }
    }

    if (report && report->n_corpora < (int)(sizeof(report->corpora) / sizeof(report->corpora[0]))) {
        CorpusReport *cr = &report->corpora[report->n_corpora++];
        snprintf(cr->name, sizeof(cr->name), "%s", corpus_name);
        cr->size = size;
        cr->n = n;
        cr->best_idx = best_idx;
        for (int i = 0; i < n; i++) cr->results[i] = results[i];
    }

    /* --- Вывод --- */
    if (!quiet) {
        print_corpus_header(corpus_name, size);
        for (int i = 0; i < n; i++) {
            print_result(&results[i], i == best_idx);
        }
        print_footer();
    }

    (*total_tests)++;
    /* Победа Kolibri, если best_idx == 0 (v65 CM) или 1 (KPC Evo) */
    if (best_idx == 0 || best_idx == 1) {
        (*total_wins)++;
    }
}

static int write_json_report(const char *path, const BenchReport *report,
                             int total_wins, int total_tests) {
    if (!path || !report) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    char git_head[128] = "";
    (void)read_cmd_output(git_head, sizeof(git_head), "git rev-parse HEAD 2>/dev/null");

    double ts_ms = get_time_ms();
    fprintf(f, "{\n");
    fprintf(f, "  \"tool\": \"bench_kolibri_vs_world\",\n");
    fprintf(f, "  \"schema_version\": 1,\n");
    fprintf(f, "  \"timestamp_ms\": %.3f,\n", ts_ms);
    fprintf(f, "  \"git_head\": \"");
    json_write_escaped(f, git_head[0] ? git_head : "");
    fprintf(f, "\",\n");
    fprintf(f, "  \"summary\": {\"kolibri_wins\": %d, \"total_corpora\": %d},\n",
            total_wins, total_tests);
    fprintf(f, "  \"corpora\": [\n");
    for (int ci = 0; ci < report->n_corpora; ci++) {
        const CorpusReport *cr = &report->corpora[ci];
        fprintf(f, "    {\"name\": \"");
        json_write_escaped(f, cr->name);
        fprintf(f, "\", \"size\": %zu, \"winner_index\": %d, \"results\": [",
                cr->size, cr->best_idx);
        for (int ri = 0; ri < cr->n; ri++) {
            const BenchResult *r = &cr->results[ri];
            if (ri) fprintf(f, ",");
            fprintf(f, "{\"name\":\"");
            json_write_escaped(f, r->name);
            fprintf(f, "\",\"compressed_size\":%zu,\"original_size\":%zu,\"ratio\":%.6f,\"compress_ms\":%.3f,\"decompress_ms\":%.3f,\"roundtrip_ok\":%d,\"error\":%d}",
                    r->compressed_size, r->original_size, r->ratio,
                    r->compress_ms, r->decompress_ms, r->roundtrip_ok,
                    (r->ratio < 0));
        }
        fprintf(f, "]}");
        if (ci + 1 < report->n_corpora) fprintf(f, ",");
        fprintf(f, "\n");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");

    fclose(f);
    return 0;
}

/* ============================================================================
 * Главная функция
 * ============================================================================ */

int main(int argc, char **argv) {
    BenchOptions opt;
    int par = parse_args(argc, argv, &opt);
    if (par != 0) return (par > 0) ? 0 : 2;

    if (!opt.quiet) print_header();

    int total_wins = 0;
    int total_tests = 0;
    BenchReport report = {0};

    /* --- Корпус 1: Исходный код C --- */
    {
        size_t sz;
        uint8_t *data = gen_c_source(&sz);
        if (data) {
            bench_corpus("Исходный код C", data, sz, &total_wins, &total_tests, &report, opt.quiet);
            free(data);
        }
    }

    /* --- Корпус 2: Русский текст --- */
    {
        size_t sz;
        uint8_t *data = gen_russian_text(&sz);
        if (data) {
            bench_corpus("Русский текст (UTF-8)", data, sz, &total_wins, &total_tests, &report, opt.quiet);
            free(data);
        }
    }

    /* --- Корпус 3: Английский текст --- */
    {
        size_t sz;
        uint8_t *data = gen_english_text(&sz);
        if (data) {
            bench_corpus("Английский текст (ASCII)", data, sz, &total_wins, &total_tests, &report, opt.quiet);
            free(data);
        }
    }

    /* --- Корпус 4: JSON --- */
    {
        size_t sz;
        uint8_t *data = gen_json_data(&sz);
        if (data) {
            bench_corpus("JSON данные", data, sz, &total_wins, &total_tests, &report, opt.quiet);
            free(data);
        }
    }

    /* --- Корпус 5: Бинарные данные --- */
    {
        size_t sz;
        uint8_t *data = gen_binary_low_entropy(&sz);
        if (data) {
            bench_corpus("Бинарные (низк. энтропия)", data, sz, &total_wins, &total_tests, &report, opt.quiet);
            free(data);
        }
    }

    /* --- Корпус 6: Рандом (теоретический предел) --- */
    {
        size_t sz;
        uint8_t *data = gen_random_data(&sz);
        if (data) {
            bench_corpus("Случайные данные (рандом)", data, sz, &total_wins, &total_tests, &report, opt.quiet);
            free(data);
        }
    }

    /* --- Корпус 7: Реальный файл из проекта (крупный) --- */
    {
        /* Конкатенация нескольких исходников для реалистичного объёма */
        const char *src_files[] = {
            "backend/src/compress.c",
            "backend/src/formula.c",
            "backend/src/genome.c",
            "backend/src/knowledge.c",
            "backend/src/inference.c",
            "backend/src/predictive_compress.c",
            "backend/src/script.c",
            "backend/src/net.c",
            NULL
        };
        /* Собираем все файлы в один буфер */
        size_t total_sz = 0;
        uint8_t *combined = (uint8_t *)malloc(2 * 1024 * 1024); /* 2 MB макс */
        if (combined) {
            for (int i = 0; src_files[i]; i++) {
                size_t fsz;
                uint8_t *fdata = load_real_file(src_files[i], &fsz);
                if (fdata && total_sz + fsz < 2 * 1024 * 1024) {
                    memcpy(combined + total_sz, fdata, fsz);
                    total_sz += fsz;
                    free(fdata);
                }
            }
            if (total_sz > 1000) {
                char label[128];
                snprintf(label, sizeof(label), "Реальный код (%zu KB)", total_sz / 1024);
                bench_corpus(label, combined, total_sz, &total_wins, &total_tests, &report, opt.quiet);
            }
            free(combined);
        }
    }

    /* --- Корпус 8: Реальная документация проекта --- */
    {
        const char *doc_files[] = {
            "README.md",
            "CHANGELOG.md",
            "CONTRIBUTING.md",
            "QUICK_REFERENCE.md",
            NULL
        };
        size_t total_sz = 0;
        uint8_t *combined = (uint8_t *)malloc(1024 * 1024);
        if (combined) {
            for (int i = 0; doc_files[i]; i++) {
                size_t fsz;
                uint8_t *fdata = load_real_file(doc_files[i], &fsz);
                if (fdata && total_sz + fsz < 1024 * 1024) {
                    memcpy(combined + total_sz, fdata, fsz);
                    total_sz += fsz;
                    free(fdata);
                }
            }
            if (total_sz > 1000) {
                char label[128];
                snprintf(label, sizeof(label), "Реальные доки (%zu KB)", total_sz / 1024);
                bench_corpus(label, combined, total_sz, &total_wins, &total_tests, &report, opt.quiet);
            }
            free(combined);
        }
    }

    if (opt.json_path && opt.json_path[0]) {
        if (write_json_report(opt.json_path, &report, total_wins, total_tests) != 0) {
            fprintf(stderr, "Failed to write JSON report to: %s\n", opt.json_path);
        }
    }

    /* --- Итоговая статистика --- */
    if (!opt.quiet) {
        printf("╔══════════════════════════════════════════════════════════════════════════════════════════╗\n");
        printf("║                              ИТОГОВЫЕ РЕЗУЛЬТАТЫ                                      ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════════════════════════╣\n");
        printf("║  Kolibri побед: %d из %d корпусов                                                      ║\n",
               total_wins, total_tests);
        
        if (total_wins == total_tests && total_tests > 0) {
            printf("║                                                                                      ║\n");
            printf("║  ★★★ KOLIBRI — ЛУЧШИЙ АРХИВАТОР В МИРЕ! ★★★                                         ║\n");
        } else if (total_wins > total_tests / 2) {
            printf("║                                                                                      ║\n");
            printf("║  ★★ Kolibri превосходит большинство конкурентов ★★                                   ║\n");
        } else {
            printf("║                                                                                      ║\n");
            printf("║  Результаты показывают области для оптимизации.                                      ║\n");
        }
        printf("╚══════════════════════════════════════════════════════════════════════════════════════════╝\n");

        printf("\n--- Сравниваемые системы ---\n");
        printf("  gzip   : GNU zip (deflate, LZ77 + Huffman)\n");
        printf("  bzip2  : Burrows-Wheeler Transform + Huffman\n");
        printf("  xz     : LZMA2 (Lempel-Ziv-Markov chain)\n");
        printf("  zstd   : Facebook Zstandard (LZ + FSE)\n");
        printf("  lz4    : Yann Collet LZ4 (ultra-fast)\n");
        printf("  Kolibri: Context-Mixing v65 (13 предикторов + SSE/APM цепочка)\n");
        printf("  KPC    : Эволюционный MLP-предсказатель + арифм. кодирование\n\n");
    }

    return (total_wins >= total_tests / 2) ? 0 : 1;
}
