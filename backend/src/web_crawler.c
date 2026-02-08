/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 *
 * Web Crawler — Реализация обхода сайтов для обучения Kolibri
 *
 * HTTP: через system curl (обработка HTTPS, редиректов, gzip)
 * HTML: встроенный парсер — удаление тегов, декодирование entities
 * BFS:  обход по ширине с дедупликацией и контролем домена
 */

#include "kolibri/web_crawler.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --- Внутренние константы --- */
#define READ_CHUNK    65536
#define DEFAULT_UA    "Mozilla/5.0 (compatible; KolibriBot/1.0)"

/* ============================================================
 * Утилиты URL
 * ============================================================ */

void kwc_url_domain(const char *url, char *domain, size_t domain_size)
{
    if (!url || !domain || domain_size == 0) { if (domain) domain[0] = '\0'; return; }

    /* Пропускаем scheme:// */
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;

    size_t i = 0;
    while (p[i] && p[i] != '/' && p[i] != ':' && p[i] != '?' && i < domain_size - 1) {
        domain[i] = (char)tolower((unsigned char)p[i]);
        i++;
    }
    domain[i] = '\0';
}

static void url_scheme(const char *url, char *scheme, size_t size)
{
    const char *p = strstr(url, "://");
    if (p) {
        size_t len = (size_t)(p - url);
        if (len >= size) len = size - 1;
        memcpy(scheme, url, len);
        scheme[len] = '\0';
    } else {
        strncpy(scheme, "https", size - 1);
        scheme[size - 1] = '\0';
    }
}

static void url_base(const char *url, char *base, size_t size)
{
    /* scheme://host (без пути) */
    const char *p = strstr(url, "://");
    if (!p) { base[0] = '\0'; return; }
    p += 3;
    const char *slash = strchr(p, '/');
    size_t len = slash ? (size_t)(slash - url) : strlen(url);
    if (len >= size) len = size - 1;
    memcpy(base, url, len);
    base[len] = '\0';
}

/* Преобразование относительного URL в абсолютный */
static void resolve_url(const char *href, const char *page_url,
                        char *out, size_t out_size)
{
    if (!href || !out || out_size == 0) return;

    /* Абсолютный URL */
    if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) {
        strncpy(out, href, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    /* Protocol-relative //example.com/... */
    if (strncmp(href, "//", 2) == 0) {
        char scheme[16];
        url_scheme(page_url, scheme, sizeof(scheme));
        snprintf(out, out_size, "%s:%s", scheme, href);
        return;
    }

    char base[KWC_URL_MAX];
    url_base(page_url, base, sizeof(base));

    if (href[0] == '/') {
        /* Абсолютный путь от корня */
        snprintf(out, out_size, "%s%s", base, href);
    } else {
        /* Относительный путь */
        const char *p = strstr(page_url, "://");
        const char *last_slash = NULL;
        if (p) {
            p += 3;
            last_slash = strrchr(p, '/');
        }
        if (last_slash && last_slash > p) {
            size_t prefix_len = (size_t)(last_slash - page_url + 1);
            if (prefix_len >= out_size) prefix_len = out_size - 1;
            memcpy(out, page_url, prefix_len);
            out[prefix_len] = '\0';
            strncat(out, href, out_size - prefix_len - 1);
        } else {
            snprintf(out, out_size, "%s/%s", base, href);
        }
    }
}

/* Очистка URL от shell-опасных символов */
static void sanitize_for_shell(const char *url, char *safe, size_t size)
{
    size_t j = 0;
    for (size_t i = 0; url[i] && j < size - 1; i++) {
        unsigned char c = (unsigned char)url[i];
        if (c == '\'' || c == '`' || c == '$' || c == '|' ||
            c == ';'  || c == '\n'|| c == '\r'|| c == '\\' ||
            c < 0x20)
            continue;
        safe[j++] = (char)c;
    }
    safe[j] = '\0';
}

/* ============================================================
 * HTTP-загрузка через system curl
 * ============================================================ */

static char *fetch_raw(const char *url, const KwcConfig *cfg, size_t *out_len)
{
    char safe_url[KWC_URL_MAX];
    sanitize_for_shell(url, safe_url, sizeof(safe_url));

    const char *ua = cfg->user_agent ? cfg->user_agent : DEFAULT_UA;

    char cmd[KWC_URL_MAX + 512];
    snprintf(cmd, sizeof(cmd),
        "curl -sL --max-time 15 --max-filesize %zu "
        "--compressed "
        "-H 'User-Agent: %s' '%s' 2>/dev/null",
        cfg->max_page_size, ua, safe_url);

    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    size_t capacity = READ_CHUNK;
    size_t len = 0;
    char *buf = malloc(capacity);
    if (!buf) { pclose(fp); return NULL; }

    size_t n;
    while ((n = fread(buf + len, 1, capacity - len - 1, fp)) > 0) {
        len += n;
        if (len >= capacity - 1) {
            size_t new_cap = capacity * 2;
            if (new_cap > cfg->max_page_size + 1)
                new_cap = cfg->max_page_size + 1;
            if (new_cap <= capacity) break; /* достигнут лимит */
            char *tmp = realloc(buf, new_cap);
            if (!tmp) break;
            buf = tmp;
            capacity = new_cap;
        }
    }
    pclose(fp);

    buf[len] = '\0';
    if (out_len) *out_len = len;

    if (len == 0) { free(buf); return NULL; }
    return buf;
}

/* ============================================================
 * HTML → чистый текст
 * ============================================================ */

/* Case-insensitive проверка имени тега */
static int tag_match(const char *p, const char *tag)
{
    size_t tlen = strlen(tag);
    for (size_t i = 0; i < tlen; i++) {
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)tag[i]))
            return 0;
    }
    char next = p[tlen];
    return next == ' ' || next == '>' || next == '/' ||
           next == '\t' || next == '\n' || next == '\0';
}

/* Декодирование HTML-entities (&amp; &lt; &#NNN; &#xHHH;) */
static void decode_entities(char *text)
{
    char *r = text, *w = text;
    while (*r) {
        if (*r != '&') { *w++ = *r++; continue; }
        if (strncmp(r, "&amp;",  5) == 0) { *w++ = '&';  r += 5; }
        else if (strncmp(r, "&lt;",   4) == 0) { *w++ = '<';  r += 4; }
        else if (strncmp(r, "&gt;",   4) == 0) { *w++ = '>';  r += 4; }
        else if (strncmp(r, "&quot;", 6) == 0) { *w++ = '"';  r += 6; }
        else if (strncmp(r, "&apos;", 6) == 0) { *w++ = '\''; r += 6; }
        else if (strncmp(r, "&nbsp;", 6) == 0) { *w++ = ' ';  r += 6; }
        else if (strncmp(r, "&mdash;",7) == 0) { *w++ = '-';  r += 7; }
        else if (strncmp(r, "&ndash;",7) == 0) { *w++ = '-';  r += 7; }
        else if (strncmp(r, "&laquo;",7) == 0) { *w++ = '"';  r += 7; }
        else if (strncmp(r, "&raquo;",7) == 0) { *w++ = '"';  r += 7; }
        else if (strncmp(r, "&#", 2) == 0) {
            char *end;
            unsigned long cp;
            if (r[2] == 'x' || r[2] == 'X')
                cp = strtoul(r + 3, &end, 16);
            else
                cp = strtoul(r + 2, &end, 10);
            if (*end == ';' && cp > 0) {
                if (cp < 128) *w++ = (char)cp;
                else *w++ = ' '; /* не-ASCII → пробел */
                r = end + 1;
            } else {
                *w++ = *r++;
            }
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

char *kwc_html_to_text(const char *html, size_t len)
{
    if (!html || len == 0) return NULL;

    char *out = malloc(len + 1);
    if (!out) return NULL;

    size_t oi = 0;
    int in_tag = 0;
    int skip_content = 0;
    const char *skip_end = NULL;
    size_t skip_end_len = 0;

    for (size_t i = 0; i < len; i++) {
        /* --- Ожидаем конец блока script/style --- */
        if (skip_content) {
            if (html[i] == '<' && i + skip_end_len <= len) {
                int match = 1;
                for (size_t k = 0; k < skip_end_len; k++) {
                    if (tolower((unsigned char)html[i + k]) !=
                        tolower((unsigned char)skip_end[k])) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    /* Пропускаем до > */
                    const char *gt = memchr(html + i, '>', len - i);
                    if (gt) i = (size_t)(gt - html);
                    skip_content = 0;
                    in_tag = 0;
                }
            }
            continue;
        }

        /* --- Начало тега --- */
        if (html[i] == '<') {
            in_tag = 1;
            const char *ts = html + i + 1;
            /* Пропускаем пробелы */
            while (*ts == ' ' || *ts == '\t') ts++;

            /* Блоки для полного пропуска содержимого */
            if (tag_match(ts, "script")) {
                skip_content = 1;
                skip_end = "</script";
                skip_end_len = 8;
                const char *gt = memchr(html + i, '>', len - i);
                if (gt) i = (size_t)(gt - html);
                continue;
            }
            if (tag_match(ts, "style")) {
                skip_content = 1;
                skip_end = "</style";
                skip_end_len = 7;
                const char *gt = memchr(html + i, '>', len - i);
                if (gt) i = (size_t)(gt - html);
                continue;
            }
            if (tag_match(ts, "noscript")) {
                skip_content = 1;
                skip_end = "</noscript";
                skip_end_len = 10;
                const char *gt = memchr(html + i, '>', len - i);
                if (gt) i = (size_t)(gt - html);
                continue;
            }

            /* Блочные элементы → перевод строки */
            if (tag_match(ts, "p") || tag_match(ts, "/p") ||
                tag_match(ts, "div") || tag_match(ts, "/div") ||
                tag_match(ts, "br") ||
                tag_match(ts, "h1") || tag_match(ts, "/h1") ||
                tag_match(ts, "h2") || tag_match(ts, "/h2") ||
                tag_match(ts, "h3") || tag_match(ts, "/h3") ||
                tag_match(ts, "h4") || tag_match(ts, "/h4") ||
                tag_match(ts, "h5") || tag_match(ts, "/h5") ||
                tag_match(ts, "li") || tag_match(ts, "/li") ||
                tag_match(ts, "tr") || tag_match(ts, "/tr") ||
                tag_match(ts, "dt") || tag_match(ts, "dd") ||
                tag_match(ts, "section") || tag_match(ts, "/section") ||
                tag_match(ts, "article") || tag_match(ts, "/article")) {
                if (oi > 0 && out[oi - 1] != '\n') out[oi++] = '\n';
            }

            /* <td>, <th> → пробел (разделитель ячеек) */
            if (tag_match(ts, "td") || tag_match(ts, "th")) {
                if (oi > 0 && out[oi - 1] != ' ' && out[oi - 1] != '\n')
                    out[oi++] = ' ';
            }
            continue;
        }

        /* --- Конец тега --- */
        if (html[i] == '>') {
            in_tag = 0;
            continue;
        }

        /* --- Текст вне тегов --- */
        if (!in_tag) {
            out[oi++] = html[i];
        }
    }
    out[oi] = '\0';

    /* Декодируем HTML entities */
    decode_entities(out);

    /* Нормализация пробелов */
    char *result = malloc(oi + 1);
    if (!result) { free(out); return NULL; }

    size_t ri = 0;
    int prev_space = 0;
    int newline_count = 0;
    for (size_t i = 0; out[i]; i++) {
        if (out[i] == '\n' || out[i] == '\r') {
            if (newline_count < 2 && ri > 0) {
                result[ri++] = '\n';
                newline_count++;
            }
            prev_space = 1;
        } else if (out[i] == ' ' || out[i] == '\t') {
            if (!prev_space && ri > 0) {
                result[ri++] = ' ';
                prev_space = 1;
            }
        } else {
            result[ri++] = out[i];
            prev_space = 0;
            newline_count = 0;
        }
    }
    result[ri] = '\0';
    free(out);

    return result;
}

/* ============================================================
 * Извлечение заголовка <title>
 * ============================================================ */

void kwc_extract_title(const char *html, size_t len,
                       char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!html) return;

    /* Ищем <title...> */
    for (size_t i = 0; i + 7 < len; i++) {
        if (html[i] != '<') continue;
        if (tolower((unsigned char)html[i + 1]) != 't') continue;
        if (!tag_match(html + i + 1, "title")) continue;

        /* Находим > */
        const char *gt = memchr(html + i + 6, '>', len - i - 6);
        if (!gt) return;
        gt++;

        /* Ищем </title */
        const char *end = gt;
        while (end < html + len - 7) {
            if (*end == '<' &&
                tolower((unsigned char)end[1]) == '/' &&
                tolower((unsigned char)end[2]) == 't' &&
                tolower((unsigned char)end[3]) == 'i' &&
                tolower((unsigned char)end[4]) == 't' &&
                tolower((unsigned char)end[5]) == 'l' &&
                tolower((unsigned char)end[6]) == 'e') {
                break;
            }
            end++;
        }

        size_t tlen = (size_t)(end - gt);
        if (tlen >= out_size) tlen = out_size - 1;
        memcpy(out, gt, tlen);
        out[tlen] = '\0';
        decode_entities(out);

        /* Trim пробелов */
        while (tlen > 0 &&
               (out[tlen - 1] == ' ' || out[tlen - 1] == '\n' ||
                out[tlen - 1] == '\r' || out[tlen - 1] == '\t'))
            out[--tlen] = '\0';
        return;
    }
}

/* ============================================================
 * Извлечение ссылок из HTML
 * ============================================================ */

size_t kwc_extract_links(const char *html, size_t len,
                         const char *base_url,
                         char ***out_links)
{
    if (!html || !out_links) return 0;
    *out_links = NULL;

    /* Выделяем массив заранее */
    size_t capacity = 128;
    char **links = calloc(capacity, sizeof(char *));
    if (!links) return 0;

    size_t found = 0;

    for (size_t i = 0; i + 5 < len && found < KWC_MAX_LINKS; i++) {
        /* Ищем <a (case-insensitive) */
        if (html[i] != '<') continue;
        char next = html[i + 1];
        if (next != 'a' && next != 'A') continue;
        char sp = html[i + 2];
        if (sp != ' ' && sp != '\t' && sp != '\n') continue;

        /* Находим конец тега */
        const char *tag_end = memchr(html + i, '>', len - i);
        if (!tag_end) continue;

        /* Ищем href="..." внутри тега */
        const char *href_attr = NULL;
        for (const char *p = html + i + 3; p < tag_end - 4; p++) {
            if (tolower((unsigned char)p[0]) == 'h' &&
                tolower((unsigned char)p[1]) == 'r' &&
                tolower((unsigned char)p[2]) == 'e' &&
                tolower((unsigned char)p[3]) == 'f' &&
                p[4] == '=') {
                href_attr = p + 5;
                break;
            }
        }
        if (!href_attr) continue;

        /* Извлекаем значение */
        char quote = 0;
        if (*href_attr == '"' || *href_attr == '\'') {
            quote = *href_attr;
            href_attr++;
        }

        const char *end;
        if (quote) {
            end = memchr(href_attr, quote, (size_t)(tag_end - href_attr));
        } else {
            end = href_attr;
            while (end < tag_end && *end != ' ' && *end != '>' && *end != '\t')
                end++;
        }
        if (!end || end <= href_attr) continue;

        size_t href_len = (size_t)(end - href_attr);
        if (href_len >= KWC_URL_MAX || href_len == 0) continue;

        char raw_href[KWC_URL_MAX];
        memcpy(raw_href, href_attr, href_len);
        raw_href[href_len] = '\0';

        /* Пропускаем javascript:, mailto:, tel:, # */
        if (strncmp(raw_href, "javascript:", 11) == 0 ||
            strncmp(raw_href, "mailto:", 7) == 0 ||
            strncmp(raw_href, "tel:", 4) == 0 ||
            raw_href[0] == '#')
            continue;

        /* Резолвим URL */
        char resolved[KWC_URL_MAX];
        resolve_url(raw_href, base_url, resolved, sizeof(resolved));

        /* Только http/https */
        if (strncmp(resolved, "http://", 7) != 0 &&
            strncmp(resolved, "https://", 8) != 0)
            continue;

        /* Убираем фрагмент (#section) */
        char *frag = strchr(resolved, '#');
        if (frag) *frag = '\0';

        /* Расширяем массив при необходимости */
        if (found >= capacity) {
            capacity *= 2;
            if (capacity > KWC_MAX_LINKS) capacity = KWC_MAX_LINKS;
            char **tmp = realloc(links, capacity * sizeof(char *));
            if (!tmp) break;
            links = tmp;
        }

        links[found] = strdup(resolved);
        if (links[found]) found++;
    }

    *out_links = links;
    return found;
}

/* ============================================================
 * Конфигурация по умолчанию
 * ============================================================ */

KwcConfig kwc_default_config(void)
{
    return (KwcConfig){
        .max_depth     = 2,
        .max_pages     = 100,
        .delay_sec     = 0.3,
        .max_page_size = 2 * 1024 * 1024,
        .min_text_len  = 100,
        .same_domain   = true,
        .verbose       = false,
        .user_agent    = NULL,
    };
}

/* ============================================================
 * Загрузка одной страницы
 * ============================================================ */

KwcPage *kwc_fetch_page(const char *url, const KwcConfig *cfg)
{
    if (!url || !cfg) return NULL;

    size_t raw_len = 0;
    char *raw = fetch_raw(url, cfg, &raw_len);
    if (!raw) return NULL;

    KwcPage *page = calloc(1, sizeof(KwcPage));
    if (!page) { free(raw); return NULL; }

    strncpy(page->url, url, KWC_URL_MAX - 1);

    /* Заголовок */
    kwc_extract_title(raw, raw_len, page->title, KWC_TITLE_MAX);
    if (page->title[0] == '\0')
        strncpy(page->title, "Untitled", KWC_TITLE_MAX - 1);

    /* Чистый текст */
    page->text = kwc_html_to_text(raw, raw_len);
    page->text_len = page->text ? strlen(page->text) : 0;

    /* Ссылки */
    page->link_count = kwc_extract_links(raw, raw_len, url, &page->links);

    free(raw);

    /* Проверка минимальной длины текста */
    if (page->text_len < cfg->min_text_len) {
        kwc_free_page(page);
        return NULL;
    }

    return page;
}

void kwc_free_page(KwcPage *page)
{
    if (!page) return;
    free(page->text);
    if (page->links) {
        for (size_t i = 0; i < page->link_count; i++)
            free(page->links[i]);
        free(page->links);
    }
    free(page);
}

/* ============================================================
 * Множество посещённых URL (хеш-множество)
 * ============================================================ */

#define VISITED_MASK (KWC_VISITED_CAP - 1)

typedef struct {
    uint32_t hashes[KWC_VISITED_CAP];
    size_t   count;
} UrlVisitedSet;

static uint32_t url_hash(const char *url)
{
    uint32_t h = 5381;
    while (*url) h = h * 33 + (unsigned char)*url++;
    return h | 1; /* non-zero */
}

static int visited_contains(const UrlVisitedSet *set, const char *url)
{
    uint32_t h = url_hash(url);
    for (size_t i = 0; i < 16; i++) {
        size_t slot = (h + i) & VISITED_MASK;
        if (set->hashes[slot] == h) return 1;
        if (set->hashes[slot] == 0) return 0;
    }
    return 0;
}

static void visited_add(UrlVisitedSet *set, const char *url)
{
    uint32_t h = url_hash(url);
    for (size_t i = 0; i < 16; i++) {
        size_t slot = (h + i) & VISITED_MASK;
        if (set->hashes[slot] == 0 || set->hashes[slot] == h) {
            set->hashes[slot] = h;
            set->count++;
            return;
        }
    }
    /* Таблица слишком полна — молча пропускаем */
}

/* ============================================================
 * BFS-краулер сайта
 * ============================================================ */

typedef struct {
    char url[KWC_URL_MAX];
    int  depth;
} CrawlEntry;

size_t kwc_crawl_site(const char *seed_url,
                      const KwcConfig *cfg,
                      kwc_page_callback cb,
                      void *userdata)
{
    if (!seed_url || !cfg || !cb) return 0;

    /* Проверка наличия curl */
    if (system("command -v curl >/dev/null 2>&1") != 0) {
        fprintf(stderr, "[Crawl] ОШИБКА: curl не найден в PATH\n");
        return 0;
    }

    /* Выделяем очередь и множество посещённых */
    size_t queue_cap = cfg->max_pages * 10;
    if (queue_cap > KWC_CRAWL_QUEUE_MAX) queue_cap = KWC_CRAWL_QUEUE_MAX;

    CrawlEntry *queue = calloc(queue_cap, sizeof(CrawlEntry));
    UrlVisitedSet *visited = calloc(1, sizeof(UrlVisitedSet));
    if (!queue || !visited) {
        free(queue);
        free(visited);
        return 0;
    }

    /* Извлекаем домен seed-URL */
    char seed_domain[256];
    kwc_url_domain(seed_url, seed_domain, sizeof(seed_domain));

    /* Засеиваем очередь */
    size_t q_head = 0, q_tail = 0;
    strncpy(queue[q_tail].url, seed_url, KWC_URL_MAX - 1);
    queue[q_tail].depth = 0;
    q_tail++;
    visited_add(visited, seed_url);

    size_t pages_fetched = 0;

    if (cfg->verbose)
        fprintf(stderr, "[Crawl] Начинаем обход: %s (домен: %s, "
                "глубина: %zu, макс: %zu)\n",
                seed_url, seed_domain, cfg->max_depth, cfg->max_pages);

    /* --- BFS --- */
    while (q_head < q_tail && pages_fetched < cfg->max_pages) {
        CrawlEntry entry = queue[q_head++];

        if (cfg->verbose) {
            fprintf(stderr, "[Crawl] [%zu/%zu] d=%d %s\n",
                    pages_fetched + 1, cfg->max_pages,
                    entry.depth, entry.url);
        }

        /* Загрузка страницы */
        KwcPage *page = kwc_fetch_page(entry.url, cfg);
        if (!page) {
            if (cfg->verbose)
                fprintf(stderr, "[Crawl] ✗ не удалось загрузить\n");
            continue;
        }

        page->depth = entry.depth;
        pages_fetched++;

        /* Вызов callback (обучение) */
        cb(page, userdata);

        /* Добавляем ссылки в очередь (если не на макс. глубине) */
        if ((size_t)entry.depth < cfg->max_depth) {
            for (size_t i = 0; i < page->link_count && q_tail < queue_cap; i++) {
                const char *link = page->links[i];
                if (!link) continue;

                /* Фильтр по домену */
                if (cfg->same_domain) {
                    char link_domain[256];
                    kwc_url_domain(link, link_domain, sizeof(link_domain));
                    if (strcmp(link_domain, seed_domain) != 0) continue;
                }

                /* Пропускаем расширения файлов (картинки, CSS, JS, PDF) */
                size_t llen = strlen(link);
                if (llen > 4) {
                    const char *ext = link + llen - 4;
                    if (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 ||
                        strcmp(ext, ".gif") == 0 || strcmp(ext, ".svg") == 0 ||
                        strcmp(ext, ".css") == 0 || strcmp(ext, ".pdf") == 0 ||
                        strcmp(ext, ".zip") == 0 || strcmp(ext, ".mp4") == 0 ||
                        strcmp(ext, ".mp3") == 0 || strcmp(ext, ".ico") == 0)
                        continue;
                }
                if (llen > 5) {
                    const char *ext5 = link + llen - 5;
                    if (strcmp(ext5, ".jpeg") == 0 || strcmp(ext5, ".webp") == 0 ||
                        strcmp(ext5, ".woff") == 0)
                        continue;
                }
                /* .js файлы */
                if (llen > 3) {
                    const char *ext3 = link + llen - 3;
                    if (strcmp(ext3, ".js") == 0) continue;
                }

                /* Пропускаем уже посещённые */
                if (visited_contains(visited, link)) continue;

                /* Добавляем в очередь */
                strncpy(queue[q_tail].url, link, KWC_URL_MAX - 1);
                queue[q_tail].depth = entry.depth + 1;
                q_tail++;
                visited_add(visited, link);
            }
        }

        kwc_free_page(page);

        /* Задержка между запросами (вежливый краулинг) */
        if (cfg->delay_sec > 0 && pages_fetched < cfg->max_pages) {
            usleep((useconds_t)(cfg->delay_sec * 1000000));
        }
    }

    free(queue);
    free(visited);

    if (cfg->verbose) {
        fprintf(stderr,
            "[Crawl] Завершено: %zu страниц загружено, "
            "%zu URL в очереди\n",
            pages_fetched, q_tail);
    }

    return pages_fetched;
}
