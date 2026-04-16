/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 *
 * Web Crawler — Обход сайтов для обучения Kolibri
 *
 * Архитектура:
 *   HTTP-запросы через system curl (обработка HTTPS, редиректов, cookies)
 *   HTML → текст: встроенный парсер (удаление тегов, декодирование entities)
 *   BFS-краулер: обход сайта с контролем глубины, домена, дедупликацией
 *
 * Интеграция:
 *   klm_crawl_site() вызывает callback на каждую страницу.
 *   В callback можно вызвать klm_train_document() для обучения.
 */

#ifndef KOLIBRI_WEB_CRAWLER_H
#define KOLIBRI_WEB_CRAWLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Лимиты --- */
#define KWC_URL_MAX         2048
#define KWC_TITLE_MAX       512
#define KWC_MAX_LINKS       500    /* Макс ссылок с одной страницы      */
#define KWC_CRAWL_QUEUE_MAX 50000  /* Макс записей в очереди            */
#define KWC_VISITED_CAP     65536  /* Ёмкость множества посещённых URL  */

/* --- Загруженная страница --- */
typedef struct {
    char    url[KWC_URL_MAX];
    char    title[KWC_TITLE_MAX];
    char   *text;          /* Чистый текст (malloc, нужен free)    */
    size_t  text_len;
    char  **links;         /* Массив ссылок (malloc, нужен free)   */
    size_t  link_count;
    int     depth;         /* Глубина обхода (0 = seed)            */
} KwcPage;

/* --- Конфигурация краулера --- */
typedef struct {
    size_t max_depth;       /* Максимальная глубина обхода (default: 2)   */
    size_t max_pages;       /* Максимум страниц (default: 100)            */
    double delay_sec;       /* Задержка между запросами (default: 0.3)    */
    size_t max_page_size;   /* Макс размер страницы (default: 2 МБ)      */
    size_t min_text_len;    /* Мин длина текста (default: 100)            */
    bool   same_domain;     /* Только тот же домен (default: true)        */
    bool   verbose;         /* Подробный вывод в stderr                   */
    const char *user_agent; /* User-Agent (NULL → KolibriBot/1.0)         */
} KwcConfig;

/* Конфигурация по умолчанию */
KwcConfig kwc_default_config(void);

/* --- Callback при загрузке страницы --- */
typedef void (*kwc_page_callback)(const KwcPage *page, void *userdata);

/* ============================================================
 * Основные функции
 * ============================================================ */

/* Загрузка одной страницы. Возвращает NULL при ошибке. */
KwcPage *kwc_fetch_page(const char *url, const KwcConfig *cfg);

/* Освобождение страницы */
void kwc_free_page(KwcPage *page);

/* BFS-краулинг сайта. Возвращает кол-во загруженных страниц. */
size_t kwc_crawl_site(const char *seed_url,
                      const KwcConfig *cfg,
                      kwc_page_callback cb,
                      void *userdata);

/* ============================================================
 * Утилиты HTML-обработки
 * ============================================================ */

/* Извлечение чистого текста из HTML (malloc, нужен free) */
char *kwc_html_to_text(const char *html, size_t len);

/* Извлечение заголовка <title> */
void kwc_extract_title(const char *html, size_t len,
                       char *out, size_t out_size);

/* Извлечение ссылок из HTML. Возвращает кол-во. */
size_t kwc_extract_links(const char *html, size_t len,
                         const char *base_url,
                         char ***out_links);

/* Извлечение домена из URL */
void kwc_url_domain(const char *url, char *domain, size_t domain_size);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_WEB_CRAWLER_H */
