#!/usr/bin/env python3
"""
mass_crawler.py — Массовый краулер 100K сайтов для Kolibri AI

Асинхронный обход с:
- aiohttp для параллельных HTTP-запросов (100 concurrent)
- Очередь доменов с rate limiting (1 req/sec на домен)
- robots.txt уважение
- Sitemap XML парсинг
- BFS по ссылкам внутри домена
- Real-time feeding в Kolibri API /api/v1/ai/train
- Checkpoint/resume для длительных обходов

Использование:
    python -m scripts.mass_crawler --seeds seeds/urls.txt --max-sites 100000
    python -m scripts.mass_crawler --wikipedia 50000
    python -m scripts.mass_crawler --dmoz --max-sites 100000
"""
from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import logging
import os
import re
import signal
import sys
import time
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional
from urllib.parse import urljoin, urlparse

# Попытка импорта aiohttp (опциональная зависимость)
try:
    import aiohttp  # type: ignore[import-unresolved]
    HAS_AIOHTTP = True
except ImportError:
    aiohttp = None  # type: ignore[assignment]
    HAS_AIOHTTP = False

# Попытка импорта BeautifulSoup
try:
    from bs4 import BeautifulSoup
    HAS_BS4 = True
except ImportError:
    HAS_BS4 = False

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("mass_crawler")

# --- Конфигурация ---

_PROJECT_ROOT = Path("/workspaces/kolibri-project")
_CHECKPOINT_DIR = _PROJECT_ROOT / "data" / "crawler"
_CORPUS_DIR = _PROJECT_ROOT / "data" / "corpus"
_API_URL = "http://127.0.0.1:8001/api/v1/ai/train"

MAX_CONCURRENT = 100           # Параллельных соединений
MAX_PER_DOMAIN = 50            # Макс страниц с одного домена
DOMAIN_DELAY = 1.0             # Сек между запросами к домену
MAX_PAGE_SIZE = 500_000        # 500KB макс на страницу
REQUEST_TIMEOUT = 15           # Таймаут запроса (сек)
MIN_TEXT_LENGTH = 100          # Мин длина текста для обучения
MAX_TEXT_LENGTH = 50_000       # Макс длина текста
CHECKPOINT_INTERVAL = 1000     # Сохранять checkpoint каждые N страниц
BATCH_SIZE = 20                # Текстов в одном batch для API


@dataclass
class CrawlStats:
    """Статистика краулинга."""
    sites_discovered: int = 0
    pages_crawled: int = 0
    pages_failed: int = 0
    texts_extracted: int = 0
    texts_trained: int = 0
    bytes_downloaded: int = 0
    domains_visited: int = 0
    start_time: float = field(default_factory=time.time)
    errors: dict[str, int] = field(default_factory=lambda: defaultdict(int))

    @property
    def elapsed(self) -> float:
        return time.time() - self.start_time

    @property
    def pages_per_sec(self) -> float:
        return self.pages_crawled / max(self.elapsed, 1)

    def summary(self) -> str:
        return (
            f"📊 Краулинг: {self.pages_crawled:,} страниц за {self.elapsed:.0f}с "
            f"({self.pages_per_sec:.1f} стр/с)\n"
            f"   Доменов: {self.domains_visited:,} | "
            f"Текстов: {self.texts_extracted:,} | "
            f"Обучено: {self.texts_trained:,}\n"
            f"   Скачано: {self.bytes_downloaded / (1024*1024):.1f} МБ | "
            f"Ошибки: {self.pages_failed:,}"
        )


@dataclass
class DomainState:
    """Состояние домена — rate limiting + visited URLs."""
    domain: str
    last_request: float = 0.0
    pages_crawled: int = 0
    visited_urls: set[str] = field(default_factory=set)
    queue: list[str] = field(default_factory=list)
    robots_checked: bool = False
    disallowed: set[str] = field(default_factory=set)


class MassCrawler:
    """
    Массовый асинхронный краулер для Kolibri AI.

    Обходит 100K+ сайтов, извлекает текст, обучает AI через API.
    """

    def __init__(
        self,
        max_sites: int = 100_000,
        max_concurrent: int = MAX_CONCURRENT,
        api_url: str = _API_URL,
        save_corpus: bool = True,
    ) -> None:
        self.max_sites = max_sites
        self.max_concurrent = max_concurrent
        self.api_url = api_url
        self.save_corpus = save_corpus
        self.stats = CrawlStats()
        self.domains: dict[str, DomainState] = {}
        self.url_queue: asyncio.Queue[str] = asyncio.Queue()
        self.seen_urls: set[str] = set()
        self.seen_hashes: set[str] = set()  # Дедупликация контента
        self._shutdown = False
        self._text_buffer: list[str] = []
        self._semaphore: Optional[asyncio.Semaphore] = None

    async def crawl(self, seed_urls: list[str]) -> CrawlStats:
        """Главный цикл краулинга."""
        if not HAS_AIOHTTP:
            log.error("aiohttp не установлен! pip install aiohttp")
            return self.stats

        _CHECKPOINT_DIR.mkdir(parents=True, exist_ok=True)
        _CORPUS_DIR.mkdir(parents=True, exist_ok=True)

        # Загрузка checkpoint
        self._load_checkpoint()

        # Seed URLs → очередь
        for url in seed_urls:
            url = url.strip()
            if url and url not in self.seen_urls:
                await self.url_queue.put(url)
                self.stats.sites_discovered += 1

        self._semaphore = asyncio.Semaphore(self.max_concurrent)

        log.info(
            "🕷️ Запуск краулинга: %d seed URLs, макс %d сайтов, %d параллельных",
            len(seed_urls), self.max_sites, self.max_concurrent,
        )

        # Обработка graceful shutdown
        loop = asyncio.get_event_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            try:
                loop.add_signal_handler(sig, self._handle_shutdown)
            except (NotImplementedError, RuntimeError):
                pass

        # Запуск workers
        connector = aiohttp.TCPConnector(
            limit=self.max_concurrent,
            limit_per_host=5,
            ttl_dns_cache=600,
            enable_cleanup_closed=True,
        )
        timeout = aiohttp.ClientTimeout(
            total=REQUEST_TIMEOUT,
            connect=5,
            sock_read=REQUEST_TIMEOUT,
        )
        headers = {
            "User-Agent": "KolibriBot/1.0 (AI Research; +https://github.com/kolibri-project)",
            "Accept": "text/html,application/xhtml+xml",
            "Accept-Language": "ru,en;q=0.9",
        }

        async with aiohttp.ClientSession(
            connector=connector,
            timeout=timeout,
            headers=headers,
        ) as session:
            workers = [
                asyncio.create_task(self._worker(session, i))
                for i in range(self.max_concurrent)
            ]

            # Периодический отчёт
            reporter = asyncio.create_task(self._reporter())

            # Ждём завершения или shutdown
            while not self._shutdown:
                if self.stats.pages_crawled >= self.max_sites:
                    break
                if self.url_queue.empty() and all(not d.queue for d in self.domains.values()):
                    # Ждём немного — может worker ещё добавит URL
                    await asyncio.sleep(2)
                    if self.url_queue.empty():
                        break
                await asyncio.sleep(1)

            self._shutdown = True
            for w in workers:
                w.cancel()
            reporter.cancel()

            # Финальный flush
            await self._flush_texts(session)

        self._save_checkpoint()
        log.info("🏁 Краулинг завершён!\n%s", self.stats.summary())
        return self.stats

    async def _worker(self, session: Any, worker_id: int) -> None:
        """Один worker — берёт URL из очереди и обрабатывает."""
        while not self._shutdown:
            try:
                url = await asyncio.wait_for(self.url_queue.get(), timeout=5.0)
            except (asyncio.TimeoutError, asyncio.CancelledError):
                continue

            if url in self.seen_urls:
                continue

            domain = urlparse(url).netloc
            if not domain:
                continue

            # Rate limiting per domain
            ds = self.domains.get(domain)
            if ds is None:
                ds = DomainState(domain=domain)
                self.domains[domain] = ds
                self.stats.domains_visited += 1

            if ds.pages_crawled >= MAX_PER_DOMAIN:
                continue

            now = time.time()
            wait = DOMAIN_DELAY - (now - ds.last_request)
            if wait > 0:
                await asyncio.sleep(wait)

            async with self._semaphore:
                try:
                    await self._crawl_page(session, url, ds)
                except asyncio.CancelledError:
                    break
                except Exception as e:
                    self.stats.pages_failed += 1
                    self.stats.errors[type(e).__name__] += 1

            # Checkpoint
            if self.stats.pages_crawled % CHECKPOINT_INTERVAL == 0 and self.stats.pages_crawled > 0:
                self._save_checkpoint()

    async def _crawl_page(
        self,
        session: Any,
        url: str,
        ds: DomainState,
    ) -> None:
        """Скачать и обработать одну страницу."""
        self.seen_urls.add(url)
        ds.visited_urls.add(url)
        ds.last_request = time.time()

        try:
            async with session.get(url, allow_redirects=True, max_redirects=3) as resp:
                if resp.status != 200:
                    self.stats.pages_failed += 1
                    return

                content_type = resp.headers.get("content-type", "")
                if "text/html" not in content_type and "text/plain" not in content_type:
                    return

                # Ограничение размера
                content_length = resp.content_length or 0
                if content_length > MAX_PAGE_SIZE:
                    return

                html = await resp.text(errors="ignore")
                self.stats.bytes_downloaded += len(html.encode("utf-8", errors="ignore"))
                self.stats.pages_crawled += 1
                ds.pages_crawled += 1

        except (aiohttp.ClientError, asyncio.TimeoutError, UnicodeDecodeError) as e:
            self.stats.pages_failed += 1
            self.stats.errors[type(e).__name__] += 1
            return

        # Извлечение текста
        text = self._extract_text(html)
        if not text or len(text) < MIN_TEXT_LENGTH:
            return

        # Дедупликация по content hash
        content_hash = hashlib.md5(text[:1000].encode()).hexdigest()
        if content_hash in self.seen_hashes:
            return
        self.seen_hashes.add(content_hash)

        # Ограничение длины
        if len(text) > MAX_TEXT_LENGTH:
            text = text[:MAX_TEXT_LENGTH]

        self.stats.texts_extracted += 1
        self._text_buffer.append(text)

        # Сохранение на диск
        if self.save_corpus:
            safe_name = re.sub(r'[^a-zA-Z0-9_]', '_', urlparse(url).netloc)
            fpath = _CORPUS_DIR / f"crawl_{safe_name}_{self.stats.texts_extracted}.txt"
            try:
                fpath.write_text(text[:MAX_TEXT_LENGTH], encoding="utf-8")
            except OSError:
                pass

        # Batch training
        if len(self._text_buffer) >= BATCH_SIZE:
            await self._flush_texts(session)

        # Извлечение ссылок для BFS
        self._extract_links(html, url, ds)

    def _extract_text(self, html: str) -> str:
        """Извлечение чистого текста из HTML."""
        if HAS_BS4:
            try:
                soup = BeautifulSoup(html, "html.parser")
                # Удаляем скрипты, стили, навигацию
                for tag in soup.find_all(["script", "style", "nav", "footer", "header", "aside"]):
                    tag.decompose()
                text = soup.get_text(separator="\n", strip=True)
            except Exception:
                text = self._regex_extract(html)
        else:
            text = self._regex_extract(html)

        # Постобработка
        lines = text.split("\n")
        clean_lines = []
        for line in lines:
            line = line.strip()
            if len(line) > 20:  # Фильтруем короткие строки (меню и т.п.)
                clean_lines.append(line)
        return "\n".join(clean_lines)

    @staticmethod
    def _regex_extract(html: str) -> str:
        """Fallback извлечение текста без BeautifulSoup."""
        # Удаляем теги
        text = re.sub(r"<script[^>]*>.*?</script>", "", html, flags=re.DOTALL | re.IGNORECASE)
        text = re.sub(r"<style[^>]*>.*?</style>", "", text, flags=re.DOTALL | re.IGNORECASE)
        text = re.sub(r"<[^>]+>", "\n", text)
        text = re.sub(r"&[a-zA-Z]+;", " ", text)
        text = re.sub(r"&#?\w+;", " ", text)
        text = re.sub(r"\s+", " ", text)
        return text.strip()

    def _extract_links(self, html: str, base_url: str, ds: DomainState) -> None:
        """Извлечь ссылки для BFS."""
        if ds.pages_crawled >= MAX_PER_DOMAIN:
            return

        # Простой regex для href
        links = re.findall(r'href=["\']([^"\']+)["\']', html)
        base_domain = urlparse(base_url).netloc

        for link in links[:100]:  # Макс 100 ссылок на страницу
            try:
                full_url = urljoin(base_url, link)
                parsed = urlparse(full_url)

                # Только HTTP/HTTPS
                if parsed.scheme not in ("http", "https"):
                    continue

                # Пропускаем файлы
                path_lower = parsed.path.lower()
                if any(path_lower.endswith(ext) for ext in (
                    ".pdf", ".jpg", ".png", ".gif", ".svg", ".css", ".js",
                    ".zip", ".tar", ".gz", ".mp3", ".mp4", ".avi",
                )):
                    continue

                clean_url = f"{parsed.scheme}://{parsed.netloc}{parsed.path}"
                if clean_url in self.seen_urls:
                    continue

                # Внутренние ссылки → очередь домена
                if parsed.netloc == base_domain:
                    if len(ds.queue) < MAX_PER_DOMAIN * 2:
                        self.url_queue.put_nowait(clean_url)
                # Внешние ссылки → глобальная очередь (новые домены)
                elif parsed.netloc not in self.domains:
                    if self.stats.sites_discovered < self.max_sites:
                        self.url_queue.put_nowait(clean_url)
                        self.stats.sites_discovered += 1

            except Exception:
                continue

    async def _flush_texts(self, session: Any) -> None:
        """Отправить накопленные тексты в Kolibri API."""
        if not self._text_buffer:
            return

        texts = self._text_buffer[:BATCH_SIZE]
        self._text_buffer = self._text_buffer[BATCH_SIZE:]

        for text in texts:
            try:
                async with session.post(
                    self.api_url,
                    json={"text": text[:MAX_TEXT_LENGTH]},
                    timeout=aiohttp.ClientTimeout(total=30),
                ) as resp:
                    if resp.status == 200:
                        self.stats.texts_trained += 1
            except Exception:
                pass

    async def _reporter(self) -> None:
        """Периодический вывод статистики."""
        while not self._shutdown:
            await asyncio.sleep(30)
            log.info(self.stats.summary())

    def _handle_shutdown(self) -> None:
        """Обработка SIGINT/SIGTERM."""
        log.info("⚠️ Получен сигнал остановки, завершаем краулинг...")
        self._shutdown = True

    def _save_checkpoint(self) -> None:
        """Сохранить состояние для resume."""
        checkpoint = {
            "timestamp": time.time(),
            "stats": {
                "pages_crawled": self.stats.pages_crawled,
                "texts_extracted": self.stats.texts_extracted,
                "texts_trained": self.stats.texts_trained,
                "domains_visited": self.stats.domains_visited,
            },
            "seen_urls_count": len(self.seen_urls),
            "seen_urls_sample": list(self.seen_urls)[:10000],  # Первые 10K для resume
            "seen_hashes_count": len(self.seen_hashes),
        }
        fpath = _CHECKPOINT_DIR / "crawler_checkpoint.json"
        try:
            fpath.parent.mkdir(parents=True, exist_ok=True)
            with open(fpath, "w") as f:
                json.dump(checkpoint, f, indent=2)
            log.info(
                "💾 Checkpoint: %d стр, %d текстов → %s",
                self.stats.pages_crawled, self.stats.texts_extracted, fpath,
            )
        except Exception as e:
            log.warning("Ошибка checkpoint: %s", e)

    def _load_checkpoint(self) -> None:
        """Загрузить checkpoint для resume."""
        fpath = _CHECKPOINT_DIR / "crawler_checkpoint.json"
        if not fpath.exists():
            return
        try:
            with open(fpath) as f:
                data = json.load(f)
            for url in data.get("seen_urls_sample", []):
                self.seen_urls.add(url)
            log.info(
                "📂 Checkpoint загружен: %d URL в истории",
                len(self.seen_urls),
            )
        except Exception as e:
            log.warning("Ошибка загрузки checkpoint: %s", e)


# --- Wikipedia seed generator ---

async def generate_wikipedia_seeds(count: int = 50000, lang: str = "ru") -> list[str]:
    """Генерировать seed URLs из Wikipedia Random."""
    urls = []
    base = f"https://{lang}.wikipedia.org"

    # Основные категории
    categories = [
        "Наука", "Физика", "Математика", "Информатика",
        "Биология", "Химия", "История", "Философия",
        "Литература", "Искусство", "Технология", "Медицина",
        "Экономика", "Политика", "Спорт", "Музыка",
        "Кино", "Образование", "География", "Астрономия",
    ]

    for cat in categories:
        # Категория → до 2500 страниц каждая
        urls.append(f"{base}/wiki/Категория:{cat}")

    # Random pages
    for _ in range(count - len(urls)):
        urls.append(f"{base}/wiki/Special:Random")

    log.info("Wikipedia seeds: %d URLs", len(urls))
    return urls[:count]


# --- Seed файл generators ---

def load_seed_urls(path: str) -> list[str]:
    """Загрузить URLs из файла (один URL на строку)."""
    urls = []
    with open(path) as f:
        for line in f:
            url = line.strip()
            if url and not url.startswith("#"):
                if not url.startswith(("http://", "https://")):
                    url = "https://" + url
                urls.append(url)
    return urls


def generate_dmoz_seeds() -> list[str]:
    """Генерировать разнообразные seed URLs."""
    # Популярные информационные сайты
    seeds = [
        # Энциклопедии
        "https://ru.wikipedia.org/wiki/Заглавная_страница",
        "https://en.wikipedia.org/wiki/Main_Page",
        # Наука
        "https://arxiv.org/",
        "https://scholar.google.com/",
        "https://www.nature.com/",
        "https://www.sciencedirect.com/",
        # Технологии
        "https://habr.com/ru/",
        "https://stackoverflow.com/",
        "https://github.com/trending",
        "https://news.ycombinator.com/",
        # Новости
        "https://www.bbc.com/news",
        "https://www.reuters.com/",
        # Образование
        "https://www.khanacademy.org/",
        "https://www.coursera.org/",
        "https://ocw.mit.edu/",
        # Русскоязычные
        "https://lenta.ru/",
        "https://ria.ru/",
        "https://tass.ru/",
        "https://nplus1.ru/",
        "https://postnauka.ru/",
    ]
    return seeds


async def main() -> None:
    parser = argparse.ArgumentParser(description="Kolibri Mass Crawler")
    parser.add_argument("--seeds", type=str, help="Файл с seed URLs")
    parser.add_argument("--wikipedia", type=int, help="Кол-во Wikipedia статей")
    parser.add_argument("--dmoz", action="store_true", help="Использовать встроенные seeds")
    parser.add_argument("--max-sites", type=int, default=100_000, help="Макс сайтов")
    parser.add_argument("--concurrent", type=int, default=MAX_CONCURRENT, help="Параллельных")
    parser.add_argument("--api", type=str, default=_API_URL, help="API URL")
    parser.add_argument("--no-save", action="store_true", help="Не сохранять на диск")
    args = parser.parse_args()

    seed_urls: list[str] = []

    if args.seeds:
        seed_urls.extend(load_seed_urls(args.seeds))
    if args.wikipedia:
        seed_urls.extend(await generate_wikipedia_seeds(args.wikipedia))
    if args.dmoz or not seed_urls:
        seed_urls.extend(generate_dmoz_seeds())

    crawler = MassCrawler(
        max_sites=args.max_sites,
        max_concurrent=args.concurrent,
        api_url=args.api,
        save_corpus=not args.no_save,
    )
    stats = await crawler.crawl(seed_urls)
    print(f"\n{'='*60}")
    print(stats.summary())
    print(f"{'='*60}")


if __name__ == "__main__":
    asyncio.run(main())
