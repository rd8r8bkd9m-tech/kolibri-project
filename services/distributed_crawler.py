"""
Распределённый краулер Kolibri — очередь URL + пул воркеров.

Архитектура:
  - URLQueue: приоритетная очередь с дедупликацией
  - CrawlWorker: асинхронные воркеры
  - CrawlCoordinator: координатор, распределяет URL по воркерам
  - DomainClassifier: классификация контента по доменам знаний
"""
from __future__ import annotations

import asyncio
import hashlib
import re
import time
from collections import defaultdict
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Optional
from urllib.parse import urljoin, urlparse

import aiohttp
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

router = APIRouter(prefix="/api/v1/distributed", tags=["crawler"])


# ============================================================
# Домены знаний (соответствуют KolibriDomainType из C)
# ============================================================

class KnowledgeDomain(IntEnum):
    GENERAL = 0
    MEDICINE = 1
    IT = 2
    PHYSICS = 3
    MATH = 4
    CHEMISTRY = 5
    BIOLOGY = 6
    HISTORY = 7
    LAW = 8
    ECONOMICS = 9
    CUSTOM = 255


# Ключевые слова для автоматической классификации домена
DOMAIN_KEYWORDS: dict[KnowledgeDomain, frozenset[str]] = {
    KnowledgeDomain.MEDICINE: frozenset({
        "медицин", "лечен", "диагноз", "болезн", "терап", "фармакол",
        "хирург", "анатом", "патолог", "clinical", "medical", "pharma",
        "health", "disease", "therapy", "diagnosis", "symptom",
    }),
    KnowledgeDomain.IT: frozenset({
        "програм", "алгоритм", "код", "сервер", "базы данных", "api",
        "python", "javascript", "machine learning", "neural", "software",
        "devops", "kubernetes", "docker", "linux", "database", "cloud",
    }),
    KnowledgeDomain.PHYSICS: frozenset({
        "физик", "квант", "термодинам", "электромагн", "гравитац",
        "ядерн", "оптик", "mechanics", "quantum", "relativity",
        "thermodynamics", "electro", "particle", "photon",
    }),
    KnowledgeDomain.MATH: frozenset({
        "математик", "теорем", "доказатель", "интеграл", "уравнен",
        "алгебр", "геометр", "topology", "calculus", "theorem",
        "equation", "proof", "abstract algebra", "statistics",
    }),
    KnowledgeDomain.CHEMISTRY: frozenset({
        "химии", "химич", "молекул", "реакц", "элемент", "формул",
        "органич", "неорганич", "chemistry", "molecule", "compound",
        "catalyst", "polymer", "synthesis", "periodic table",
    }),
    KnowledgeDomain.BIOLOGY: frozenset({
        "биолог", "клетк", "генетик", "эволюц", "экологи", "микробио",
        "ботаник", "зоолог", "biology", "cell", "genome", "evolution",
        "ecosystem", "species", "protein", "dna", "rna",
    }),
    KnowledgeDomain.HISTORY: frozenset({
        "истори", "цивилизац", "империя", "революц", "войн",
        "dynasty", "civilization", "ancient", "medieval", "century",
        "war", "empire", "revolution", "archaeological",
    }),
    KnowledgeDomain.LAW: frozenset({
        "право", "юрид", "закон", "конституц", "судеб", "кодекс",
        "law", "legal", "court", "legislation", "constitution",
        "regulation", "judicial", "criminal", "civil code",
    }),
    KnowledgeDomain.ECONOMICS: frozenset({
        "эконом", "финанс", "рынок", "инвестиц", "валют", "бюджет",
        "economics", "market", "investment", "gdp", "inflation",
        "fiscal", "monetary", "trade", "banking",
    }),
}


# ============================================================
# Модели данных
# ============================================================

@dataclass(order=True)
class CrawlTask:
    """Задача краулинга с приоритетом."""
    priority: int                          # меньше = выше приоритет
    url: str = field(compare=False)
    depth: int = field(compare=False, default=0)
    parent_url: str = field(compare=False, default="")
    domain_hint: KnowledgeDomain = field(compare=False, default=KnowledgeDomain.GENERAL)
    created_at: float = field(compare=False, default_factory=time.monotonic)


@dataclass
class CrawlResult:
    """Результат краулинга одной страницы."""
    url: str
    status: int
    title: str = ""
    text: str = ""
    links: list[str] = field(default_factory=list)
    domain: KnowledgeDomain = KnowledgeDomain.GENERAL
    text_length: int = 0
    crawl_time_ms: float = 0.0
    error: str = ""


# ============================================================
# URLQueue — приоритетная очередь с дедупликацией
# ============================================================

class URLQueue:
    """Потокобезопасная приоритетная очередь URL с дедупликацией."""

    def __init__(self, max_size: int = 0) -> None:
        self._queue: asyncio.PriorityQueue[CrawlTask] = asyncio.PriorityQueue(
            maxsize=max_size,
        )
        self._seen: set[str] = set()
        self._lock = asyncio.Lock()
        self._total_added: int = 0
        self._total_deduped: int = 0

    def _normalize_url(self, url: str) -> str:
        """Нормализация URL для дедупликации."""
        parsed = urlparse(url)
        # Убираем фрагменты и trailing слэши
        normalized = f"{parsed.scheme}://{parsed.netloc}{parsed.path.rstrip('/')}"
        if parsed.query:
            normalized += f"?{parsed.query}"
        return normalized.lower()

    async def put(self, task: CrawlTask) -> bool:
        """Добавить задачу. Возвращает False если дубликат."""
        normalized = self._normalize_url(task.url)
        async with self._lock:
            if normalized in self._seen:
                self._total_deduped += 1
                return False
            self._seen.add(normalized)
            self._total_added += 1

        await self._queue.put(task)
        return True

    async def get(self) -> CrawlTask:
        """Получить следующую задачу (блокирующий)."""
        return await self._queue.get()

    def task_done(self) -> None:
        self._queue.task_done()

    @property
    def qsize(self) -> int:
        return self._queue.qsize()

    @property
    def total_seen(self) -> int:
        return len(self._seen)

    @property
    def stats(self) -> dict:
        return {
            "queue_size": self._queue.qsize(),
            "total_seen": len(self._seen),
            "total_added": self._total_added,
            "total_deduped": self._total_deduped,
        }


# ============================================================
# DomainClassifier — автоклассификация контента
# ============================================================

class DomainClassifier:
    """Классификатор контента по доменам знаний."""

    @staticmethod
    def classify(text: str, url: str = "") -> KnowledgeDomain:
        """Определить домен знаний по тексту и URL."""
        text_lower = text.lower()[:5000]  # проверяем начало
        url_lower = url.lower()

        scores: dict[KnowledgeDomain, int] = defaultdict(int)

        for domain, keywords in DOMAIN_KEYWORDS.items():
            for kw in keywords:
                # Считаем вхождения ключевых слов
                count = text_lower.count(kw)
                if count > 0:
                    scores[domain] += count
                # Бонус за присутствие в URL
                if kw in url_lower:
                    scores[domain] += 5

        if not scores:
            return KnowledgeDomain.GENERAL

        best_domain = max(scores, key=lambda d: scores[d])
        # Порог уверенности: минимум 3 совпадения
        if scores[best_domain] < 3:
            return KnowledgeDomain.GENERAL

        return best_domain


# ============================================================
# CrawlWorker — асинхронный воркер
# ============================================================

class CrawlWorker:
    """Асинхронный воркер краулинга."""

    def __init__(
        self,
        worker_id: int,
        queue: URLQueue,
        results: list[CrawlResult],
        max_depth: int = 3,
        delay: float = 0.3,
        timeout: float = 10.0,
    ) -> None:
        self.worker_id = worker_id
        self.queue = queue
        self.results = results
        self.max_depth = max_depth
        self.delay = delay
        self.timeout = timeout
        self._running = False
        self._pages_crawled = 0
        self._classifier = DomainClassifier()

    async def run(self, session: aiohttp.ClientSession) -> None:
        """Основной цикл воркера."""
        self._running = True
        while self._running:
            try:
                task = await asyncio.wait_for(self.queue.get(), timeout=5.0)
            except asyncio.TimeoutError:
                break

            try:
                result = await self._crawl_page(session, task)
                self.results.append(result)
                self._pages_crawled += 1

                # Добавляем найденные ссылки в очередь
                if task.depth < self.max_depth and not result.error:
                    for link in result.links[:50]:  # до 50 ссылок со страницы
                        child_task = CrawlTask(
                            priority=task.depth + 1,
                            url=link,
                            depth=task.depth + 1,
                            parent_url=task.url,
                            domain_hint=result.domain,
                        )
                        await self.queue.put(child_task)

                # Задержка между запросами к одному домену
                if self.delay > 0:
                    await asyncio.sleep(self.delay)
            except Exception:
                pass
            finally:
                self.queue.task_done()

    async def _crawl_page(
        self, session: aiohttp.ClientSession, task: CrawlTask,
    ) -> CrawlResult:
        """Краулинг одной страницы."""
        t0 = time.monotonic()
        try:
            async with session.get(
                task.url,
                timeout=aiohttp.ClientTimeout(total=self.timeout),
                headers={"User-Agent": "KolibriBot/2.0 (+kolibri-os.dev)"},
            ) as resp:
                if resp.status != 200:
                    return CrawlResult(
                        url=task.url,
                        status=resp.status,
                        error=f"HTTP {resp.status}",
                        crawl_time_ms=(time.monotonic() - t0) * 1000,
                    )

                content_type = resp.headers.get("Content-Type", "")
                if "text/html" not in content_type and "text/plain" not in content_type:
                    return CrawlResult(
                        url=task.url,
                        status=resp.status,
                        error=f"Unsupported content-type: {content_type}",
                        crawl_time_ms=(time.monotonic() - t0) * 1000,
                    )

                html = await resp.text(errors="replace")

        except Exception as e:
            return CrawlResult(
                url=task.url,
                status=0,
                error=str(e),
                crawl_time_ms=(time.monotonic() - t0) * 1000,
            )

        # Извлекаем текст и ссылки
        title = self._extract_title(html)
        text = self._extract_text(html)
        links = self._extract_links(html, task.url)
        domain = self._classifier.classify(text, task.url)

        return CrawlResult(
            url=task.url,
            status=200,
            title=title,
            text=text,
            links=links,
            domain=domain,
            text_length=len(text),
            crawl_time_ms=(time.monotonic() - t0) * 1000,
        )

    @staticmethod
    def _extract_title(html: str) -> str:
        m = re.search(r"<title[^>]*>(.*?)</title>", html, re.IGNORECASE | re.DOTALL)
        return m.group(1).strip()[:256] if m else ""

    @staticmethod
    def _extract_text(html: str) -> str:
        """Извлечь чистый текст из HTML."""
        # Убираем script и style
        text = re.sub(r"<(script|style)[^>]*>.*?</\1>", "", html, flags=re.DOTALL | re.IGNORECASE)
        # Убираем теги
        text = re.sub(r"<[^>]+>", " ", text)
        # Нормализация пробелов
        text = re.sub(r"\s+", " ", text).strip()
        # Декодируем HTML-сущности
        text = text.replace("&nbsp;", " ").replace("&amp;", "&")
        text = text.replace("&lt;", "<").replace("&gt;", ">")
        return text[:100000]  # Ограничиваем 100KB текста

    @staticmethod
    def _extract_links(html: str, base_url: str) -> list[str]:
        """Извлечь абсолютные ссылки из HTML."""
        links: list[str] = []
        seen: set[str] = set()
        for m in re.finditer(r'href=["\']([^"\']+)["\']', html, re.IGNORECASE):
            href = m.group(1).strip()
            if href.startswith(("#", "javascript:", "mailto:", "tel:")):
                continue
            absolute = urljoin(base_url, href)
            if absolute not in seen:
                seen.add(absolute)
                links.append(absolute)
        return links

    def stop(self) -> None:
        self._running = False


# ============================================================
# CrawlCoordinator — координатор краулинга
# ============================================================

class CrawlCoordinator:
    """Координатор распределённого краулинга."""

    def __init__(
        self,
        num_workers: int = 8,
        max_pages: int = 10000,
        max_depth: int = 3,
        delay: float = 0.3,
    ) -> None:
        self.num_workers = num_workers
        self.max_pages = max_pages
        self.max_depth = max_depth
        self.delay = delay
        self.queue = URLQueue()
        self.results: list[CrawlResult] = []
        self._workers: list[CrawlWorker] = []
        self._running = False
        self._start_time: float = 0.0
        self._domain_stats: dict[KnowledgeDomain, int] = defaultdict(int)

    async def crawl(self, seed_urls: list[str]) -> dict:
        """Запустить распределённый краулинг."""
        self._running = True
        self._start_time = time.monotonic()
        self.results.clear()

        # Добавляем seed URL
        for i, url in enumerate(seed_urls):
            await self.queue.put(CrawlTask(
                priority=0,
                url=url,
                depth=0,
            ))

        # Запускаем пул воркеров
        connector = aiohttp.TCPConnector(limit=self.num_workers * 2, ttl_dns_cache=300)
        async with aiohttp.ClientSession(connector=connector) as session:
            tasks: list[asyncio.Task] = []  # type: ignore[type-arg]
            for i in range(self.num_workers):
                worker = CrawlWorker(
                    worker_id=i,
                    queue=self.queue,
                    results=self.results,
                    max_depth=self.max_depth,
                    delay=self.delay,
                )
                self._workers.append(worker)
                tasks.append(asyncio.create_task(worker.run(session)))

            # Ждём завершения или лимита страниц
            while self._running and len(self.results) < self.max_pages:
                await asyncio.sleep(0.5)
                if self.queue.qsize == 0 and all(t.done() for t in tasks):
                    break

            # Останавливаем воркеров
            for w in self._workers:
                w.stop()

            # Ждём завершения
            await asyncio.gather(*tasks, return_exceptions=True)

        # Собираем статистику по доменам
        self._domain_stats.clear()
        for r in self.results:
            if not r.error:
                self._domain_stats[r.domain] += 1

        elapsed = time.monotonic() - self._start_time
        return {
            "pages_crawled": len(self.results),
            "pages_ok": sum(1 for r in self.results if not r.error),
            "pages_failed": sum(1 for r in self.results if r.error),
            "total_text_bytes": sum(r.text_length for r in self.results),
            "time_sec": round(elapsed, 2),
            "pages_per_sec": round(len(self.results) / max(elapsed, 0.001), 1),
            "queue_stats": self.queue.stats,
            "domain_stats": {d.name: c for d, c in self._domain_stats.items()},
            "workers": self.num_workers,
        }

    def stop(self) -> None:
        self._running = False

    @property
    def status(self) -> dict:
        elapsed = time.monotonic() - self._start_time if self._start_time else 0
        return {
            "running": self._running,
            "pages_crawled": len(self.results),
            "queue_size": self.queue.qsize,
            "urls_seen": self.queue.total_seen,
            "elapsed_sec": round(elapsed, 1),
        }


# ============================================================
# Глобальный координатор
# ============================================================

_coordinator: Optional[CrawlCoordinator] = None
_crawl_task: Optional[asyncio.Task] = None  # type: ignore[type-arg]


# ============================================================
# Pydantic модели API
# ============================================================

class DistributedCrawlRequest(BaseModel):
    seed_urls: list[str] = Field(description="Стартовые URL для краулинга")
    num_workers: int = Field(default=8, ge=1, le=64, description="Количество воркеров")
    max_pages: int = Field(default=10000, ge=1, description="Макс. страниц (без лимита сверху)")
    max_depth: int = Field(default=3, ge=0, le=10, description="Макс. глубина")
    delay: float = Field(default=0.3, ge=0.0, le=5.0, description="Задержка между запросами (сек)")


class DistributedCrawlResponse(BaseModel):
    status: str
    message: str
    initial_urls: int = 0


class CrawlStatusResponse(BaseModel):
    running: bool
    pages_crawled: int = 0
    queue_size: int = 0
    urls_seen: int = 0
    elapsed_sec: float = 0.0
    domain_stats: dict[str, int] = {}


class CrawlResultsResponse(BaseModel):
    status: str
    total_pages: int = 0
    total_text_bytes: int = 0
    time_sec: float = 0.0
    pages_per_sec: float = 0.0
    domain_stats: dict[str, int] = {}
    queue_stats: dict = {}


# ============================================================
# API эндпоинты
# ============================================================

@router.post("/crawl/start", response_model=DistributedCrawlResponse)
async def start_distributed_crawl(req: DistributedCrawlRequest) -> DistributedCrawlResponse:
    """Запустить распределённый краулинг с пулом воркеров."""
    global _coordinator, _crawl_task

    if _coordinator and _coordinator._running:
        raise HTTPException(409, "Краулинг уже запущен. Остановите текущий.")

    _coordinator = CrawlCoordinator(
        num_workers=req.num_workers,
        max_pages=req.max_pages,
        max_depth=req.max_depth,
        delay=req.delay,
    )

    # Запускаем в фоне
    _crawl_task = asyncio.create_task(_coordinator.crawl(req.seed_urls))

    return DistributedCrawlResponse(
        status="started",
        message=f"Краулинг запущен: {req.num_workers} воркеров, "
                f"макс. {req.max_pages} страниц",
        initial_urls=len(req.seed_urls),
    )


@router.post("/crawl/stop")
async def stop_distributed_crawl() -> dict:
    """Остановить краулинг."""
    global _coordinator
    if _coordinator:
        _coordinator.stop()
        return {"status": "stopping"}
    return {"status": "not_running"}


@router.get("/crawl/status", response_model=CrawlStatusResponse)
async def distributed_crawl_status() -> CrawlStatusResponse:
    """Статус текущего краулинга."""
    if not _coordinator:
        return CrawlStatusResponse(running=False)

    st = _coordinator.status
    domain_stats = {}
    if _coordinator.results:
        for r in _coordinator.results:
            if not r.error:
                name = r.domain.name
                domain_stats[name] = domain_stats.get(name, 0) + 1

    return CrawlStatusResponse(
        running=st["running"],
        pages_crawled=st["pages_crawled"],
        queue_size=st["queue_size"],
        urls_seen=st["urls_seen"],
        elapsed_sec=st["elapsed_sec"],
        domain_stats=domain_stats,
    )


@router.get("/crawl/results", response_model=CrawlResultsResponse)
async def distributed_crawl_results() -> CrawlResultsResponse:
    """Финальные результаты краулинга."""
    global _crawl_task

    if not _coordinator:
        raise HTTPException(404, "Нет данных краулинга")

    if _crawl_task and _crawl_task.done():
        try:
            result = _crawl_task.result()
            return CrawlResultsResponse(
                status="completed",
                total_pages=result.get("pages_crawled", 0),
                total_text_bytes=result.get("total_text_bytes", 0),
                time_sec=result.get("time_sec", 0),
                pages_per_sec=result.get("pages_per_sec", 0),
                domain_stats=result.get("domain_stats", {}),
                queue_stats=result.get("queue_stats", {}),
            )
        except Exception as e:
            return CrawlResultsResponse(status=f"error: {e}")

    return CrawlResultsResponse(
        status="in_progress",
        total_pages=len(_coordinator.results),
    )
