"""
Kolibri Autonomous Learning Agent.

Автономный агент, который:
1. Получает тему от пользователя
2. Ищет релевантные страницы через поисковые системы (DDG, Bing, Wikipedia)
3. Загружает и извлекает чистый текст из найденных страниц
4. Обучает модель KLM на собранных данных
5. Обучает Python-граф знаний (sentence store + knowledge graph + формулы)
6. Отслеживает прогресс и предоставляет real-time статистику

Архитектура:
  [Topic] → [Search Engines] → [URLs] → [Python Crawler] → [Text Files]
    → [C Mass Trainer --dir] → [KLM Model]
    → [Python train_text()] → [KnowledgeGraph + SentenceStore + Формулы]
"""
from __future__ import annotations

import asyncio
import hashlib
import logging
import os
import random
import re
import shutil
import tempfile
import time
import urllib3
from pathlib import Path
from typing import Any, Optional

import requests
from bs4 import BeautifulSoup
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

from .search_engine import generate_search_queries, search_topic

# Подавляем предупреждения SSL (для verify=False)
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

logger = logging.getLogger("kolibri.agent")

router = APIRouter(prefix="/api/v1/agent", tags=["agent"])

# --- Пути ---
PROJECT_ROOT = Path("/workspaces/kolibri-project")
TRAINER_BIN = PROJECT_ROOT / "build" / "kolibri_mass_trainer"
MODEL_DIR = PROJECT_ROOT / "data" / "models"
CORPUS_DIR = PROJECT_ROOT / "data" / "corpus"
DEFAULT_MODEL = MODEL_DIR / "kolibri_web.klm"

# --- HTTP User-Agent ротация ---
USER_AGENTS = [
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/119.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/118.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:120.0) "
    "Gecko/20100101 Firefox/120.0",
]

HEADERS_BASE = {
    "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    "Accept-Language": "en-US,en;q=0.9,ru;q=0.8",
    "Accept-Encoding": "gzip, deflate, br",
    "Connection": "keep-alive",
}


# ============================================================
# Pydantic Models
# ============================================================

class AgentStartRequest(BaseModel):
    topic: str = Field(description="Тема для автономного обучения")
    max_urls: int = Field(default=30, ge=5, le=100)
    engines: list[str] = Field(
        default=["duckduckgo", "wikipedia_en", "wikipedia_ru", "bing"],
    )
    model_name: Optional[str] = None


class PageInfo(BaseModel):
    url: str
    title: str = ""
    chars: int = 0
    ok: bool = False


class SearchResultInfo(BaseModel):
    url: str
    title: str = ""
    snippet: str = ""
    source: str = ""


class AgentStatus(BaseModel):
    running: bool = False
    topic: str = ""
    phase: str = "idle"
    progress: float = 0.0
    # Поиск
    urls_found: int = 0
    search_results: list[dict] = []
    # Загрузка
    urls_crawled: int = 0
    urls_total: int = 0
    urls_failed: int = 0
    current_url: str = ""
    bytes_downloaded: int = 0
    pages_text: list[dict] = []
    # Обучение
    patterns: int = 0
    edges: int = 0
    tokens: int = 0
    model_size_mb: float = 0.0
    # Время
    time_elapsed: float = 0.0
    # Лог
    events: list[dict] = []


# ============================================================
# Глобальное состояние агента
# ============================================================

_agent_state: AgentStatus = AgentStatus()
_agent_task: Optional[asyncio.Task[None]] = None


def _add_event(phase: str, message: str, **data: Any) -> None:
    """Добавить событие в лог агента."""
    _agent_state.events.append({
        "ts": time.time(),
        "phase": phase,
        "msg": message,
        **data,
    })
    if len(_agent_state.events) > 150:
        _agent_state.events = _agent_state.events[-150:]


# ============================================================
# Извлечение текста из HTML
# ============================================================

def _fetch_page_text(url: str, timeout: int = 20) -> tuple[str, str, int]:
    """
    Загружает страницу и извлекает чистый текст.

    Returns:
        (text, title, bytes_downloaded)
    """
    headers = {
        **HEADERS_BASE,
        "User-Agent": random.choice(USER_AGENTS),
    }

    resp = requests.get(
        url,
        headers=headers,
        timeout=timeout,
        allow_redirects=True,
        verify=False,
    )
    resp.raise_for_status()

    bytes_dl = len(resp.content)

    # Определяем кодировку
    if resp.encoding and resp.encoding.lower() not in ("iso-8859-1", "ascii"):
        html = resp.text
    else:
        resp.encoding = resp.apparent_encoding or "utf-8"
        html = resp.text

    soup = BeautifulSoup(html, "html.parser")

    # Заголовок
    title_tag = soup.find("title")
    title = title_tag.get_text(strip=True) if title_tag else url.split("/")[-1]

    # Удаляем ненужные элементы
    for tag in soup([
        "script", "style", "nav", "footer", "header",
        "aside", "noscript", "iframe", "form", "button",
        "svg", "img", "video", "audio", "figure",
        "input", "select", "textarea",
    ]):
        tag.decompose()

    # Ищем основной контент (article > main > .content > body)
    main_content = (
        soup.find("article")
        or soup.find("main")
        or soup.find(class_=re.compile(r"content|article|post|entry|text", re.I))
        or soup.find("div", {"id": re.compile(r"content|article|main|body", re.I)})
        or soup.find("body")
    )

    if main_content:
        text = main_content.get_text(separator="\n", strip=True)
    else:
        text = soup.get_text(separator="\n", strip=True)

    # Очистка: убираем пустые и очень короткие строки
    lines = []
    for line in text.split("\n"):
        line = line.strip()
        if len(line) > 10:
            lines.append(line)

    text = "\n".join(lines)

    # Ограничение: 100KB на страницу
    if len(text) > 100_000:
        text = text[:100_000]

    return text, title, bytes_dl


# ============================================================
# Основной цикл агента
# ============================================================

async def _run_agent(
    topic: str,
    max_urls: int,
    engines: list[str],
    model_path: str,
) -> None:
    """Главный рабочий цикл автономного агента обучения."""
    global _agent_state

    start_time = time.monotonic()
    temp_dir: Optional[str] = None

    try:
        loop = asyncio.get_event_loop()

        # ═══════════════════════════════════════════════
        # ФАЗА 1: ПОИСК
        # ═══════════════════════════════════════════════
        _agent_state.phase = "searching"
        _agent_state.progress = 5.0
        _add_event("searching", f"🔍 Начинаю поиск по теме: «{topic}»")

        queries = generate_search_queries(topic)
        _add_event("searching", f"📝 Сгенерировано {len(queries)} поисковых запросов")

        # Поиск в отдельном потоке (sync requests)
        search_results = await loop.run_in_executor(
            None, search_topic, topic, max_urls, engines,
        )

        _agent_state.search_results = search_results
        _agent_state.urls_found = len(search_results)
        _agent_state.urls_total = len(search_results)
        _agent_state.progress = 15.0

        # Группируем по источникам для красивого лога
        sources: dict[str, int] = {}
        for r in search_results:
            src = r.get("source", "unknown")
            sources[src] = sources.get(src, 0) + 1
        src_str = ", ".join(f"{k}: {v}" for k, v in sources.items())
        _add_event(
            "searching",
            f"✅ Найдено {len(search_results)} URL [{src_str}]",
        )

        if not search_results:
            _agent_state.phase = "error"
            _add_event("error", "❌ Не удалось найти URL по данной теме")
            return

        # ═══════════════════════════════════════════════
        # ФАЗА 2: ЗАГРУЗКА СТРАНИЦ
        # ═══════════════════════════════════════════════
        _agent_state.phase = "crawling"
        _add_event("crawling", f"🕷️ Загружаю {len(search_results)} страниц...")

        temp_dir = tempfile.mkdtemp(prefix="kolibri_agent_")
        total = len(search_results)

        for i, result in enumerate(search_results):
            if not _agent_state.running:
                _add_event("crawling", "⏹️ Остановлено пользователем")
                break

            url = result["url"]
            _agent_state.current_url = url
            _agent_state.urls_crawled = i

            # Прогресс: 15% → 70%
            _agent_state.progress = 15.0 + (i / total) * 55.0

            try:
                text, title, bytes_dl = await loop.run_in_executor(
                    None, _fetch_page_text, url,
                )

                _agent_state.bytes_downloaded += bytes_dl

                if len(text) < 100:
                    _agent_state.urls_failed += 1
                    _agent_state.pages_text.append(
                        {"url": url, "title": title, "chars": 0, "ok": False},
                    )
                    _add_event(
                        "crawling",
                        f"⚠️ [{i+1}/{total}] Мало текста: {url[:60]}",
                    )
                    continue

                # Сохраняем текст в temp файл
                safe_name = hashlib.md5(url.encode()).hexdigest()[:12]
                file_path = os.path.join(temp_dir, f"{safe_name}.txt")
                with open(file_path, "w", encoding="utf-8") as f:
                    f.write(f"# {title}\n# Source: {url}\n\n{text}")

                _agent_state.pages_text.append(
                    {"url": url, "title": title, "chars": len(text), "ok": True},
                )
                _add_event(
                    "crawling",
                    f"✅ [{i+1}/{total}] {title[:50]} — {len(text):,} символов",
                )

            except Exception as e:
                _agent_state.urls_failed += 1
                _agent_state.pages_text.append(
                    {"url": url, "title": "", "chars": 0, "ok": False},
                )
                _add_event(
                    "crawling",
                    f"❌ [{i+1}/{total}] {url[:50]}: {e}",
                )

            # Задержка между запросами (вежливый краулинг)
            await asyncio.sleep(0.3)

        _agent_state.urls_crawled = total

        successful = sum(1 for p in _agent_state.pages_text if p["ok"])
        total_chars = sum(p["chars"] for p in _agent_state.pages_text)
        _add_event(
            "crawling",
            f"📊 Итого: {successful}/{total} страниц, "
            f"{total_chars:,} символов, "
            f"{_agent_state.bytes_downloaded / 1024:.0f} КБ",
        )

        if successful == 0:
            _agent_state.phase = "error"
            _add_event("error", "❌ Не удалось загрузить ни одной страницы")
            return

        # ═══════════════════════════════════════════════
        # ФАЗА 3: ОБУЧЕНИЕ МОДЕЛИ
        # ═══════════════════════════════════════════════
        _agent_state.phase = "training"
        _agent_state.progress = 75.0
        _add_event(
            "training",
            f"🧠 Обучаю модель на {successful} документах...",
        )

        if not TRAINER_BIN.exists():
            _agent_state.phase = "error"
            _add_event("error", f"❌ Бинарник не найден: {TRAINER_BIN}")
            return

        MODEL_DIR.mkdir(parents=True, exist_ok=True)

        # Эволюция ВКЛЮЧЕНА: формулы реально обучаются через мутации + селекцию
        # --generations 5 даёт реальный рост fitness (компромисс скорость/качество)
        cmd = [
            str(TRAINER_BIN),
            "--model", model_path,
            "--dir", temp_dir,
            "--generations", "5",
            "--verbose",
        ]

        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
            cwd=str(PROJECT_ROOT),
        )

        full_output = ""
        assert proc.stdout is not None

        while True:
            line = await proc.stdout.readline()
            if not line:
                break
            decoded = line.decode("utf-8", errors="replace").rstrip()
            full_output += decoded + "\n"

            # Прогресс обучения: 75% → 95%
            if "[Train]" in decoded:
                _add_event("training", f"  {decoded}")
                # Пытаемся определить прогресс из "Обработано N файлов"
                m = re.search(r"Обработано\s+(\d+)\s+файлов", decoded)
                if m:
                    done_files = int(m.group(1))
                    _agent_state.progress = 75.0 + min(
                        20.0, (done_files / max(1, successful)) * 20.0,
                    )
            elif "[Model]" in decoded:
                _add_event("training", f"  {decoded}")

        await proc.wait()

        _agent_state.progress = 95.0

        # Парсим статистику из вывода тренера
        m = re.search(r"Паттернов в модели:\s*(\d+)", full_output)
        if m:
            _agent_state.patterns = int(m.group(1))

        m = re.search(r"Рёбер в графе:\s*(\d+)", full_output)
        if m:
            _agent_state.edges = int(m.group(1))

        m = re.search(r"Токенов обработано:\s*(\d+)", full_output)
        if m:
            _agent_state.tokens = int(m.group(1))

        m = re.search(r"Размер модели:\s*([\d.]+)\s*МБ", full_output)
        if m:
            _agent_state.model_size_mb = float(m.group(1))

        # ═══════════════════════════════════════════════
        # ФАЗА 4: ОБУЧЕНИЕ PYTHON-ГРАФА ЗНАНИЙ
        # ═══════════════════════════════════════════════
        _agent_state.progress = 92.0
        _add_event("training", "🧬 Обучаю числовой граф знаний (Python)...")

        # Импортируем движок для обучения Python-графа
        from .ai_engine import get_engine
        engine = get_engine()

        # Сохраняем тексты в постоянный корпус + обучаем Python-граф
        CORPUS_DIR.mkdir(parents=True, exist_ok=True)
        topic_slug = re.sub(r'[^\w]+', '_', topic.lower())[:40]
        texts_trained = 0

        if temp_dir and os.path.exists(temp_dir):
            for fname in os.listdir(temp_dir):
                if not fname.endswith(".txt"):
                    continue
                src_path = os.path.join(temp_dir, fname)
                try:
                    content = open(src_path, "r", encoding="utf-8").read()
                    if len(content) < 100:
                        continue

                    # 1. Обучаем Python-граф и sentence store
                    engine.train_text(content)
                    texts_trained += 1

                    # 2. Копируем в постоянный корпус (переживёт перезагрузку)
                    dest = CORPUS_DIR / f"agent_{topic_slug}_{fname}"
                    if not dest.exists():
                        dest.write_text(content, encoding="utf-8")

                except Exception as e:
                    logger.warning("Ошибка обучения Python-графа на %s: %s", fname, e)

        if texts_trained > 0:
            _add_event(
                "training",
                f"✅ Python-граф обучен на {texts_trained} документах "
                f"({engine.sentence_store.size} предложений в памяти)",
            )

        # ═══════════════════════════════════════════════
        # ФАЗА 5: ГОТОВО
        # ═══════════════════════════════════════════════
        _agent_state.phase = "complete"
        _agent_state.progress = 100.0
        _agent_state.time_elapsed = time.monotonic() - start_time

        _add_event(
            "complete",
            f"🎉 Обучение завершено! "
            f"{_agent_state.patterns:,} паттернов, "
            f"{_agent_state.edges:,} связей, "
            f"{_agent_state.model_size_mb} МБ — "
            f"за {_agent_state.time_elapsed:.1f} сек",
        )

    except asyncio.CancelledError:
        _agent_state.phase = "error"
        _add_event("error", "⏹️ Агент остановлен пользователем")
    except Exception as e:
        _agent_state.phase = "error"
        _add_event("error", f"💥 Критическая ошибка: {e}")
        logger.exception("Agent error")
    finally:
        _agent_state.running = False
        _agent_state.time_elapsed = time.monotonic() - start_time
        # Очистка временных файлов
        if temp_dir and os.path.exists(temp_dir):
            shutil.rmtree(temp_dir, ignore_errors=True)


# ============================================================
# API Endpoints
# ============================================================

@router.post("/start")
async def start_agent(req: AgentStartRequest) -> dict:
    """Запуск автономного агента обучения по теме."""
    global _agent_state, _agent_task

    if _agent_state.running:
        raise HTTPException(409, "Агент уже работает. Остановите текущую задачу.")

    model_name = req.model_name or "kolibri_web.klm"
    model_path = str(MODEL_DIR / model_name)

    # Полный сброс состояния
    _agent_state = AgentStatus(
        running=True,
        topic=req.topic,
        phase="searching",
        progress=0.0,
    )
    _add_event("searching", f"🚀 Агент запущен — тема: «{req.topic}»")

    # Запускаем фоновую задачу
    _agent_task = asyncio.create_task(
        _run_agent(req.topic, req.max_urls, req.engines, model_path),
    )

    return {
        "status": "started",
        "topic": req.topic,
        "max_urls": req.max_urls,
        "engines": req.engines,
    }


@router.get("/status", response_model=AgentStatus)
async def agent_status() -> AgentStatus:
    """Текущий статус агента (для polling из frontend)."""
    return _agent_state


@router.post("/stop")
async def stop_agent() -> dict:
    """Остановить работающего агента."""
    global _agent_task

    if _agent_task and not _agent_task.done():
        _agent_state.running = False
        _agent_task.cancel()
        return {"status": "stopping"}

    return {"status": "not_running"}
