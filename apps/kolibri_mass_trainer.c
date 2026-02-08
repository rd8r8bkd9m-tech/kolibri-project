/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 *
 * Kolibri Mass Trainer — CLI для масштабного обучения модели
 *
 * Использование:
 *   # Обучение на директории
 *   ./kolibri_mass_trainer --model model.klm --dir /path/to/texts
 *
 *   # Обучение из потока stdin (для интеграции с Python)
 *   python3 train_corpus.py --urls urls.txt | ./kolibri_mass_trainer --model model.klm --stdin
 *
 *   # Обучение на одном URL
 *   ./kolibri_mass_trainer --model model.klm --url https://example.com
 *
 *   # Обучение на списке URL из файла
 *   ./kolibri_mass_trainer --model model.klm --urls sites.txt
 *
 *   # Краулинг сайта (BFS-обход)
 *   ./kolibri_mass_trainer --model model.klm --crawl https://example.com --depth 3 --max-pages 500
 *
 *   # Показать статистику модели
 *   ./kolibri_mass_trainer --model model.klm --stats
 *
 *   # Запрос к модели
 *   ./kolibri_mass_trainer --model model.klm --query "что такое кот"
 *
 *   # Интерактивный режим
 *   ./kolibri_mass_trainer --model model.klm --interactive
 */

#include "kolibri/corpus_trainer.h"
#include "kolibri/web_crawler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Максимальный размер буфера для stdin-документа */
#define STDIN_BUF_SIZE (512 * 1024)

/* ============================================================
 * Режим обучения из stdin (протокол DOC/END_DOC/DONE)
 * ============================================================ */
static int train_from_stdin(KlmTrainerContext *ctx, int verbose)
{
    char *buf = malloc(STDIN_BUF_SIZE);
    if (!buf) {
        fprintf(stderr, "[ERROR] Не удалось выделить буфер\n");
        return -1;
    }

    char title[512] = {0};
    size_t body_len = 0;
    int in_doc = 0;
    size_t docs_ok = 0;
    size_t docs_fail = 0;

    while (fgets(buf + (in_doc ? body_len : 0),
                 (int)(STDIN_BUF_SIZE - body_len), stdin)) {

        char *line = buf + (in_doc ? 0 : 0);

        if (!in_doc) {
            /* Ожидаем "DOC <title>" или "DONE" */
            if (strncmp(buf, "DONE", 4) == 0)
                break;

            if (strncmp(buf, "DOC ", 4) == 0) {
                in_doc = 1;
                body_len = 0;
                /* Извлекаем title */
                char *nl = strchr(buf + 4, '\n');
                size_t tlen = nl ? (size_t)(nl - buf - 4) : strlen(buf + 4);
                if (tlen >= sizeof(title)) tlen = sizeof(title) - 1;
                memcpy(title, buf + 4, tlen);
                title[tlen] = '\0';
            }
        } else {
            /* Внутри документа — собираем текст */
            line = buf + body_len;

            if (strncmp(line, "END_DOC", 7) == 0) {
                /* Конец документа — обучаем */
                buf[body_len] = '\0';
                if (klm_train_document(ctx, title, buf) == 0) {
                    docs_ok++;
                    if (verbose && docs_ok % 100 == 0)
                        fprintf(stderr, "[Train] %zu документов обработано\n",
                                docs_ok);
                } else {
                    docs_fail++;
                }
                in_doc = 0;
                body_len = 0;
                title[0] = '\0';
            } else {
                body_len = strlen(buf);
                if (body_len >= STDIN_BUF_SIZE - 256) {
                    /* Буфер переполнен — обрезаем */
                    buf[body_len] = '\0';
                    klm_train_document(ctx, title, buf);
                    docs_ok++;
                    in_doc = 0;
                    body_len = 0;
                }
            }
        }
    }

    free(buf);
    fprintf(stderr, "[Stdin] Готово: %zu OK, %zu ошибок\n", docs_ok, docs_fail);
    return 0;
}

/* ============================================================
 * Callback краулера — обучение на каждой странице
 * ============================================================ */
struct crawl_train_ctx {
    KlmTrainerContext *trainer;
    int verbose;
    size_t trained;
};

static void crawl_train_cb(const KwcPage *page, void *userdata)
{
    struct crawl_train_ctx *tc = userdata;
    if (tc->verbose) {
        fprintf(stderr, "[Train] %s — %s (%zu байт, %zu ссылок)\n",
                page->url, page->title, page->text_len, page->link_count);
    }
    klm_train_document(tc->trainer, page->title, page->text);
    tc->trained++;
}

/* ============================================================
 * Обучение по одному URL
 * ============================================================ */
static int train_from_url(KlmTrainerContext *ctx, const char *url, int verbose)
{
    KwcConfig cfg = kwc_default_config();
    cfg.verbose = verbose;

    KwcPage *page = kwc_fetch_page(url, &cfg);
    if (!page) {
        fprintf(stderr, "[ERROR] Не удалось загрузить: %s\n", url);
        return -1;
    }

    fprintf(stderr, "[Train] URL: %s (%zu байт, title: %s)\n",
            url, page->text_len, page->title);

    klm_train_document(ctx, page->title, page->text);
    kwc_free_page(page);
    return 0;
}

/* ============================================================
 * Обучение по файлу со списком URL
 * ============================================================ */
static int train_from_urls_file(KlmTrainerContext *ctx, const char *filepath,
                                int verbose)
{
    FILE *f = fopen(filepath, "r");
    if (!f) {
        fprintf(stderr, "[ERROR] Не удалось открыть: %s\n", filepath);
        return -1;
    }

    KwcConfig cfg = kwc_default_config();
    cfg.verbose = verbose;

    char line[KWC_URL_MAX];
    size_t ok = 0, fail = 0;
    while (fgets(line, sizeof(line), f)) {
        /* Trim */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len < 5 || line[0] == '#') continue;

        KwcPage *page = kwc_fetch_page(line, &cfg);
        if (page) {
            klm_train_document(ctx, page->title, page->text);
            ok++;
            if (verbose || ok % 10 == 0) {
                fprintf(stderr, "[Train] [%zu] %s (%zu байт)\n",
                        ok, line, page->text_len);
            }
            kwc_free_page(page);
        } else {
            fail++;
        }
        usleep(300000); /* 300мс задержка между запросами */
    }

    fclose(f);
    fprintf(stderr, "[Train] Из файла %s: %zu OK, %zu ошибок\n",
            filepath, ok, fail);
    return 0;
}

/* ============================================================
 * Краулинг сайта с обучением
 * ============================================================ */
static int train_crawl_site(KlmTrainerContext *ctx, const char *seed_url,
                            size_t depth, size_t max_pages, double delay,
                            int verbose)
{
    KwcConfig cfg = kwc_default_config();
    cfg.max_depth   = depth;
    cfg.max_pages   = max_pages;
    cfg.delay_sec   = delay;
    cfg.verbose     = verbose;
    cfg.same_domain = true;

    struct crawl_train_ctx tc = {
        .trainer = ctx,
        .verbose = verbose,
        .trained = 0,
    };

    char domain[256];
    kwc_url_domain(seed_url, domain, sizeof(domain));
    fprintf(stderr,
        "\n╔═══════════════════════════════════════════════╗\n"
        "║          Kolibri Web Crawler                  ║\n"
        "╠═══════════════════════════════════════════════╣\n"
        "║ Seed:      %-34s ║\n"
        "║ Домен:     %-34s ║\n"
        "║ Глубина:   %-34zu ║\n"
        "║ Макс стр:  %-34zu ║\n"
        "║ Задержка:  %-34.1f ║\n"
        "╚═══════════════════════════════════════════════╝\n\n",
        seed_url, domain, depth, max_pages, delay);

    clock_t t0 = clock();
    size_t fetched = kwc_crawl_site(seed_url, &cfg, crawl_train_cb, &tc);
    double elapsed = (double)(clock() - t0) / CLOCKS_PER_SEC;

    fprintf(stderr,
        "\n[Crawl] Результат: %zu страниц загружено, "
        "%zu обучено за %.1f сек\n",
        fetched, tc.trained, elapsed);

    return 0;
}

/* ============================================================
 * Интерактивный режим
 * ============================================================ */
static void interactive_mode(const KlmTrainerContext *ctx)
{
    char line[1024];
    char answer[4096];

    fprintf(stderr,
        "\n=== Kolibri Interactive Mode ===\n"
        "Введите вопрос (или 'quit' для выхода):\n\n");

    while (1) {
        fprintf(stderr, "kolibri> ");
        if (!fgets(line, sizeof(line), stdin)) break;

        /* Убираем перевод строки */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0)
            break;

        if (strlen(line) < 2) continue;

        /* Пробуем ответить */
        if (klm_answer(ctx, line, answer, sizeof(answer)) == 0) {
            fprintf(stderr, "  → %s\n\n", answer);
        } else {
            fprintf(stderr, "  → (нет ассоциаций для этого запроса)\n\n");
        }

        /* Показываем ассоциации для каждого слова */
        char assocs[10][KLM_WORD_MAX];
        float weights[10];
        /* Показываем ассоциации первого значимого слова */
        char *word = strtok(line, " ");
        while (word) {
            if (strlen(word) >= 3) {
                size_t n = klm_get_associations(ctx, word, assocs, weights, 5);
                if (n > 0) {
                    fprintf(stderr, "  [%s]:", word);
                    for (size_t i = 0; i < n; i++)
                        fprintf(stderr, " %s(%.2f)", assocs[i], weights[i]);
                    fprintf(stderr, "\n");
                }
            }
            word = strtok(NULL, " ");
        }
        fprintf(stderr, "\n");
    }
}

/* ============================================================
 * Печать использования
 * ============================================================ */
static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Kolibri Mass Trainer — масштабное обучение с фиксированным размером\n"
        "\nИспользование: %s [опции]\n"
        "\nИсточники данных:\n"
        "  --dir <path>          Обучение на директории с текстами\n"
        "  --file <path>         Обучение на одном файле\n"
        "  --stdin               Обучение из stdin (протокол DOC/END_DOC/DONE)\n"
        "  --url <url>           Обучение на одном URL\n"
        "  --urls <file>         Обучение на URL из файла (по одному на строку)\n"
        "  --crawl <url>         Краулинг сайта (BFS-обход по ссылкам)\n"
        "\nОпции краулера:\n"
        "  --depth <N>           Глубина обхода (по умолчанию: 2)\n"
        "  --max-pages <N>       Максимум страниц при краулинге (100)\n"
        "  --delay <sec>         Задержка между запросами (0.3)\n"
        "\nМодель:\n"
        "  --model <path>        Путь к файлу модели (.klm)\n"
        "  --stats               Показать статистику модели\n"
        "  --query <text>        Задать вопрос модели\n"
        "  --interactive         Интерактивный режим\n"
        "\nОбучение:\n"
        "  --generations <N>     Поколений эволюции на слово (10)\n"
        "  --distill-every <N>   Дистилляция каждые N документов (1000)\n"
        "  --verbose             Подробный вывод\n"
        "  --help                Эта справка\n"
        "\nПримеры:\n"
        "  %s --model brain.klm --dir ./corpus/\n"
        "  %s --model brain.klm --url https://ru.wikipedia.org/wiki/Python\n"
        "  %s --model brain.klm --crawl https://example.com --depth 3 --max-pages 200\n"
        "  %s --model brain.klm --urls sites.txt\n"
        "  %s --model brain.klm --query \"что такое программирование\"\n",
        prog, prog, prog, prog, prog, prog);
}

/* ============================================================
 * main
 * ============================================================ */
int main(int argc, char **argv)
{
    const char *model_path = NULL;
    const char *dir_path = NULL;
    const char *file_path = NULL;
    const char *query = NULL;
    int query_digits_mode = 0;
    const char *single_url = NULL;
    const char *urls_file = NULL;
    const char *crawl_url = NULL;
    int do_stdin = 0;
    int do_stats = 0;
    int do_interactive = 0;
    int verbose = 0;
    size_t crawl_depth = 2;
    size_t crawl_max_pages = 100;
    double crawl_delay = 0.3;

    KlmTrainerConfig config = klm_default_config();

    /* Парсинг аргументов */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--model") == 0 && i + 1 < argc)
            model_path = argv[++i];
        else if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc)
            dir_path = argv[++i];
        else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc)
            file_path = argv[++i];
        else if (strcmp(argv[i], "--stdin") == 0)
            do_stdin = 1;
        else if (strcmp(argv[i], "--url") == 0 && i + 1 < argc)
            single_url = argv[++i];
        else if (strcmp(argv[i], "--urls") == 0 && i + 1 < argc)
            urls_file = argv[++i];
        else if (strcmp(argv[i], "--crawl") == 0 && i + 1 < argc)
            crawl_url = argv[++i];
        else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc)
            crawl_depth = (size_t)atol(argv[++i]);
        else if (strcmp(argv[i], "--max-pages") == 0 && i + 1 < argc)
            crawl_max_pages = (size_t)atol(argv[++i]);
        else if (strcmp(argv[i], "--delay") == 0 && i + 1 < argc)
            crawl_delay = atof(argv[++i]);
        else if (strcmp(argv[i], "--stats") == 0)
            do_stats = 1;
        else if (strcmp(argv[i], "--query") == 0 && i + 1 < argc)
            query = argv[++i];
        else if (strcmp(argv[i], "--query-digits") == 0 && i + 1 < argc) {
            query = argv[++i];
            query_digits_mode = 1;
        }
        else if (strcmp(argv[i], "--interactive") == 0)
            do_interactive = 1;
        else if (strcmp(argv[i], "--generations") == 0 && i + 1 < argc)
            config.evolution_generations = (size_t)atol(argv[++i]);
        else if (strcmp(argv[i], "--distill-every") == 0 && i + 1 < argc)
            config.distill_interval = (size_t)atol(argv[++i]);
        else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
            config.verbose = true;
        }
        else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        else {
            fprintf(stderr, "[ERROR] Неизвестный аргумент: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!model_path) {
        fprintf(stderr, "[ERROR] Укажите --model <path>\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Создаём тренер */
    KlmTrainerContext *ctx = klm_trainer_create(&config);
    if (!ctx) {
        fprintf(stderr, "[ERROR] Не удалось создать контекст обучения\n");
        return 1;
    }

    /* Пытаемся загрузить существующую модель */
    FILE *test = fopen(model_path, "rb");
    if (test) {
        fclose(test);
        if (klm_load(ctx, model_path) == 0) {
            fprintf(stderr, "[Model] Загружена: %s (%zu паттернов, %zu рёбер)\n",
                    model_path, ctx->model.pattern_count,
                    ctx->model.edge_count);
        } else {
            fprintf(stderr, "[Model] Не удалось загрузить %s, начинаем с нуля\n",
                    model_path);
        }
    }

    /* --- Режим: статистика --- */
    if (do_stats) {
        klm_print_stats(ctx);
        klm_trainer_free(ctx);
        return 0;
    }

    /* --- Режим: запрос --- */
    if (query) {
        if (query_digits_mode) {
            /* Числовой формат: каждый байт UTF-8 → 3 цифры */
            uint8_t digits[12288]; /* 4096 символов × 3 цифры */
            size_t dlen = 0;
            if (klm_answer_digits(ctx, query, digits, sizeof(digits), &dlen) == 0 && dlen > 0) {
                for (size_t i = 0; i < dlen; ++i)
                    printf("%u", (unsigned)digits[i]);
                printf("\n");
            } else {
                printf("(нет ассоциаций)\n");
            }
        } else {
            char answer[4096];
            if (klm_answer(ctx, query, answer, sizeof(answer)) == 0) {
                printf("%s\n", answer);
            } else {
                printf("(нет ассоциаций)\n");
            }
        }
        klm_trainer_free(ctx);
        return 0;
    }

    /* --- Режим: интерактивный --- */
    if (do_interactive) {
        interactive_mode(ctx);
        klm_trainer_free(ctx);
        return 0;
    }

    /* --- Режим: обучение --- */
    clock_t start = clock();

    if (dir_path) {
        fprintf(stderr, "[Train] Обучение на директории: %s\n", dir_path);
        size_t count = klm_train_directory(ctx, dir_path);
        fprintf(stderr, "[Train] Обработано %zu файлов\n", count);
    }

    if (file_path) {
        fprintf(stderr, "[Train] Обучение на файле: %s\n", file_path);
        klm_train_file(ctx, file_path);
    }

    if (single_url) {
        train_from_url(ctx, single_url, verbose);
    }

    if (urls_file) {
        fprintf(stderr, "[Train] Обучение по URL из файла: %s\n", urls_file);
        train_from_urls_file(ctx, urls_file, verbose);
    }

    if (crawl_url) {
        train_crawl_site(ctx, crawl_url, crawl_depth, crawl_max_pages,
                         crawl_delay, verbose);
    }

    if (do_stdin) {
        fprintf(stderr, "[Train] Обучение из stdin (протокол DOC/END_DOC/DONE)\n");
        train_from_stdin(ctx, verbose);
    }

    /* Финальная дистилляция */
    fprintf(stderr, "[Train] Финальная дистилляция...\n");
    size_t evicted = klm_distill(ctx);
    fprintf(stderr, "[Train] Вытеснено %zu записей\n", evicted);

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    fprintf(stderr, "[Train] Время обучения: %.1f сек\n", elapsed);

    /* Сохраняем модель */
    if (klm_save(ctx, model_path) == 0) {
        fprintf(stderr, "[Model] Сохранена: %s (%.2f МБ)\n",
                model_path, klm_model_size_mb(ctx));
    } else {
        fprintf(stderr, "[ERROR] Не удалось сохранить модель\n");
    }

    /* Статистика */
    klm_print_stats(ctx);

    klm_trainer_free(ctx);
    return 0;
}
