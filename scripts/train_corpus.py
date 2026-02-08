#!/usr/bin/env python3
"""
Kolibri Corpus Fetcher — парсинг веб-страниц для обучения модели.

Загружает страницы по URL, извлекает чистый текст, передаёт в
kolibri_mass_trainer через stdout (протокол DOC/END_DOC/DONE).

Использование:
    # Из файла с URL
    python3 train_corpus.py --urls urls.txt --model brain.klm

    # Из Wikipedia (N случайных статей)
    python3 train_corpus.py --wiki --count 1000 --model brain.klm

    # Из конкретных URL
    python3 train_corpus.py --url https://ru.wikipedia.org/wiki/Python \
                            --model brain.klm

    # Только загрузка (без обучения, сохранить тексты в директорию)
    python3 train_corpus.py --urls urls.txt --save-dir ./corpus/

Требования: pip install requests beautifulsoup4
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
import re
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import TextIO
from urllib.parse import urljoin, urlparse

# Опциональные зависимости
try:
    import requests
    HAS_REQUESTS = True
except ImportError:
    HAS_REQUESTS = False

try:
    from bs4 import BeautifulSoup
    HAS_BS4 = True
except ImportError:
    HAS_BS4 = False


# ================================================================
# Конфигурация
# ================================================================

DEFAULT_TIMEOUT = 10
DEFAULT_DELAY = 0.5  # секунд между запросами
MAX_PAGE_SIZE = 5 * 1024 * 1024  # 5 МБ
MIN_TEXT_LENGTH = 100  # минимум символов для обучения
USER_AGENT = (
    "Mozilla/5.0 (compatible; KolibriBot/1.0; "
    "+https://github.com/kolibri-project)"
)


# ================================================================
# Извлечение текста из HTML
# ================================================================

def html_to_text_bs4(raw_html: str) -> str:
    """Извлечение текста через BeautifulSoup (качественно)."""
    soup = BeautifulSoup(raw_html, "html.parser")

    # Удаляем ненужные элементы
    for tag in soup.find_all(["script", "style", "nav", "footer",
                               "header", "aside", "form", "noscript"]):
        tag.decompose()

    # Извлекаем текст
    text = soup.get_text(separator="\n", strip=True)
    # Нормализация пробелов
    text = re.sub(r"\n{3,}", "\n\n", text)
    text = re.sub(r"[ \t]+", " ", text)
    return text.strip()


def html_to_text_simple(raw_html: str) -> str:
    """Извлечение текста через regex (без зависимостей, хуже качество)."""
    # Удаляем теги script и style
    text = re.sub(r"<(script|style)[^>]*>.*?</\1>", "", raw_html,
                  flags=re.DOTALL | re.IGNORECASE)
    # Удаляем все теги
    text = re.sub(r"<[^>]+>", " ", text)
    # Декодируем HTML entities
    text = html.unescape(text)
    # Нормализация
    text = re.sub(r"\s+", " ", text)
    return text.strip()


def extract_text(raw_html: str) -> str:
    """Лучший доступный метод."""
    if HAS_BS4:
        return html_to_text_bs4(raw_html)
    return html_to_text_simple(raw_html)


def extract_title(raw_html: str) -> str:
    """Извлечение заголовка страницы."""
    m = re.search(r"<title[^>]*>(.*?)</title>", raw_html,
                  re.IGNORECASE | re.DOTALL)
    if m:
        return html.unescape(m.group(1)).strip()[:256]
    return "Untitled"


# ================================================================
# Загрузка URL
# ================================================================

def fetch_url(url: str, timeout: int = DEFAULT_TIMEOUT) -> tuple[str, str] | None:
    """Загружает страницу, возвращает (title, text) или None."""
    try:
        if HAS_REQUESTS:
            resp = requests.get(
                url,
                timeout=timeout,
                headers={"User-Agent": USER_AGENT},
                allow_redirects=True,
            )
            resp.raise_for_status()
            raw = resp.text
        else:
            # Фоллбэк на urllib
            import urllib.request
            req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                charset = resp.headers.get_content_charset() or "utf-8"
                raw = resp.read().decode(charset, errors="replace")

        if len(raw) > MAX_PAGE_SIZE:
            raw = raw[:MAX_PAGE_SIZE]

        title = extract_title(raw)
        text = extract_text(raw)

        if len(text) < MIN_TEXT_LENGTH:
            return None

        return title, text

    except Exception as e:
        print(f"[WARN] {url}: {e}", file=sys.stderr)
        return None


# ================================================================
# Генераторы URL
# ================================================================

def urls_from_file(filepath: str) -> list[str]:
    """Читает URL из файла (по одному на строку)."""
    urls: list[str] = []
    with open(filepath, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                urls.append(line)
    return urls


def urls_wikipedia_random(count: int = 100, lang: str = "ru") -> list[str]:
    """Генерирует URL случайных статей Wikipedia."""
    urls: list[str] = []
    base = f"https://{lang}.wikipedia.org/wiki/Special:Random"
    for _ in range(count):
        urls.append(base)
    return urls


def urls_from_sitemap(sitemap_url: str, max_urls: int = 10000) -> list[str]:
    """Извлекает URL из XML sitemap."""
    try:
        if HAS_REQUESTS:
            resp = requests.get(sitemap_url, timeout=DEFAULT_TIMEOUT)
            raw = resp.text
        else:
            import urllib.request
            with urllib.request.urlopen(sitemap_url, timeout=DEFAULT_TIMEOUT) as resp:
                raw = resp.read().decode("utf-8", errors="replace")

        urls = re.findall(r"<loc>(.*?)</loc>", raw)
        return urls[:max_urls]
    except Exception as e:
        print(f"[WARN] Sitemap {sitemap_url}: {e}", file=sys.stderr)
        return []


# ================================================================
# Извлечение ссылок из HTML
# ================================================================

def extract_links(raw_html: str, base_url: str) -> list[str]:
    """Извлекает и резолвит ссылки из HTML-страницы."""
    links: list[str] = []

    if HAS_BS4:
        soup = BeautifulSoup(raw_html, "html.parser")
        for a_tag in soup.find_all("a", href=True):
            href = a_tag["href"]
            if isinstance(href, list):
                href = href[0]
            links.append(href)
    else:
        # Regex fallback
        for m in re.finditer(r'<a[^>]+href=["\']([^"\']+)["\']', raw_html,
                             re.IGNORECASE):
            links.append(m.group(1))

    # Резолвим относительные URL
    resolved: list[str] = []
    seen: set[str] = set()
    for href in links:
        # Пропускаем мусор
        if href.startswith("javascript:") or href.startswith("mailto:"):
            continue
        if href.startswith("tel:") or href.startswith("#"):
            continue

        full_url = urljoin(base_url, href)

        # Убираем фрагмент
        full_url = full_url.split("#")[0]

        # Только http/https
        if not full_url.startswith(("http://", "https://")):
            continue

        # Пропускаем файлы (картинки, CSS, JS, PDF и т.д.)
        skip_exts = (".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp",
                     ".css", ".js", ".pdf", ".zip", ".mp4", ".mp3",
                     ".ico", ".woff", ".woff2", ".ttf", ".eot")
        if any(full_url.lower().endswith(ext) for ext in skip_exts):
            continue

        if full_url not in seen:
            seen.add(full_url)
            resolved.append(full_url)

    return resolved


# ================================================================
# Краулинг сайта (BFS-обход)
# ================================================================

def crawl_site(
    seed_url: str,
    max_depth: int = 2,
    max_pages: int = 100,
    same_domain: bool = True,
    delay: float = 0.3,
    verbose: bool = False,
) -> list[tuple[str, str, str]]:
    """
    BFS-краулинг сайта.

    Возвращает список (url, title, text) для каждой загруженной страницы.
    """
    from collections import deque

    seed_domain = urlparse(seed_url).netloc.lower()
    results: list[tuple[str, str, str]] = []
    visited: set[str] = {seed_url}
    queue: deque[tuple[str, int]] = deque([(seed_url, 0)])  # (url, depth)

    print(f"[Crawl] Начало: {seed_url} (домен: {seed_domain}, "
          f"глубина: {max_depth}, макс: {max_pages})", file=sys.stderr)

    while queue and len(results) < max_pages:
        url, depth = queue.popleft()

        if verbose:
            print(f"[Crawl] [{len(results)+1}/{max_pages}] d={depth} {url}",
                  file=sys.stderr)

        # Загрузка
        try:
            if HAS_REQUESTS:
                resp = requests.get(
                    url,
                    timeout=DEFAULT_TIMEOUT,
                    headers={"User-Agent": USER_AGENT},
                    allow_redirects=True,
                )
                resp.raise_for_status()
                raw = resp.text
            else:
                import urllib.request
                req = urllib.request.Request(
                    url, headers={"User-Agent": USER_AGENT})
                with urllib.request.urlopen(req, timeout=DEFAULT_TIMEOUT) as r:
                    charset = r.headers.get_content_charset() or "utf-8"
                    raw = r.read().decode(charset, errors="replace")
        except Exception as e:
            if verbose:
                print(f"[Crawl] ✗ {url}: {e}", file=sys.stderr)
            continue

        if len(raw) > MAX_PAGE_SIZE:
            raw = raw[:MAX_PAGE_SIZE]

        title = extract_title(raw)
        text = extract_text(raw)

        if len(text) >= MIN_TEXT_LENGTH:
            results.append((url, title, text))
            if not verbose and len(results) % 10 == 0:
                print(f"[Crawl] {len(results)} страниц загружено...",
                      file=sys.stderr)

        # Извлечение и добавление ссылок
        if depth < max_depth:
            page_links = extract_links(raw, url)
            for link in page_links:
                if link in visited:
                    continue

                # Фильтр по домену
                if same_domain:
                    link_domain = urlparse(link).netloc.lower()
                    if link_domain != seed_domain:
                        continue

                visited.add(link)
                queue.append((link, depth + 1))

        # Задержка
        if delay > 0:
            time.sleep(delay)

    print(f"[Crawl] Завершено: {len(results)} страниц из {seed_url}",
          file=sys.stderr)
    return results


# ================================================================
# Протокол вывода (DOC / END_DOC / DONE)
# ================================================================

def output_document(out: TextIO, title: str, text: str) -> None:
    """Выводит документ в протоколе DOC/END_DOC."""
    # Экранируем маркеры протокола в тексте
    safe_text = text.replace("END_DOC", "END DOC").replace("DONE", "D0NE")
    out.write(f"DOC {title}\n")
    out.write(safe_text)
    out.write("\nEND_DOC\n")
    out.flush()


# ================================================================
# Сохранение текстов на диск
# ================================================================

def save_to_dir(save_dir: str, url: str, title: str, text: str) -> None:
    """Сохраняет текст в файл."""
    os.makedirs(save_dir, exist_ok=True)
    h = hashlib.md5(url.encode()).hexdigest()[:12]
    safe_title = re.sub(r"[^\w\s-]", "", title)[:50].strip()
    filename = f"{h}_{safe_title}.txt"
    filepath = os.path.join(save_dir, filename)
    with open(filepath, "w", encoding="utf-8") as f:
        f.write(f"# {title}\n# URL: {url}\n\n{text}\n")


# ================================================================
# Главный пайплайн
# ================================================================

def run_pipeline(
    urls: list[str],
    model_path: str | None = None,
    save_dir: str | None = None,
    delay: float = DEFAULT_DELAY,
    verbose: bool = False,
    mass_trainer_path: str | None = None,
) -> dict[str, int]:
    """
    Основной пайплайн: загрузка URL → обучение.

    Возвращает статистику: {fetched, failed, trained, skipped}
    """
    stats = {"fetched": 0, "failed": 0, "trained": 0, "skipped": 0}

    # Определяем пайп для обучения
    trainer_proc = None
    out: TextIO = sys.stdout

    if model_path:
        # Ищем бинарник mass_trainer
        if mass_trainer_path is None:
            candidates = [
                "./build/kolibri_mass_trainer",
                "build/kolibri_mass_trainer",
            ]
            for c in candidates:
                if os.path.isfile(c) and os.access(c, os.X_OK):
                    mass_trainer_path = c
                    break

        if mass_trainer_path:
            cmd = [mass_trainer_path, "--model", model_path, "--stdin"]
            if verbose:
                cmd.append("--verbose")
            trainer_proc = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                text=True,
                bufsize=1,
            )
            out = trainer_proc.stdin  # type: ignore
        else:
            print("[WARN] kolibri_mass_trainer не найден, "
                  "вывод в stdout", file=sys.stderr)

    total = len(urls)
    seen: set[str] = set()
    start_time = time.time()

    for i, url in enumerate(urls):
        # Дедупликация (кроме random)
        if "Special:Random" not in url and url in seen:
            stats["skipped"] += 1
            continue
        seen.add(url)

        # Прогресс
        if verbose or (i > 0 and i % 50 == 0):
            elapsed = time.time() - start_time
            rate = stats["fetched"] / max(elapsed, 0.1)
            print(f"[{i+1}/{total}] {rate:.1f} docs/sec  "
                  f"OK={stats['fetched']} FAIL={stats['failed']}  "
                  f"Модель: {stats['trained']} docs",
                  file=sys.stderr)

        # Загрузка
        result = fetch_url(url)
        if result is None:
            stats["failed"] += 1
            continue

        title, text = result
        stats["fetched"] += 1

        # Сохранение на диск
        if save_dir:
            save_to_dir(save_dir, url, title, text)

        # Передача в тренер
        try:
            output_document(out, title, text)
            stats["trained"] += 1
        except BrokenPipeError:
            print("[ERROR] Тренер завершился", file=sys.stderr)
            break

        # Задержка между запросами
        if delay > 0:
            time.sleep(delay)

    # Завершение
    if trainer_proc:
        out.write("DONE\n")
        out.flush()
        out.close()
        trainer_proc.wait()
    elif out is sys.stdout:
        out.write("DONE\n")
        out.flush()

    elapsed = time.time() - start_time
    print(f"\n=== Corpus Fetch Complete ===", file=sys.stderr)
    print(f"Время: {elapsed:.1f} сек", file=sys.stderr)
    print(f"Загружено: {stats['fetched']}", file=sys.stderr)
    print(f"Ошибок:    {stats['failed']}", file=sys.stderr)
    print(f"Обучено:   {stats['trained']}", file=sys.stderr)
    print(f"Пропущено: {stats['skipped']}", file=sys.stderr)

    return stats


# ================================================================
# CLI
# ================================================================

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Kolibri Corpus Fetcher — парсинг сайтов для обучения AI",
    )
    parser.add_argument("--urls", type=str,
                        help="Файл с URL (по одному на строку)")
    parser.add_argument("--url", type=str, action="append",
                        help="Конкретный URL (можно указать несколько)")
    parser.add_argument("--wiki", action="store_true",
                        help="Случайные статьи Wikipedia")
    parser.add_argument("--wiki-lang", type=str, default="ru",
                        help="Язык Wikipedia (по умолчанию: ru)")
    parser.add_argument("--sitemap", type=str,
                        help="URL XML sitemap")
    parser.add_argument("--count", type=int, default=100,
                        help="Количество статей Wikipedia")
    parser.add_argument("--crawl", action="store_true",
                        help="Режим краулинга: следовать по ссылкам с seed URL")
    parser.add_argument("--depth", type=int, default=2,
                        help="Глубина краулинга (по умолчанию: 2)")
    parser.add_argument("--max-pages", type=int, default=100,
                        help="Макс. страниц при краулинге (по умолчанию: 100)")
    parser.add_argument("--same-domain", action="store_true", default=True,
                        help="Только тот же домен при краулинге (по умолчанию)")
    parser.add_argument("--model", type=str,
                        help="Путь к модели (.klm) для прямого обучения")
    parser.add_argument("--save-dir", type=str,
                        help="Сохранить тексты в директорию")
    parser.add_argument("--trainer", type=str,
                        help="Путь к kolibri_mass_trainer")
    parser.add_argument("--delay", type=float, default=DEFAULT_DELAY,
                        help=f"Задержка между запросами (сек, {DEFAULT_DELAY})")
    parser.add_argument("--verbose", action="store_true",
                        help="Подробный вывод")

    args = parser.parse_args()

    # Собираем URL
    all_urls: list[str] = []

    if args.urls:
        all_urls.extend(urls_from_file(args.urls))
    if args.url:
        all_urls.extend(args.url)
    if args.wiki:
        all_urls.extend(urls_wikipedia_random(args.count, args.wiki_lang))
    if args.sitemap:
        all_urls.extend(urls_from_sitemap(args.sitemap))

    if not all_urls:
        parser.error("Укажите источник URL: --urls, --url, --wiki или --sitemap")

    # Режим краулинга
    if args.crawl:
        print(f"[Corpus] Режим краулинга: {len(all_urls)} seed URL, "
              f"глубина {args.depth}, макс {args.max_pages} страниц",
              file=sys.stderr)

        # Проверка зависимостей
        if not HAS_REQUESTS:
            print("[WARN] requests не установлен, используем urllib",
                  file=sys.stderr)
        if not HAS_BS4:
            print("[WARN] beautifulsoup4 не установлен, используем regex",
                  file=sys.stderr)

        # Краулим каждый seed URL
        crawled_pages: list[tuple[str, str, str]] = []
        for seed in all_urls:
            pages = crawl_site(
                seed_url=seed,
                max_depth=args.depth,
                max_pages=args.max_pages,
                same_domain=args.same_domain,
                delay=args.delay,
                verbose=args.verbose,
            )
            crawled_pages.extend(pages)

        # Собираем URL из результатов краулинга
        crawled_urls = [url for url, _, _ in crawled_pages]

        # Если нужно обучить — запускаем pipe
        if args.model:
            mass_trainer_path = args.trainer
            if mass_trainer_path is None:
                for c in ["./build/kolibri_mass_trainer",
                           "build/kolibri_mass_trainer"]:
                    if os.path.isfile(c) and os.access(c, os.X_OK):
                        mass_trainer_path = c
                        break

            if mass_trainer_path:
                cmd = [mass_trainer_path, "--model", args.model, "--stdin"]
                if args.verbose:
                    cmd.append("--verbose")
                proc = subprocess.Popen(
                    cmd, stdin=subprocess.PIPE, text=True, bufsize=1)
                out = proc.stdin
                for url, title, text in crawled_pages:
                    output_document(out, title, text)  # type: ignore
                out.write("DONE\n")  # type: ignore
                out.flush()  # type: ignore
                out.close()  # type: ignore
                proc.wait()
            else:
                print("[WARN] kolibri_mass_trainer не найден",
                      file=sys.stderr)
                for _, title, text in crawled_pages:
                    output_document(sys.stdout, title, text)
                sys.stdout.write("DONE\n")
        elif args.save_dir:
            for url, title, text in crawled_pages:
                save_to_dir(args.save_dir, url, title, text)
        else:
            for _, title, text in crawled_pages:
                output_document(sys.stdout, title, text)
            sys.stdout.write("DONE\n")

        print(f"\n=== Crawl Complete ===", file=sys.stderr)
        print(f"Страниц загружено: {len(crawled_pages)}", file=sys.stderr)
        return

    print(f"[Corpus] {len(all_urls)} URL для обработки", file=sys.stderr)

    # Проверка зависимостей
    if not HAS_REQUESTS:
        print("[WARN] requests не установлен, используем urllib "
              "(медленнее, нет retry)", file=sys.stderr)
    if not HAS_BS4:
        print("[WARN] beautifulsoup4 не установлен, используем regex "
              "(хуже качество парсинга)", file=sys.stderr)

    # Запуск пайплайна
    run_pipeline(
        urls=all_urls,
        model_path=args.model,
        save_dir=args.save_dir,
        delay=args.delay,
        verbose=args.verbose,
        mass_trainer_path=args.trainer,
    )


if __name__ == "__main__":
    main()
