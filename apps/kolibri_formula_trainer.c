#include "kolibri/web_crawler.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define KFT_MAX_URLS 256

typedef struct {
    const char *out_dir;
    int verbose;
    size_t saved;
} FormulaTrainerContext;

static unsigned long long fnv1a64(const char *text) {
    const unsigned char *bytes = (const unsigned char *)(text ? text : "");
    unsigned long long hash = 1469598103934665603ULL;
    while (*bytes) {
        hash ^= (unsigned long long)(*bytes++);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void trim_inplace(char *text) {
    if (!text || text[0] == '\0') {
        return;
    }
    size_t len = strlen(text);
    size_t start = 0U;
    while (start < len && isspace((unsigned char)text[start])) {
        ++start;
    }
    size_t end = len;
    while (end > start && isspace((unsigned char)text[end - 1U])) {
        --end;
    }
    if (start > 0U) {
        memmove(text, text + start, end - start);
    }
    text[end - start] = '\0';
}

static void clean_title_inplace(char *title) {
    if (!title) {
        return;
    }
    trim_inplace(title);
    char *wiki = strstr(title, "Википедия");
    if (!wiki) {
        wiki = strstr(title, "Wikipedia");
    }
    if (wiki) {
        char *sep = strstr(title, " — ");
        if (!sep) {
            sep = strstr(title, " - ");
        }
        if (!sep) {
            sep = strstr(title, " | ");
        }
        if (sep && sep < wiki) {
            *sep = '\0';
        } else {
            char *cut = wiki;
            while (cut > title && isspace((unsigned char)cut[-1])) {
                --cut;
            }
            *cut = '\0';
        }
        trim_inplace(title);
    }
}

static int is_noise_line(const char *line, const char *title) {
    if (!line || line[0] == '\0') {
        return 1;
    }
    if (title && strcmp(line, title) == 0) {
        return 1;
    }
    if (strncmp(line, "Материал из Википедии", 20) == 0 ||
        strstr(line, "Википедия") != NULL ||
        strstr(line, "Wikipedia") != NULL ||
        strncmp(line, "Перейти к", 9) == 0 ||
        strncmp(line, "Стабильная версия", 17) == 0 ||
        strncmp(line, "У этого термина", 15) == 0 ||
        strncmp(line, "Запрос ", 7) == 0 ||
        strncmp(line, "Содержание", 10) == 0 ||
        strncmp(line, "Медиафайлы", 10) == 0 ||
        strncmp(line, "Тема", 4) == 0 ||
        strncmp(line, "Наука", 5) == 0 ||
        strncmp(line, "Предмет изучения", 16) == 0 ||
        strncmp(line, "Период зарождения", 17) == 0 ||
        strncmp(line, "Основные направления", 20) == 0) {
        return 1;
    }
    size_t len = strlen(line);
    if (len < 3U) {
        return 1;
    }
    return 0;
}

static char *extract_definition_block(const char *text, const char *title) {
    if (!text) {
        return NULL;
    }
    size_t text_len = strlen(text);
    char *filtered = (char *)malloc(text_len + 1U);
    if (!filtered) {
        return NULL;
    }

    size_t out = 0U;
    const char *cursor = text;
    int started = 0;
    size_t kept_lines = 0U;
    while (*cursor && kept_lines < 4U) {
        const char *line_end = strchr(cursor, '\n');
        size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);
        char line[4096];
        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1U;
        }
        memcpy(line, cursor, line_len);
        line[line_len] = '\0';
        trim_inplace(line);

        if (!started) {
            if (is_noise_line(line, title)) {
                goto next_line;
            }
            if (strstr(line, "—") != NULL && strlen(line) >= 60U) {
                started = 1;
            } else {
                goto next_line;
            }
        }

        if (!is_noise_line(line, title)) {
            int written = snprintf(filtered + out,
                                   text_len + 1U - out,
                                   "%s%s",
                                   kept_lines == 0U ? "" : "\n",
                                   line);
            if (written > 0) {
                out += (size_t)written;
                kept_lines++;
            }
        }

next_line:
        if (!line_end) {
            break;
        }
        cursor = line_end + 1;
    }

    filtered[out] = '\0';
    if (kept_lines == 0U) {
        free(filtered);
        return NULL;
    }
    return filtered;
}

static int ensure_directory(const char *path) {
    if (!path || path[0] == '\0') {
        return -1;
    }
    char temp[4096];
    snprintf(temp, sizeof(temp), "%s", path);
    size_t len = strlen(temp);
    if (len == 0U) {
        return -1;
    }
    for (size_t i = 1U; i < len; ++i) {
        if (temp[i] != '/') {
            continue;
        }
        temp[i] = '\0';
        if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
        temp[i] = '/';
    }
    if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static int save_page_to_formula_dir(const KwcPage *page,
                                    const char *out_dir,
                                    size_t ordinal,
                                    int verbose) {
    if (!page || !out_dir || page->text_len == 0U || !page->text) {
        return -1;
    }
    char title[KWC_TITLE_MAX];
    snprintf(title, sizeof(title), "%s", page->title[0] != '\0' ? page->title : page->url);
    clean_title_inplace(title);
    if (title[0] == '\0') {
        snprintf(title, sizeof(title), "%s", page->url);
    }

    char *filtered = extract_definition_block(page->text, title);

    unsigned long long hash = fnv1a64(page->url);
    char path[4608];
    snprintf(path, sizeof(path), "%s/%06zu_%016llx.txt",
             out_dir,
             ordinal,
             hash);

    FILE *file = fopen(path, "wb");
    if (!file) {
        free(filtered);
        return -1;
    }
    fprintf(file, "# %s\n\n", title);
    if (filtered) {
        fwrite(filtered, 1U, strlen(filtered), file);
    } else {
        fwrite(page->text, 1U, page->text_len, file);
    }
    fputc('\n', file);
    fclose(file);
    free(filtered);

    if (verbose) {
        fprintf(stderr, "[formula-trainer] %s -> %s (%zu bytes)\n",
                page->url, path, page->text_len);
    }
    return 0;
}

static void crawl_save_cb(const KwcPage *page, void *userdata) {
    FormulaTrainerContext *ctx = (FormulaTrainerContext *)userdata;
    if (!ctx) {
        return;
    }
    if (save_page_to_formula_dir(page, ctx->out_dir, ctx->saved + 1U, ctx->verbose) == 0) {
        ctx->saved++;
    }
}

static int fetch_and_save_url(const char *url,
                              const KwcConfig *cfg,
                              FormulaTrainerContext *ctx) {
    KwcPage *page = kwc_fetch_page(url, cfg);
    if (!page) {
        fprintf(stderr, "[formula-trainer] failed to fetch %s\n", url);
        return -1;
    }
    int rc = save_page_to_formula_dir(page, ctx->out_dir, ctx->saved + 1U, ctx->verbose);
    if (rc == 0) {
        ctx->saved++;
    }
    kwc_free_page(page);
    return rc;
}

static int train_from_urls_file(const char *filepath,
                                const KwcConfig *cfg,
                                FormulaTrainerContext *ctx) {
    FILE *file = fopen(filepath, "r");
    if (!file) {
        fprintf(stderr, "[formula-trainer] cannot open %s\n", filepath);
        return -1;
    }
    char line[KWC_URL_MAX];
    while (fgets(line, sizeof(line), file)) {
        size_t len = strlen(line);
        while (len > 0U && (line[len - 1U] == '\n' || line[len - 1U] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0U || line[0] == '#') {
            continue;
        }
        (void)fetch_and_save_url(line, cfg, ctx);
        usleep((useconds_t)(cfg->delay_sec * 1000000.0));
    }
    fclose(file);
    return 0;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Kolibri Formula Trainer — web/text ingest for formula memory\n"
            "\nUsage: %s --out-dir DIR [options]\n"
            "\nOptions:\n"
            "  --out-dir DIR        Directory for saved .txt formula docs\n"
            "  --url URL            Fetch one URL (repeatable)\n"
            "  --urls FILE          File with URLs, one per line\n"
            "  --crawl URL          Crawl one site and save pages\n"
            "  --depth N            Crawl depth (default: 2)\n"
            "  --max-pages N        Crawl max pages (default: 100)\n"
            "  --delay SEC          Delay between requests (default: 0.3)\n"
            "  --verbose            Verbose output\n"
            "  --help               Show this help\n",
            prog);
}

int main(int argc, char **argv) {
    const char *out_dir = NULL;
    const char *urls_file = NULL;
    const char *crawl_url = NULL;
    const char *single_urls[KFT_MAX_URLS];
    size_t single_url_count = 0U;
    int verbose = 0;

    KwcConfig cfg = kwc_default_config();

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) {
            if (single_url_count < KFT_MAX_URLS) {
                single_urls[single_url_count++] = argv[++i];
            } else {
                fprintf(stderr, "[formula-trainer] too many --url entries\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--urls") == 0 && i + 1 < argc) {
            urls_file = argv[++i];
        } else if (strcmp(argv[i], "--crawl") == 0 && i + 1 < argc) {
            crawl_url = argv[++i];
        } else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) {
            cfg.max_depth = (size_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "--max-pages") == 0 && i + 1 < argc) {
            cfg.max_pages = (size_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            cfg.delay_sec = atof(argv[++i]);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
            cfg.verbose = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "[formula-trainer] unknown arg: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!out_dir) {
        fprintf(stderr, "[formula-trainer] --out-dir is required\n");
        print_usage(argv[0]);
        return 1;
    }
    if (ensure_directory(out_dir) != 0) {
        fprintf(stderr, "[formula-trainer] cannot create %s\n", out_dir);
        return 1;
    }

    FormulaTrainerContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.out_dir = out_dir;
    ctx.verbose = verbose;

    for (size_t i = 0; i < single_url_count; ++i) {
        (void)fetch_and_save_url(single_urls[i], &cfg, &ctx);
        usleep((useconds_t)(cfg.delay_sec * 1000000.0));
    }

    if (urls_file) {
        (void)train_from_urls_file(urls_file, &cfg, &ctx);
    }

    if (crawl_url) {
        (void)kwc_crawl_site(crawl_url, &cfg, crawl_save_cb, &ctx);
    }

    fprintf(stderr, "[formula-trainer] saved %zu documents into %s\n",
            ctx.saved, out_dir);
    return 0;
}
