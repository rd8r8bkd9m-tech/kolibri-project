#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kolibri/digital_core.h"
#include "kolibri/inference.h"
#include "logical_memory.h"
#include "knowledge_data.h"

#define KOLIBRI_CLI_VERSION "0.2.0"
#define QUERY_LINE_CAP 8192

#define COLOR_RESET "\x1b[0m"
#define COLOR_CYAN  "\x1b[36m"
#define COLOR_GREEN "\x1b[32m"
#define COLOR_GRAY  "\x1b[90m"
#define COLOR_BLUE  "\x1b[34m"

typedef struct {
    bool json;
    bool quiet;
    bool no_color;
    bool interactive;
    bool stats;
    bool no_stats;
    bool list_knowledge;
    const char *ask;
    int positional_start;
} CliOptions;

static const char *paint(bool enabled, const char *code) {
    return enabled ? code : "";
}

static bool stdout_supports_color(const CliOptions *opts) {
    return !opts->no_color && !opts->json && isatty(STDOUT_FILENO);
}

static size_t embedded_knowledge_count(void) {
    size_t count = 0;
    while (KNOWLEDGE_BASE[count].p != NULL) {
        count++;
    }
    return count;
}

static void trim_eol(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

static bool is_blank(const char *s) {
    if (!s) return true;
    while (*s) {
        if (!isspace((unsigned char)*s)) return false;
        s++;
    }
    return true;
}

static void print_usage(FILE *out, const char *prog) {
    fprintf(out,
        "Usage: %s [options] [query...]\n"
        "\n"
        "Native Kolibri CLI backed by the C inference core.\n"
        "\n"
        "Options:\n"
        "  -q, --ask TEXT        Run one query and exit\n"
        "  -i, --interactive     Start REPL even when stdin is piped\n"
        "      --json            Print one JSON object per answer\n"
        "      --stats           Include inference statistics in text mode\n"
        "      --no-stats        Hide REPL statistics\n"
        "      --no-color        Disable ANSI colors\n"
        "      --quiet           Hide banner and lifecycle messages\n"
        "      --list-knowledge  List embedded knowledge keys and exit\n"
        "  -V, --version         Print version and exit\n"
        "  -h, --help            Show this help\n"
        "\n"
        "Digital commands:\n"
        "  digit encode [TEXT]              Encode bytes/text to canonical digits\n"
        "  digit decode [DIGITS]            Decode canonical byte digits back to bytes\n"
        "  formula inspect literal DIGITS   Inspect a literal digit formula\n"
        "  formula inspect repeat DIGITS N  Inspect a repeat formula\n"
        "  formula inspect sequence S T N   Inspect a sequence formula\n"
        "  formula inspect meta GENE        Decode a 32/64-digit meta-formula gene\n"
        "\n"
        "Examples:\n"
        "  %s --ask README.md\n"
        "  %s --json --ask \"core/main_cli.c\"\n"
        "  printf 'README.md' | %s --json\n"
        "  %s digit encode Kolibri\n"
        "  %s --json formula inspect repeat 456 4\n",
        prog, prog, prog, prog, prog, prog);
}

static void print_digital_usage(FILE *out, const char *prog) {
    fprintf(out,
        "Usage:\n"
        "  %s [--json] digit encode [TEXT]\n"
        "  %s [--json] digit decode [DIGITS]\n"
        "  %s [--json] formula inspect literal DIGITS\n"
        "  %s [--json] formula inspect repeat DIGITS COUNT\n"
        "  %s [--json] formula inspect sequence START STEP COUNT\n"
        "  %s [--json] formula inspect meta GENE\n",
        prog, prog, prog, prog, prog, prog);
}

static void print_version(void) {
    printf("kolibri-cli %s\n", KOLIBRI_CLI_VERSION);
}

static int parse_options(int argc, char **argv, CliOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->positional_start = -1;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--") == 0) {
            opts->positional_start = i + 1;
            break;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(stdout, argv[0]);
            exit(0);
        } else if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            exit(0);
        } else if (strcmp(arg, "-q") == 0 || strcmp(arg, "--ask") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "kolibri-cli: %s requires a value\n", arg);
                return 2;
            }
            opts->ask = argv[++i];
        } else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--interactive") == 0) {
            opts->interactive = true;
        } else if (strcmp(arg, "--json") == 0) {
            opts->json = true;
        } else if (strcmp(arg, "--stats") == 0) {
            opts->stats = true;
        } else if (strcmp(arg, "--no-stats") == 0) {
            opts->no_stats = true;
        } else if (strcmp(arg, "--no-color") == 0) {
            opts->no_color = true;
        } else if (strcmp(arg, "--quiet") == 0) {
            opts->quiet = true;
        } else if (strcmp(arg, "--list-knowledge") == 0) {
            opts->list_knowledge = true;
        } else if (arg[0] == '-') {
            fprintf(stderr, "kolibri-cli: unknown option: %s\n", arg);
            fprintf(stderr, "Try '%s --help'.\n", argv[0]);
            return 2;
        } else {
            opts->positional_start = i;
            break;
        }
    }

    if (opts->ask && opts->positional_start >= 0) {
        fprintf(stderr, "kolibri-cli: use either --ask or positional query text, not both\n");
        return 2;
    }

    if (opts->stats && opts->no_stats) {
        fprintf(stderr, "kolibri-cli: --stats and --no-stats are mutually exclusive\n");
        return 2;
    }

    return 0;
}

static char *join_args(int argc, char **argv, int start) {
    if (start < 0 || start >= argc) return NULL;

    size_t len = 0;
    for (int i = start; i < argc; i++) {
        len += strlen(argv[i]) + (i == start ? 0 : 1);
        if (len > KOLIBRI_INF_MAX_QUERY) {
            fprintf(stderr, "kolibri-cli: query is too large\n");
            return NULL;
        }
    }

    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;

    out[0] = '\0';
    for (int i = start; i < argc; i++) {
        if (i > start) strcat(out, " ");
        strcat(out, argv[i]);
    }
    return out;
}

static char *read_all_stdin_bytes(size_t *out_len) {
    if (out_len) *out_len = 0;
    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(cap + 1);
    if (!buf) return NULL;

    while (!feof(stdin)) {
        if (len == cap) {
            if (cap >= KOLIBRI_INF_MAX_QUERY) {
                fprintf(stderr, "kolibri-cli: stdin exceeds %d bytes\n", KOLIBRI_INF_MAX_QUERY);
                free(buf);
                return NULL;
            }
            size_t next_cap = cap * 2U;
            if (next_cap > KOLIBRI_INF_MAX_QUERY) next_cap = KOLIBRI_INF_MAX_QUERY;
            char *next = (char *)realloc(buf, next_cap + 1U);
            if (!next) {
                free(buf);
                return NULL;
            }
            buf = next;
            cap = next_cap;
        }

        size_t n = fread(buf + len, 1, cap - len, stdin);
        len += n;
        if (ferror(stdin)) {
            free(buf);
            return NULL;
        }
    }

    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

static char *read_stdin_query(void) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;

    while (!feof(stdin)) {
        if (len + 2048 + 1 > cap) {
            size_t next_cap = cap * 2;
            if (next_cap > KOLIBRI_INF_MAX_QUERY + 1) next_cap = KOLIBRI_INF_MAX_QUERY + 1;
            if (next_cap <= cap) {
                fprintf(stderr, "kolibri-cli: stdin query exceeds %d bytes\n", KOLIBRI_INF_MAX_QUERY);
                free(buf);
                return NULL;
            }
            char *next = (char *)realloc(buf, next_cap);
            if (!next) {
                free(buf);
                return NULL;
            }
            buf = next;
            cap = next_cap;
        }

        size_t n = fread(buf + len, 1, cap - len - 1, stdin);
        len += n;
        if (ferror(stdin)) {
            free(buf);
            return NULL;
        }
    }

    buf[len] = '\0';
    trim_eol(buf);
    if (is_blank(buf)) {
        free(buf);
        return NULL;
    }
    return buf;
}

static size_t load_embedded_knowledge(void) {
    size_t loaded = 0;
    for (size_t i = 0; KNOWLEDGE_BASE[i].p != NULL; i++) {
        kolibri_mem_store(KNOWLEDGE_BASE[i].p, KNOWLEDGE_BASE[i].c, 1.0f);
        loaded++;
    }
    return loaded;
}

static void print_banner(const CliOptions *opts, size_t knowledge_count) {
    const bool color = stdout_supports_color(opts);
    printf("%s\n  KOLIBRI AI - Native CLI\n%s", paint(color, COLOR_CYAN), paint(color, COLOR_RESET));
    printf("%s  Core: C23 | Embedded knowledge: %zu entries\n%s",
           paint(color, COLOR_GRAY), knowledge_count, paint(color, COLOR_RESET));
    printf("  -----------------------------------------\n\n");
}

static void print_knowledge_list(void) {
    for (size_t i = 0; KNOWLEDGE_BASE[i].p != NULL; i++) {
        printf("%zu\t%s\n", i + 1, KNOWLEDGE_BASE[i].p);
    }
}

static void print_json_string(const char *s) {
    putchar('"');
    if (s) {
        for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
            switch (*p) {
                case '\\': fputs("\\\\", stdout); break;
                case '"': fputs("\\\"", stdout); break;
                case '\b': fputs("\\b", stdout); break;
                case '\f': fputs("\\f", stdout); break;
                case '\n': fputs("\\n", stdout); break;
                case '\r': fputs("\\r", stdout); break;
                case '\t': fputs("\\t", stdout); break;
                default:
                    if (*p < 0x20) {
                        printf("\\u%04x", *p);
                    } else {
                        putchar((int)*p);
                    }
            }
        }
    }
    putchar('"');
}

static void print_json_bytes(const uint8_t *bytes, size_t len) {
    putchar('"');
    for (size_t i = 0; i < len; ++i) {
        switch (bytes[i]) {
            case '\\': fputs("\\\\", stdout); break;
            case '"': fputs("\\\"", stdout); break;
            case '\b': fputs("\\b", stdout); break;
            case '\f': fputs("\\f", stdout); break;
            case '\n': fputs("\\n", stdout); break;
            case '\r': fputs("\\r", stdout); break;
            case '\t': fputs("\\t", stdout); break;
            default:
                if (bytes[i] < 0x20 || bytes[i] > 0x7e) {
                    printf("\\u%04x", bytes[i]);
                } else {
                    putchar(bytes[i]);
                }
        }
    }
    putchar('"');
}

static void print_bytes_hex(const uint8_t *bytes, size_t len) {
    static const char hex[] = "0123456789abcdef";
    putchar('"');
    for (size_t i = 0; i < len; ++i) {
        putchar(hex[bytes[i] >> 4]);
        putchar(hex[bytes[i] & 0x0f]);
    }
    putchar('"');
}

static int parse_size_value(const char *s, size_t *out) {
    if (!s || !*s || !out) return -1;
    size_t value = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (!isdigit(*p)) return -1;
        size_t digit = (size_t)(*p - '0');
        if (value > (SIZE_MAX - digit) / 10U) return -1;
        value = value * 10U + digit;
    }
    *out = value;
    return 0;
}

static int digits_from_ascii_arg(const char *ascii, uint8_t **out_digits, size_t *out_len) {
    if (!ascii || !out_digits || !out_len) return -1;
    size_t len = strlen(ascii);
    uint8_t *digits = len ? (uint8_t *)malloc(len) : NULL;
    if (!digits && len > 0) return -1;
    k_digit_stream stream;
    k_digit_stream_init(&stream, digits, len);
    if (k_ascii_to_digits(ascii, &stream) != 0) {
        free(digits);
        return -1;
    }
    *out_digits = digits;
    *out_len = stream.length;
    return 0;
}

static void print_formula_json(const char *type,
                               const k_digit_stream *stream,
                               int verified,
                               const KMetaFormula *meta) {
    char *ascii = (char *)malloc(stream->length + 1U);
    if (!ascii) {
        fputs("{\"error\":\"out_of_memory\"}\n", stdout);
        return;
    }
    if (k_digits_to_ascii(stream, ascii, stream->length + 1U) != 0) {
        free(ascii);
        fputs("{\"error\":\"invalid_digits\"}\n", stdout);
        return;
    }

    fputs("{\"command\":\"formula.inspect\",\"type\":", stdout);
    print_json_string(type);
    printf(",\"digit_count\":%zu,\"digits\":", stream->length);
    print_json_string(ascii);
    printf(",\"verified\":%s", verified ? "true" : "false");
    if (meta) {
        printf(",\"meta_operation\":%d,\"gene_length\":%zu", meta->operation, meta->gene_length);
    }
    fputs("}\n", stdout);
    free(ascii);
}

static int run_formula_output(const CliOptions *opts, const char *type, KDigitFormula *formula, const KMetaFormula *meta) {
    if (!formula) {
        fprintf(stderr, "kolibri-cli: failed to create formula\n");
        return 1;
    }

    size_t len = k_formula_digit_length(formula);
    uint8_t *digits = len ? (uint8_t *)malloc(len) : NULL;
    if (!digits && len > 0) {
        k_formula_destroy(formula);
        return 1;
    }

    k_digit_stream eval;
    k_digit_stream_init(&eval, digits, len);
    if (k_formula_eval(formula, &eval) != 0) {
        fprintf(stderr, "kolibri-cli: failed to evaluate formula\n");
        free(digits);
        k_formula_destroy(formula);
        return 1;
    }

    int verified = k_formula_verify(formula, &eval);
    if (opts->json) {
        print_formula_json(type, &eval, verified, meta);
    } else {
        char *ascii = (char *)malloc(eval.length + 1U);
        if (!ascii) {
            free(digits);
            k_formula_destroy(formula);
            return 1;
        }
        if (k_digits_to_ascii(&eval, ascii, eval.length + 1U) != 0) {
            free(ascii);
            free(digits);
            k_formula_destroy(formula);
            return 1;
        }
        printf("type: %s\n", type);
        printf("digit_count: %zu\n", eval.length);
        printf("digits: %s\n", ascii);
        printf("verified: %s\n", verified ? "true" : "false");
        if (meta) {
            printf("meta_operation: %d\n", meta->operation);
            printf("gene_length: %zu\n", meta->gene_length);
        }
        free(ascii);
    }

    free(digits);
    k_formula_destroy(formula);
    return 0;
}

static int run_digit_encode(int argc, char **argv, int start, const CliOptions *opts) {
    char *owned = NULL;
    const uint8_t *input = NULL;
    size_t input_len = 0;

    if (start < argc) {
        owned = join_args(argc, argv, start);
        if (!owned) return 1;
        input = (const uint8_t *)owned;
        input_len = strlen(owned);
    } else if (!isatty(STDIN_FILENO)) {
        owned = read_all_stdin_bytes(&input_len);
        if (!owned) return 1;
        input = (const uint8_t *)owned;
    } else {
        fprintf(stderr, "kolibri-cli: digit encode requires TEXT or stdin\n");
        return 2;
    }

    size_t digit_cap = k_digits_for_bytes(input_len);
    uint8_t *digits = digit_cap ? (uint8_t *)malloc(digit_cap) : NULL;
    char *ascii = (char *)malloc(digit_cap + 1U);
    if ((!digits && digit_cap > 0) || !ascii) {
        free(owned);
        free(digits);
        free(ascii);
        return 1;
    }

    k_digit_stream stream;
    k_digit_stream_init(&stream, digits, digit_cap);
    int rc = k_encode_bytes_to_digits(input, input_len, &stream);
    if (rc == 0) rc = k_digits_to_ascii(&stream, ascii, digit_cap + 1U);
    if (rc != 0) {
        fprintf(stderr, "kolibri-cli: digit encode failed\n");
        free(owned);
        free(digits);
        free(ascii);
        return 1;
    }

    if (opts->json) {
        fputs("{\"command\":\"digit.encode\",\"input_bytes\":", stdout);
        printf("%zu,\"digit_count\":%zu,\"canonical\":%s,\"digits\":",
               input_len, stream.length, k_validate_canonical_byte_digits(&stream) ? "true" : "false");
        print_json_string(ascii);
        fputs("}\n", stdout);
    } else {
        printf("%s\n", ascii);
    }

    free(owned);
    free(digits);
    free(ascii);
    return 0;
}

static int run_digit_decode(int argc, char **argv, int start, const CliOptions *opts) {
    char *owned = NULL;
    if (start < argc) {
        owned = join_args(argc, argv, start);
    } else if (!isatty(STDIN_FILENO)) {
        size_t ignored = 0;
        owned = read_all_stdin_bytes(&ignored);
        if (owned) trim_eol(owned);
    }
    if (!owned) {
        fprintf(stderr, "kolibri-cli: digit decode requires DIGITS or stdin\n");
        return 2;
    }

    uint8_t *digits = NULL;
    size_t digit_len = 0;
    if (digits_from_ascii_arg(owned, &digits, &digit_len) != 0) {
        fprintf(stderr, "kolibri-cli: digit decode expects only digits 0..9\n");
        free(owned);
        return 2;
    }

    k_digit_stream stream;
    k_digit_stream_init(&stream, digits, digit_len);
    stream.length = digit_len;
    if (!k_validate_canonical_byte_digits(&stream)) {
        fprintf(stderr, "kolibri-cli: digit stream is not canonical byte digits\n");
        free(owned);
        free(digits);
        return 2;
    }

    size_t out_len = k_bytes_for_digits(digit_len);
    uint8_t *bytes = out_len ? (uint8_t *)malloc(out_len) : NULL;
    if (!bytes && out_len > 0) {
        free(owned);
        free(digits);
        return 1;
    }

    size_t written = 0;
    if (k_decode_digits_to_bytes(&stream, bytes, out_len, &written) != 0) {
        fprintf(stderr, "kolibri-cli: digit decode failed\n");
        free(owned);
        free(digits);
        free(bytes);
        return 1;
    }

    if (opts->json) {
        printf("{\"command\":\"digit.decode\",\"digit_count\":%zu,\"byte_count\":%zu,\"bytes_hex\":",
               digit_len, written);
        print_bytes_hex(bytes, written);
        fputs(",\"text\":", stdout);
        print_json_bytes(bytes, written);
        fputs("}\n", stdout);
    } else {
        if (written > 0) fwrite(bytes, 1, written, stdout);
        putchar('\n');
    }

    free(owned);
    free(digits);
    free(bytes);
    return 0;
}

static int run_formula_inspect(int argc, char **argv, int start, const CliOptions *opts) {
    if (start >= argc) {
        print_digital_usage(stderr, argv[0]);
        return 2;
    }

    const char *kind = argv[start++];
    if (strcmp(kind, "literal") == 0) {
        if (start >= argc) return 2;
        uint8_t *digits = NULL;
        size_t len = 0;
        if (digits_from_ascii_arg(argv[start], &digits, &len) != 0) return 2;
        KDigitFormula *formula = k_formula_literal(digits, len);
        free(digits);
        return run_formula_output(opts, "literal", formula, NULL);
    }

    if (strcmp(kind, "repeat") == 0) {
        if (start + 1 >= argc) return 2;
        uint8_t *digits = NULL;
        size_t len = 0;
        size_t count = 0;
        if (digits_from_ascii_arg(argv[start], &digits, &len) != 0) return 2;
        if (parse_size_value(argv[start + 1], &count) != 0 || count == 0) {
            free(digits);
            return 2;
        }
        KDigitFormula *pattern = k_formula_literal(digits, len);
        KDigitFormula *formula = pattern ? k_formula_repeat(pattern, count) : NULL;
        if (!formula) k_formula_destroy(pattern);
        free(digits);
        return run_formula_output(opts, "repeat", formula, NULL);
    }

    if (strcmp(kind, "sequence") == 0) {
        if (start + 2 >= argc) return 2;
        size_t s = 0, step = 0, count = 0;
        if (parse_size_value(argv[start], &s) != 0 ||
            parse_size_value(argv[start + 1], &step) != 0 ||
            parse_size_value(argv[start + 2], &count) != 0 ||
            s > 9 || step > 9 || count == 0) {
            return 2;
        }
        KDigitFormula *formula = k_formula_sequence((uint8_t)s, (uint8_t)step, count);
        return run_formula_output(opts, "sequence", formula, NULL);
    }

    if (strcmp(kind, "meta") == 0) {
        if (start >= argc) return 2;
        uint8_t *digits = NULL;
        size_t len = 0;
        if (digits_from_ascii_arg(argv[start], &digits, &len) != 0) return 2;
        KMetaFormula meta;
        int rc = k_meta_formula_decode(digits, len, &meta);
        free(digits);
        if (rc != 0) {
            fprintf(stderr, "kolibri-cli: meta gene must be 32 or 64 digits and use supported op 00/01/02\n");
            return 2;
        }
        KDigitFormula *formula = k_meta_formula_execute(&meta);
        return run_formula_output(opts, "meta", formula, &meta);
    }

    fprintf(stderr, "kolibri-cli: unknown formula inspect kind: %s\n", kind);
    return 2;
}

static int parse_subcommand_options(int argc, char **argv, int *index, CliOptions *opts) {
    while (*index < argc) {
        const char *arg = argv[*index];
        if (strcmp(arg, "--json") == 0) {
            opts->json = true;
        } else if (strcmp(arg, "--no-color") == 0) {
            opts->no_color = true;
        } else if (strcmp(arg, "--quiet") == 0) {
            opts->quiet = true;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_digital_usage(stdout, argv[0]);
            exit(0);
        } else {
            break;
        }
        (*index)++;
    }
    return 0;
}

static int run_digital_subcommand(int argc, char **argv, int start, const CliOptions *global_opts) {
    CliOptions opts = *global_opts;
    int i = start;
    if (i >= argc) return 2;
    const char *group = argv[i++];
    parse_subcommand_options(argc, argv, &i, &opts);

    if (strcmp(group, "digit") == 0) {
        if (i >= argc) {
            print_digital_usage(stderr, argv[0]);
            return 2;
        }
        const char *cmd = argv[i++];
        parse_subcommand_options(argc, argv, &i, &opts);
        if (strcmp(cmd, "encode") == 0) return run_digit_encode(argc, argv, i, &opts);
        if (strcmp(cmd, "decode") == 0) return run_digit_decode(argc, argv, i, &opts);
        fprintf(stderr, "kolibri-cli: unknown digit command: %s\n", cmd);
        return 2;
    }

    if (strcmp(group, "formula") == 0) {
        if (i >= argc || strcmp(argv[i], "inspect") != 0) {
            print_digital_usage(stderr, argv[0]);
            return 2;
        }
        i++;
        parse_subcommand_options(argc, argv, &i, &opts);
        return run_formula_inspect(argc, argv, i, &opts);
    }

    return -1;
}

static int run_query(KolibriInferenceContext *ctx,
                     const CliOptions *opts,
                     const char *query,
                     size_t knowledge_count,
                     bool repl_mode) {
    KolibriCognitionResult result = {0};
    if (kolibri_inference_think(ctx, query, &result) != 0) {
        fprintf(stderr, "kolibri-cli: inference failed\n");
        return 1;
    }

    if (opts->json) {
        fputs("{\"query\":", stdout);
        print_json_string(query);
        fputs(",\"response\":", stdout);
        print_json_string(result.response ? result.response : "");
        printf(",\"digit_count\":%zu,\"duration_ms\":%.3f,\"confidence\":%.3f,\"knowledge_entries\":%zu}\n",
               result.digit_count,
               result.duration_ms,
               result.confidence,
               knowledge_count);
    } else {
        const bool color = stdout_supports_color(opts);
        if (repl_mode) {
            printf("%sKolibri >%s %s\n",
                   paint(color, COLOR_CYAN),
                   paint(color, COLOR_RESET),
                   result.response ? result.response : "");
        } else {
            printf("%s\n", result.response ? result.response : "");
        }

        const bool show_stats = opts->stats || (repl_mode && !opts->no_stats);
        if (show_stats) {
            printf("%s[stats: digits=%zu, time=%.2fms, confidence=%.2f, knowledge=%zu]%s\n",
                   paint(color, COLOR_GRAY),
                   result.digit_count,
                   result.duration_ms,
                   result.confidence,
                   knowledge_count,
                   paint(color, COLOR_RESET));
        }
    }

    free(result.response);
    free(result.digit_stream);
    return 0;
}

static int run_repl(KolibriInferenceContext *ctx,
                    const CliOptions *opts,
                    size_t knowledge_count) {
    char query[QUERY_LINE_CAP];
    const bool color = stdout_supports_color(opts);
    FILE *prompt_stream = opts->json ? stderr : stdout;

    if (!opts->quiet && !opts->json) {
        printf("%sCore ready. Type a query, 'exit', 'quit', or ':q'.%s\n",
               paint(color, COLOR_BLUE),
               paint(color, COLOR_RESET));
    }

    while (true) {
        fprintf(prompt_stream, "%s\nYou > %s", paint(color, COLOR_GREEN), paint(color, COLOR_RESET));
        fflush(prompt_stream);

        if (!fgets(query, sizeof(query), stdin)) {
            break;
        }
        trim_eol(query);

        if (strcmp(query, "exit") == 0 || strcmp(query, "quit") == 0 || strcmp(query, ":q") == 0) {
            break;
        }
        if (is_blank(query)) {
            continue;
        }

        int rc = run_query(ctx, opts, query, knowledge_count, true);
        if (rc != 0) return rc;
    }

    if (!opts->quiet && !opts->json) {
        printf("\nKolibri core stopped.\n");
    }
    return 0;
}

int main(int argc, char **argv) {
    CliOptions opts;
    int parse_rc = parse_options(argc, argv, &opts);
    if (parse_rc != 0) return parse_rc;

    if (opts.list_knowledge) {
        print_knowledge_list();
        return 0;
    }

    if (opts.positional_start >= 0) {
        int subcommand_rc = run_digital_subcommand(argc, argv, opts.positional_start, &opts);
        if (subcommand_rc >= 0) return subcommand_rc;
    }

    char *owned_query = NULL;
    const char *query = NULL;

    if (opts.ask) {
        query = opts.ask;
    } else if (opts.positional_start >= 0) {
        owned_query = join_args(argc, argv, opts.positional_start);
        if (!owned_query) return 1;
        query = owned_query;
    } else if (!opts.interactive && !isatty(STDIN_FILENO)) {
        owned_query = read_stdin_query();
        query = owned_query;
        if (!query) {
            fprintf(stderr, "kolibri-cli: empty stdin query\n");
            return 2;
        }
    }

    const bool repl_mode = opts.interactive || query == NULL;
    const bool quiet_engine = opts.quiet || opts.json || !repl_mode;
    if (quiet_engine) {
        kolibri_mem_set_quiet(1);
        setenv("KOLIBRI_QUIET", "1", 1);
    }

    const size_t knowledge_count = embedded_knowledge_count();
    if (repl_mode && !opts.quiet && !opts.json) {
        print_banner(&opts, knowledge_count);
    }

    KolibriInferenceContext *ctx = kolibri_inference_create();
    if (!ctx) {
        fprintf(stderr, "kolibri-cli: failed to initialize inference context\n");
        free(owned_query);
        return 1;
    }

    const size_t loaded = load_embedded_knowledge();
    int rc = repl_mode
        ? run_repl(ctx, &opts, loaded)
        : run_query(ctx, &opts, query, loaded, false);

    kolibri_inference_destroy(ctx);
    free(owned_query);
    return rc;
}
