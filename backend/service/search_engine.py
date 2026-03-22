"""
Kolibri Search Engine Integration.

Поиск релевантных URL по теме через несколько поисковых систем.
Используется автономным агентом обучения.
Поддерживает: DuckDuckGo, Wikipedia (en/ru), Bing.
"""
from __future__ import annotations

import logging
import re
import time
from urllib.parse import parse_qs, quote_plus, unquote, urlparse
from typing import Optional

import requests
from bs4 import BeautifulSoup

logger = logging.getLogger("kolibri.search")

# --- Конфигурация ---
DEFAULT_USER_AGENT = (
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/120.0.0.0 Safari/537.36"
)

HEADERS = {
    "User-Agent": DEFAULT_USER_AGENT,
    "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    "Accept-Language": "en-US,en;q=0.9,ru;q=0.8",
    "Accept-Encoding": "gzip, deflate",
}

# Домены которые бесполезны для обучения
BLOCKED_DOMAINS = {
    "youtube.com", "facebook.com", "twitter.com", "instagram.com",
    "tiktok.com", "pinterest.com", "linkedin.com",
    "amazon.com", "ebay.com", "aliexpress.com",
    "play.google.com", "apps.apple.com",
    "web.archive.org",
}

# Ограничения для быстрого веб-обогащения в чате
_MAX_FETCH_BYTES = 1_500_000
_MAX_FETCH_CHARS = 80_000


def _is_good_url(url: str) -> bool:
    """Проверяем что URL подходит для обучения (текстовый контент)."""
    try:
        parsed = urlparse(url)
        domain = parsed.netloc.lower()

        for blocked in BLOCKED_DOMAINS:
            if blocked in domain:
                return False

        path = parsed.path.lower()
        bad_extensions = (
            ".pdf", ".jpg", ".jpeg", ".png", ".gif", ".svg",
            ".mp4", ".mp3", ".avi", ".mov", ".webm",
            ".zip", ".tar", ".gz", ".rar", ".7z",
            ".exe", ".dmg", ".apk", ".deb",
            ".css", ".js", ".json", ".xml",
        )
        if any(path.endswith(ext) for ext in bad_extensions):
            return False

        return bool(parsed.scheme in ("http", "https") and parsed.netloc)
    except Exception:
        return False


# ============================================================
# DuckDuckGo
# ============================================================

def search_duckduckgo(
    query: str,
    max_results: int = 10,
    timeout: int = 15,
) -> list[dict]:
    """
    Поиск через DuckDuckGo HTML-версию.
    Не требует API-ключа.
    """
    results: list[dict] = []
    try:
        url = "https://html.duckduckgo.com/html/"
        resp = requests.post(
            url,
            data={"q": query, "b": ""},
            headers=HEADERS,
            timeout=timeout,
        )
        resp.raise_for_status()

        soup = BeautifulSoup(resp.text, "html.parser")

        for result_div in soup.select(".result"):
            a_tag = result_div.select_one("a.result__a")
            if not a_tag:
                continue

            href = a_tag.get("href", "")
            title = a_tag.get_text(strip=True)

            # DuckDuckGo оборачивает URL в редиректы
            actual_url = href
            if "uddg=" in href:
                parsed = urlparse(href)
                params = parse_qs(parsed.query)
                uddg = params.get("uddg", [""])[0]
                if uddg:
                    actual_url = unquote(uddg)

            if not _is_good_url(actual_url):
                continue

            snippet_tag = result_div.select_one(".result__snippet")
            snippet = snippet_tag.get_text(strip=True) if snippet_tag else ""

            results.append({
                "url": actual_url,
                "title": title,
                "snippet": snippet[:200],
                "source": "duckduckgo",
            })

            if len(results) >= max_results:
                break

        logger.info("DuckDuckGo: '%s' → %d URLs", query, len(results))
    except Exception as e:
        logger.warning("DuckDuckGo search failed: %s", e)

    return results


# ============================================================
# Wikipedia API
# ============================================================

def search_wikipedia(
    query: str,
    max_results: int = 5,
    lang: str = "en",
    timeout: int = 10,
) -> list[dict]:
    """
    Поиск через Wikipedia API (opensearch).
    Высококачественные энциклопедические статьи.
    """
    results: list[dict] = []
    try:
        api_url = f"https://{lang}.wikipedia.org/w/api.php"
        params = {
            "action": "query",
            "list": "search",
            "srsearch": query,
            "srlimit": max_results,
            "format": "json",
            "utf8": 1,
        }
        resp = requests.get(
            api_url,
            params=params,
            headers={
                "User-Agent": "KolibriBot/1.0 (https://github.com/kolibri; kolibri@example.com)",
                "Accept": "application/json",
            },
            timeout=timeout,
        )
        resp.raise_for_status()
        data = resp.json()

        for item in data.get("query", {}).get("search", []):
            title = item["title"]
            page_url = (
                f"https://{lang}.wikipedia.org/wiki/"
                f"{quote_plus(title.replace(' ', '_'))}"
            )
            # Очищаем snippet от HTML
            snippet = BeautifulSoup(
                item.get("snippet", ""), "html.parser"
            ).get_text()

            results.append({
                "url": page_url,
                "title": title,
                "snippet": snippet[:200],
                "source": f"wikipedia_{lang}",
            })

        logger.info("Wikipedia (%s): '%s' → %d URLs", lang, query, len(results))
    except Exception as e:
        logger.warning("Wikipedia (%s) search failed: %s", lang, e)

    return results


# ============================================================
# Bing
# ============================================================

def search_bing(
    query: str,
    max_results: int = 10,
    timeout: int = 15,
) -> list[dict]:
    """
    Поиск через Bing HTML.
    Backup-движок на случай если DuckDuckGo блокирует.
    """
    results: list[dict] = []
    try:
        url = (
            f"https://www.bing.com/search"
            f"?q={quote_plus(query)}&count={max_results}"
        )
        resp = requests.get(url, headers=HEADERS, timeout=timeout)
        resp.raise_for_status()

        soup = BeautifulSoup(resp.text, "html.parser")

        for li in soup.select("li.b_algo"):
            a_tag = li.select_one("h2 a")
            if not a_tag:
                continue

            href = a_tag.get("href", "")
            title = a_tag.get_text(strip=True)

            if not _is_good_url(href):
                continue

            snippet_tag = li.select_one(".b_caption p")
            snippet = snippet_tag.get_text(strip=True) if snippet_tag else ""

            results.append({
                "url": href,
                "title": title,
                "snippet": snippet[:200],
                "source": "bing",
            })

            if len(results) >= max_results:
                break

        logger.info("Bing: '%s' → %d URLs", query, len(results))
    except Exception as e:
        logger.warning("Bing search failed: %s", e)

    return results


# ============================================================
# Генерация запросов
# ============================================================

def generate_search_queries(topic: str) -> list[str]:
    """
    Генерируем множество поисковых запросов из одной темы.
    Это увеличивает покрытие и разнообразие результатов.
    """
    topic_clean = topic.strip()

    queries = [
        topic_clean,
        f"{topic_clean} overview",
        f"{topic_clean} explained",
        f"{topic_clean} tutorial",
        f"{topic_clean} research",
        f"{topic_clean} applications",
        f"what is {topic_clean}",
        f"{topic_clean} technology 2024",
    ]

    # Русскоязычные запросы (если тема на кириллице)
    has_cyrillic = any("\u0400" <= c <= "\u04ff" for c in topic_clean)
    if has_cyrillic:
        queries.extend([
            f"{topic_clean} что это",
            f"{topic_clean} обзор",
            f"{topic_clean} технологии",
            f"{topic_clean} применение",
            f"{topic_clean} исследования",
        ])

    return queries


# ============================================================
# Комбинированный поиск по теме
# ============================================================

def search_topic(
    topic: str,
    max_urls: int = 30,
    engines: Optional[list[str]] = None,
) -> list[dict]:
    """
    Масштабный поиск по теме через все поисковые системы.

    Генерирует множество запросов, ищет через несколько движков,
    дедуплицирует и возвращает до max_urls результатов.
    """
    if engines is None:
        engines = ["duckduckgo", "wikipedia_en", "wikipedia_ru", "bing"]

    queries = generate_search_queries(topic)
    seen_urls: set[str] = set()
    all_results: list[dict] = []

    engine_handlers = {
        "duckduckgo": lambda q: search_duckduckgo(q, max_results=8),
        "wikipedia_en": lambda q: search_wikipedia(q, max_results=3, lang="en"),
        "wikipedia_ru": lambda q: search_wikipedia(q, max_results=3, lang="ru"),
        "bing": lambda q: search_bing(q, max_results=8),
    }

    for query in queries:
        if len(all_results) >= max_urls:
            break

        for engine in engines:
            if len(all_results) >= max_urls:
                break

            handler = engine_handlers.get(engine)
            if not handler:
                continue

            try:
                new_results = handler(query)
            except Exception as e:
                logger.warning("Engine %s failed for query '%s': %s", engine, query, e)
                continue

            # Дедупликация по нормализованному URL
            for r in new_results:
                normalized = r["url"].rstrip("/").lower()
                # Убираем якоря и параметры отслеживания
                normalized = re.sub(r"[?#].*$", "", normalized)

                if normalized not in seen_urls:
                    seen_urls.add(normalized)
                    r["query"] = query
                    all_results.append(r)

            # Маленькая пауза между движками
            time.sleep(0.2)

    logger.info("Topic search '%s': %d unique URLs from %d engines",
                topic, len(all_results), len(engines))

    return all_results[:max_urls]


def search_quick(
    query: str,
    max_urls: int = 8,
    include_bing_fallback: bool = True,
    timeout: int = 4,
) -> list[dict]:
    """
    Быстрый поиск для онлайн-обогащения ответа в чате.

    В отличие от search_topic() делает минимум сетевых запросов:
    - 1x DuckDuckGo
    - 1x Wikipedia (ru или en)
    - optional 1x Bing (fallback)
    """
    q = (query or "").strip()
    if not q:
        return []

    seen: set[str] = set()
    merged: list[dict] = []

    def _merge_block(block: list[dict]) -> bool:
        for r in block:
            url = str(r.get("url", "") or "").strip()
            if not url:
                continue
            normalized = re.sub(r"[?#].*$", "", url.rstrip("/").lower())
            if normalized in seen:
                continue
            seen.add(normalized)
            merged.append(r)
            if len(merged) >= max_urls:
                return True
        return False

    short_timeout = max(1, int(timeout))
    ddg = search_duckduckgo(
        q,
        max_results=max(4, min(10, max_urls)),
        timeout=short_timeout,
    )
    if _merge_block(ddg):
        return merged

    has_cyrillic = any("\u0400" <= c <= "\u04ff" for c in q)
    wiki_lang = "ru" if has_cyrillic else "en"
    wiki = search_wikipedia(
        q,
        max_results=3,
        lang=wiki_lang,
        timeout=short_timeout,
    )
    if _merge_block(wiki):
        return merged

    if include_bing_fallback and len(merged) < max_urls:
        bing = search_bing(
            q,
            max_results=max(3, min(8, max_urls)),
            timeout=short_timeout,
        )
        _merge_block(bing)
    return merged


def fetch_page_text(
    url: str,
    timeout: int = 10,
    max_chars: int = _MAX_FETCH_CHARS,
) -> tuple[str, str, int]:
    """
    Забрать и очистить текст страницы.

    Returns:
        (text, title, bytes_downloaded)
    """
    target = (url or "").strip()
    if not _is_good_url(target):
        return "", "", 0

    response = requests.get(
        target,
        headers=HEADERS,
        timeout=timeout,
        allow_redirects=True,
    )
    response.raise_for_status()

    content = response.content[:_MAX_FETCH_BYTES]
    bytes_downloaded = len(content)
    if bytes_downloaded <= 0:
        return "", "", 0

    content_type = (response.headers.get("Content-Type", "") or "").lower()
    if "text/html" not in content_type and "application/xhtml" not in content_type:
        return "", "", bytes_downloaded

    encoding = response.encoding or response.apparent_encoding or "utf-8"
    html = content.decode(encoding, errors="ignore")
    soup = BeautifulSoup(html, "html.parser")

    title_tag = soup.find("title")
    title = title_tag.get_text(strip=True) if title_tag else target

    for tag in soup([
        "script", "style", "nav", "footer", "header", "aside",
        "noscript", "iframe", "form", "button", "svg", "img",
        "video", "audio", "figure", "input", "select", "textarea",
    ]):
        tag.decompose()

    main_content = (
        soup.find("article")
        or soup.find("main")
        or soup.find(class_=re.compile(r"content|article|post|entry|text", re.I))
        or soup.find("div", {"id": re.compile(r"content|article|main|body", re.I)})
        or soup.find("body")
    )
    raw_text = (
        main_content.get_text(separator="\n", strip=True)
        if main_content is not None
        else soup.get_text(separator="\n", strip=True)
    )

    lines: list[str] = []
    for line in raw_text.split("\n"):
        clean = re.sub(r"\s+", " ", line.strip())
        if len(clean) >= 24:
            lines.append(clean)

    text = "\n".join(lines).strip()
    if len(text) > max_chars:
        text = text[:max_chars]
    return text, title, bytes_downloaded
