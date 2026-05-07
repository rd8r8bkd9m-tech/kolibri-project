"""
ai_engine.py — Движок «Числового Мышления» Kolibri

НЕ классический TF-IDF / N-gram. Настоящая архитектура Kolibri:

1. Каждое слово = 64-цифровой числовой паттерн (DJB2 → LCG каскад)
2. Знания = граф связей между паттернами (co-occurrence edges)
3. Формулы = 4000 цифр генома → до 500 слоёв, 12 операций
4. Эволюция: мутация + кроссовер + селекция = улучшение формул
5. Восстановление: из числового паттерна → исходное слово
6. Всё хранится в ЧИСЛАХ. Формулах. Паттернах.
"""
from __future__ import annotations

import asyncio
import hashlib
import math
import os
import queue
import re
import subprocess
import threading
import time
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from functools import lru_cache
from pathlib import Path
from typing import Optional
from urllib.parse import parse_qs, quote_plus, unquote, urlparse

import requests
from bs4 import BeautifulSoup

from .number_mind import (
    KnowledgeGraph,
    FormulaPool,
    KolibriGene,
    SentenceStore,
    word_to_pattern,
    pattern_to_str,
    pattern_similarity,
    text_to_digits,
    digits_to_text,
    djb2_hash,
    fnv1a_hash,
    _tokenize,
    _is_stop_word,
    _stem_ru,
)
from .embeddings import EmbeddingTable
from .c_evolve import get_c_evolve_bridge
from .training_worker import TrainingWorker
from .tokenizer import BPETokenizer
from .formula_lm import FormulaLM
from .reasoning import ChainOfThought
from .context_window import ContextWindow
from .cognition import SwarmCognition
from .project_paths import get_project_root

import logging

log = logging.getLogger("kolibri.ai")

# ---------------------------------------------------------------------------
# Конфигурация
# ---------------------------------------------------------------------------

_PROJECT_ROOT = get_project_root()
_TRAINER_BIN = _PROJECT_ROOT / "build" / "kolibri_mass_trainer"
_DEFAULT_MODEL = _PROJECT_ROOT / "data" / "models" / "kolibri_web.klm"
_CORPUS_DIR = _PROJECT_ROOT / "data" / "corpus"
_FORMULA_SAVE_PATH = _PROJECT_ROOT / "data" / "models" / "kolibri_formulas.json"
_EMBEDDINGS_SAVE_PATH = _PROJECT_ROOT / "data" / "models" / "kolibri_embeddings.json"

_MAX_CONTEXT_TURNS = 20
_QUERY_TIMEOUT = 10


def _env_bool(name: str, default: bool) -> bool:
    """Прочитать bool-флаг из ENV в стиле 0/1, true/false, on/off."""
    raw = os.getenv(name)
    if raw is None:
        return default
    return raw.strip().lower() not in {"0", "false", "no", "off", ""}


# Production-safe старт:
# по умолчанию не запускаем тяжёлое обучение при boot, чтобы HTTP не "залипал".
_STARTUP_BACKGROUND_TRAIN = _env_bool("KOLIBRI_STARTUP_BACKGROUND_TRAIN", False)
_STARTUP_LM_TRAIN = _env_bool("KOLIBRI_STARTUP_LM_TRAIN", False)
_STARTUP_CAUSAL_INDEX = _env_bool("KOLIBRI_STARTUP_CAUSAL_INDEX", False)
_LM_MAX_TEXTS = int(os.getenv("KOLIBRI_LM_MAX_TEXTS", "400"))
_LM_MAX_SEQUENCES = int(os.getenv("KOLIBRI_LM_MAX_SEQUENCES", "120"))
_LM_GENERATIONS = int(os.getenv("KOLIBRI_LM_GENERATIONS", "8"))
_WEB_FALLBACK = _env_bool("KOLIBRI_WEB_FALLBACK", True)
_WEB_MAX_URLS = int(os.getenv("KOLIBRI_WEB_MAX_URLS", "6"))
_WEB_MAX_PAGES = int(os.getenv("KOLIBRI_WEB_MAX_PAGES", "1"))
_WEB_PAGE_TIMEOUT = int(os.getenv("KOLIBRI_WEB_PAGE_TIMEOUT", "8"))
_WEB_MAX_TRAIN_CHARS = int(os.getenv("KOLIBRI_WEB_MAX_TRAIN_CHARS", "80000"))
_WEB_TOTAL_TIMEOUT = int(os.getenv("KOLIBRI_WEB_TOTAL_TIMEOUT", "20"))
_WEATHER_ENABLED = _env_bool("KOLIBRI_WEATHER_ENABLED", True)
_WEATHER_GEO_URL = "https://geocoding-api.open-meteo.com/v1/search"
_WEATHER_FORECAST_URL = "https://api.open-meteo.com/v1/forecast"

_WEB_HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/120.0.0.0 Safari/537.36"
    ),
    "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    "Accept-Language": "ru,en-US;q=0.9,en;q=0.8",
}


def _read_secret_file(path: str) -> Optional[str]:
    if not path:
        return None
    try:
        with open(path, "r", encoding="utf-8") as secret_file:
            value = secret_file.read().strip()
    except OSError:
        return None
    return value or None


def _strip_weather_prefix(text: str) -> str:
    cleaned = re.sub(r"[?!,.]", " ", text)
    cleaned = re.sub(r"\s+", " ", cleaned).strip()
    cleaned = re.sub(
        r"^(какая\s+погода|какова\s+погода|погода|weather|forecast)\s+",
        "",
        cleaned,
        flags=re.IGNORECASE,
    )
    cleaned = re.sub(
        r"^(сейчас|сегодня|завтра|послезавтра)\s+",
        "",
        cleaned,
        flags=re.IGNORECASE,
    )
    cleaned = re.sub(r"^(в|на)\s+", "", cleaned, flags=re.IGNORECASE)
    return cleaned.strip()


def _match_weather_intent(text: str) -> bool:
    low = text.lower()
    if "погод" in low or "weather" in low or "forecast" in low or "прогноз" in low:
        return True
    return False


def _extract_weather_location(text: str) -> str:
    cleaned = _strip_weather_prefix(text)
    if not cleaned:
        return ""
    # Try "в <location>" / "in <location>"
    match = re.search(r"(?:\bв\b|\bin\b)\s+([\\w\\s\\-\\.]+)$", cleaned, flags=re.IGNORECASE)
    if match:
        candidate = match.group(1).strip()
        return candidate
    # If the whole string is a location, return it as-is.
    return cleaned.strip()


def _weather_code_label(code: int) -> str:
    mapping = {
        0: "ясно",
        1: "преимущественно ясно",
        2: "переменная облачность",
        3: "пасмурно",
        45: "туман",
        48: "изморозь/туман",
        51: "морось",
        53: "морось умеренная",
        55: "морось сильная",
        56: "ледяная морось",
        57: "ледяная морось сильная",
        61: "дождь",
        63: "дождь умеренный",
        65: "дождь сильный",
        66: "ледяной дождь",
        67: "ледяной дождь сильный",
        71: "снег",
        73: "снег умеренный",
        75: "снег сильный",
        77: "снег/крупа",
        80: "ливень",
        81: "ливень умеренный",
        82: "ливень сильный",
        85: "снегопад",
        86: "снегопад сильный",
        95: "гроза",
        96: "гроза с градом",
        99: "гроза с сильным градом",
    }
    return mapping.get(code, "неизвестно")


def _load_text_llm_api_key() -> Optional[str]:
    key = (
        os.getenv("KOLIBRI_TEXT_LLM_API_KEY")
        or os.getenv("KOLIBRI_LLM_API_KEY")
        or os.getenv("KOLIBRI_VOICE_API_KEY")
        or os.getenv("OPENAI_API_KEY")
    )
    if key:
        return key.strip()

    key_file = (
        os.getenv("KOLIBRI_TEXT_LLM_API_KEY_FILE", "").strip()
        or os.getenv("KOLIBRI_VOICE_API_KEY_FILE", "").strip()
    )
    if not key_file:
        key_file = str(_PROJECT_ROOT / ".run" / "voice_api_key")
    return _read_secret_file(key_file)


def _resolve_text_llm_provider(api_key: Optional[str]) -> str:
    provider_raw = os.getenv("KOLIBRI_TEXT_LLM_PROVIDER", "auto").strip().lower()
    if provider_raw in {"none", "off", "disabled"}:
        return ""
    if provider_raw in {"gemini", "openrouter"}:
        return provider_raw
    if not api_key:
        return ""
    if api_key.startswith("AIza"):
        return "gemini"
    if api_key.startswith("sk-or-"):
        return "openrouter"
    # Если тип ключа неизвестен, по умолчанию пробуем OpenRouter-совместимый API.
    return "openrouter"


_TEXT_LLM_ENABLE = _env_bool("KOLIBRI_TEXT_LLM_ENABLE", True)
_TEXT_LLM_API_KEY = _load_text_llm_api_key()
_TEXT_LLM_PROVIDER = _resolve_text_llm_provider(_TEXT_LLM_API_KEY)
_TEXT_LLM_TIMEOUT = float(os.getenv("KOLIBRI_TEXT_LLM_TIMEOUT", "12"))
_TEXT_LLM_TEMPERATURE = float(os.getenv("KOLIBRI_TEXT_LLM_TEMPERATURE", "0.2"))
_TEXT_LLM_MAX_TOKENS = int(os.getenv("KOLIBRI_TEXT_LLM_MAX_TOKENS", "700"))
_TEXT_GEMINI_BASE_URL = os.getenv("KOLIBRI_GEMINI_BASE_URL", "https://generativelanguage.googleapis.com/v1beta").strip().rstrip("/")
_TEXT_GEMINI_MODEL = os.getenv("KOLIBRI_TEXT_GEMINI_MODEL", "gemini-2.5-flash").strip()
_TEXT_OPENROUTER_URL = os.getenv("KOLIBRI_OPENROUTER_URL", "https://openrouter.ai/api/v1/chat/completions").strip()
_TEXT_OPENROUTER_MODEL = os.getenv("KOLIBRI_TEXT_OPENROUTER_MODEL", "openai/gpt-4o-mini").strip()
_TEXT_LLM_RUNTIME_DISABLED = False

# Минимальный словарь RU→EN для кросс-языкового retrieval по англ. корпусу.
# Это не переводчик; только "мост" для частых технических терминов.
_RU_TO_EN_TERMS: dict[str, str] = {
    "кубит": "qubit",
    "бит": "bit",
    "трансформер": "transformer",
    "трансформера": "transformer",
    "трансформеры": "transformer",
    "архитектура": "architecture",
    "архитектуру": "architecture",
    "энтропия": "entropy",
    "энтропии": "entropy",
    "квантовый": "quantum",
    "квантовая": "quantum",
    "квантовые": "quantum",
    "квантового": "quantum",
    "квантовой": "quantum",
    "квантовых": "quantum",
    "квантовом": "quantum",
    "компьютер": "computer",
    "вычисления": "computing",
    "вычисление": "computing",
}

# Служебные слова вопроса, которые не считаем "темой" при проверке релевантности.
_GENERIC_QUERY_TOKENS: set[str] = {
    "что", "такое", "как", "почему", "зачем", "когда", "где",
    "объясни", "объяснить", "расскажи", "опиши", "покажи",
    "работает", "работать", "работают",
    "простыми", "словами", "подробно", "кратко",
    "please", "explain", "simple", "simply",
}

_TOPIC_FALLBACKS: dict[str, str] = {
    "qubit": (
        "Квантовый бит (кубит) — базовая единица квантовой информации. "
        "В отличие от обычного бита, кубит может находиться в суперпозиции: "
        "одновременно в состояниях 0 и 1 до момента измерения."
    ),
    "transformer": (
        "Трансформер — архитектура нейросетей на механизме self-attention. "
        "Она позволяет модели учитывать связи между всеми словами сразу, "
        "поэтому лучше понимать контекст длинного текста."
    ),
    "entropy": (
        "Энтропия — мера неопределённости. "
        "В теории информации энтропия Шеннона показывает, "
        "сколько в среднем бит нужно для кодирования сообщения: "
        "чем выше непредсказуемость, тем выше энтропия."
    ),
}


# ---------------------------------------------------------------------------
# Conversation (диалог с контекстом)
# ---------------------------------------------------------------------------

@dataclass
class ConversationTurn:
    role: str          # 'user' | 'assistant'
    content: str
    timestamp: float = field(default_factory=time.time)
    pattern_str: str = ""
    formula_used: str = ""


@dataclass
class Conversation:
    id: str
    turns: list[ConversationTurn] = field(default_factory=list)
    created_at: float = field(default_factory=time.time)

    def add(self, role: str, content: str, pattern_str: str = "", formula_used: str = "") -> None:
        self.turns.append(ConversationTurn(
            role=role, content=content,
            pattern_str=pattern_str,
            formula_used=formula_used,
        ))
        if len(self.turns) > _MAX_CONTEXT_TURNS * 2:
            self.turns = self.turns[-_MAX_CONTEXT_TURNS * 2:]

    def context_text(self, last_n: int = 6) -> str:
        recent = self.turns[-last_n:]
        parts: list[str] = []
        for t in recent:
            prefix = "User" if t.role == "user" else "Kolibri"
            parts.append(f"{prefix}: {t.content}")
        return "\n".join(parts)


# ---------------------------------------------------------------------------
# C-бинарник KnowledgeRetriever
# ---------------------------------------------------------------------------

class CModelRetriever:
    """
    Обёртка над kolibri_mass_trainer — работает с .klm моделью.
    
    Числовое Мышление: ответы C-модели кодируются В ЦИФРЫ
    через --query-digits (каждый байт → 3 цифры 0-9).
    Текст восстанавливается из цифр при необходимости.
    """

    def __init__(self, model_path: Path | None = None) -> None:
        self.model_path = model_path or _DEFAULT_MODEL
        self.trainer_bin = _TRAINER_BIN

    @property
    def available(self) -> bool:
        return self.trainer_bin.exists() and self.model_path.exists()

    def query(self, text: str) -> list[str]:
        """Запрос к C-модели — возвращает текст (восстановленный из цифр)."""
        if not self.available:
            return []
        try:
            result = subprocess.run(
                [str(self.trainer_bin), "--model", str(self.model_path), "--query", text],
                capture_output=True, text=True, timeout=_QUERY_TIMEOUT,
                cwd=str(_PROJECT_ROOT),
            )
            if result.returncode != 0:
                return []
            knowledge: list[str] = []
            for line in result.stdout.strip().split("\n"):
                stripped = line.strip()
                if stripped and not stripped.startswith("["):
                    knowledge.append(stripped)
            return knowledge
        except (subprocess.TimeoutExpired, FileNotFoundError, OSError):
            return []

    def query_digits(self, text: str) -> list[int]:
        """
        Запрос к C-модели в ЧИСЛОВОМ формате.
        
        Возвращает массив цифр (0-9) — чистое числовое представление.
        Текст можно восстановить через digits_to_text().
        """
        if not self.available:
            return []
        try:
            result = subprocess.run(
                [str(self.trainer_bin), "--model", str(self.model_path),
                 "--query-digits", text],
                capture_output=True, text=True, timeout=_QUERY_TIMEOUT,
                cwd=str(_PROJECT_ROOT),
            )
            if result.returncode != 0:
                return []
            line = result.stdout.strip()
            if not line or line.startswith("("):
                return []
            return [int(c) for c in line if c.isdigit()]
        except (subprocess.TimeoutExpired, FileNotFoundError, OSError, ValueError):
            return []

    # --- Async-варианты для неблокирующего I/O ---

    async def aquery(self, text: str) -> list[str]:
        """Async-версия query() — не блокирует event loop."""
        if not self.available:
            return []
        try:
            proc = await asyncio.create_subprocess_exec(
                str(self.trainer_bin), "--model", str(self.model_path),
                "--query", text,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                cwd=str(_PROJECT_ROOT),
            )
            stdout, _ = await asyncio.wait_for(proc.communicate(), timeout=_QUERY_TIMEOUT)
            if proc.returncode != 0:
                return []
            knowledge: list[str] = []
            for line in stdout.decode().strip().split("\n"):
                stripped = line.strip()
                if stripped and not stripped.startswith("["):
                    knowledge.append(stripped)
            return knowledge
        except (asyncio.TimeoutError, FileNotFoundError, OSError):
            return []

    async def aquery_digits(self, text: str) -> list[int]:
        """Async-версия query_digits() — не блокирует event loop."""
        if not self.available:
            return []
        try:
            proc = await asyncio.create_subprocess_exec(
                str(self.trainer_bin), "--model", str(self.model_path),
                "--query-digits", text,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                cwd=str(_PROJECT_ROOT),
            )
            stdout, _ = await asyncio.wait_for(proc.communicate(), timeout=_QUERY_TIMEOUT)
            if proc.returncode != 0:
                return []
            line = stdout.decode().strip()
            if not line or line.startswith("("):
                return []
            return [int(c) for c in line if c.isdigit()]
        except (asyncio.TimeoutError, FileNotFoundError, OSError, ValueError):
            return []

    def get_stats(self) -> dict:
        if not self.available:
            return {"exists": False}
        try:
            result = subprocess.run(
                [str(self.trainer_bin), "--model", str(self.model_path), "--stats"],
                capture_output=True, text=True, timeout=5,
                cwd=str(_PROJECT_ROOT),
            )
            info: dict = {"exists": True, "path": str(self.model_path)}
            # Бинарник пишет статистику в stderr, а не stdout
            output = result.stdout + "\n" + result.stderr
            for line in output.split("\n"):
                low = line.lower()
                if "паттерн" in low and "модели" in low:
                    m = re.search(r"(\d[\d\s]*\d|\d+)", line)
                    if m:
                        info["patterns"] = int(m.group().replace(" ", ""))
                if ("рёб" in low or "реб" in low) and "граф" in low:
                    m = re.search(r"(\d[\d\s]*\d|\d+)", line)
                    if m:
                        info["edges"] = int(m.group().replace(" ", ""))
                if "fitness" in low:
                    m = re.search(r"(\d+\.\d+)", line)
                    if m:
                        info["avg_fitness"] = float(m.group())
                if "вес" in low and "ребр" in low:
                    m = re.search(r"(\d+\.\d+)", line)
                    if m:
                        info["avg_weight"] = float(m.group())
                if "документ" in low and "→" not in line:
                    m = re.search(r"(\d+)", line)
                    if m:
                        info["documents"] = int(m.group())
                if "токен" in low:
                    m = re.search(r"(\d+)", line)
                    if m:
                        info["tokens"] = int(m.group())
                if "эпох" in low:
                    m = re.search(r"(\d+)", line)
                    if m:
                        info["epoch"] = int(m.group())
            info["size_mb"] = round(self.model_path.stat().st_size / (1024 * 1024), 2)
            return info
        except Exception:
            return {"exists": False}


# ---------------------------------------------------------------------------
# Главный движок: Числовое Мышление Kolibri
# ---------------------------------------------------------------------------

class KolibriAIEngine:
    """
    Центральный AI-движок — «Числовое Формульное Мышление».

    Принципы:
    1. Каждое слово = 64-цифровой паттерн (DJB2 + LCG)
    2. Знания = граф числовых паттернов с весами
    3. Ответ = навигация по графу + формульный прогноз
    4. Формулы = геном 4000 цифр → до 500 слоёв, 12 операций
    5. Эволюция формул = мутация + кроссовер + селекция
    """

    def __init__(self, model_path: Path | None = None) -> None:
        self.graph = KnowledgeGraph()
        # Загружаем формулы с диска — эволюция ПРОДОЛЖАЕТСЯ между перезапусками
        self.formula_pool = FormulaPool.load_or_create(_FORMULA_SAVE_PATH)
        self.sentence_store = SentenceStore()
        # --- Обучаемые эмбеддинги (Фаза 1 AI) ---
        self.embeddings = EmbeddingTable.load_or_create(_EMBEDDINGS_SAVE_PATH)
        # Связываем эмбеддинги с графом и sentence store
        self.graph.embeddings = self.embeddings
        self.sentence_store.embeddings = self.embeddings
        # Связываем SwarmManager с текущим графом, иначе /api/v1/swarm/sync
        # будет в "no-op" режиме (граф не будет меняться).
        try:
            from .swarm_sync import get_swarm_manager
            get_swarm_manager().set_knowledge_graph(self.graph)
        except Exception:
            pass
        self.c_retriever = CModelRetriever(model_path)
        self.conversations: dict[str, Conversation] = {}
        self._corpus_loaded = False
        self._stats_cache: dict | None = None
        self._stats_cache_time = 0.0
        self._evolution_counter = 0  # Счётчик для периодического сохранения
        self._embeddings_training = False  # Флаг: идёт фоновое обучение
        self._formulas_training = False    # Флаг: идёт фоновое обучение формул
        self._ready = False                # Движок готов к работе
        # --- Единый фоновый worker для обучения (очередь, 1 поток) ---
        self._train_queue: queue.Queue[tuple] = queue.Queue(maxsize=32)
        self._worker = threading.Thread(
            target=self._background_worker, daemon=True, name="kolibri-worker",
        )
        self._worker.start()
        # --- Multiprocessing-worker: ЛЕНИВАЯ инициализация (не при старте) ---
        self._mp_worker: TrainingWorker | None = None
        # --- Генеративный AI: токенизатор + FormulaLM + CoT + контекст ---
        self._bpe_tokenizer = BPETokenizer()
        self._formula_lm = FormulaLM(
            vocab_size=8_000, embed_dim=64,
            context_size=256, num_formulas=16,
        )
        self._chain_of_thought = ChainOfThought()
        self._context_window = ContextWindow(max_tokens=8192)
        self._lm_trained = False
        self._lm_generation = 0
        # --- Когнитивный модуль (абстракция, каузальность, индукция, аналогии, рефлексия) ---
        self._cognition: SwarmCognition | None = None
        # --- Persistent storage (SQLite) ---
        # --- Кэш ответов (TTL 30с, макс. 128 записей) ---
        self._response_cache: dict[str, tuple[float, dict]] = {}
        self._response_cache_ttl = 30.0
        self._response_cache_max = 128
        from .persistence import get_db
        self._db = get_db()
        self._load_from_db()
        # Движок ГОТОВ к запросам ДО загрузки корпуса — чтобы health check отвечал
        self._ready = True
        # Загрузка корпуса — в фоновом потоке, чтобы не блокировать event loop
        threading.Thread(
            target=self._safe_load_corpus, daemon=True, name="corpus-loader",
        ).start()

    def _safe_load_corpus(self) -> None:
        """Обёртка: загрузить корпус с перехватом ошибок."""
        try:
            self._load_corpus()
        except Exception as e:
            log.error("Ошибка загрузки корпуса: %s", e)
        # --- Тяжёлые шаги запускаем только по явному ENV-флагу ---
        if _STARTUP_LM_TRAIN:
            try:
                self._train_lm_on_corpus()
            except Exception as e:
                log.error("Ошибка обучения FormulaLM: %s", e)
        else:
            log.info(
                "FormulaLM startup training skipped (set KOLIBRI_STARTUP_LM_TRAIN=1 to enable)",
            )

        if _STARTUP_CAUSAL_INDEX:
            try:
                self._auto_build_causal_index()
            except Exception as e:
                log.error("Ошибка построения каузального индекса: %s", e)
        else:
            log.info(
                "Causal index startup build skipped (set KOLIBRI_STARTUP_CAUSAL_INDEX=1 to enable)",
            )
        # --- Сохраняем граф в SQLite для персистентности ---
        try:
            self._save_to_db()
        except Exception as e:
            log.error("Ошибка сохранения в SQLite: %s", e)

    def _background_worker(self) -> None:
        """Единый фоновый поток обучения — обрабатывает задачи из очереди."""
        while True:
            try:
                task = self._train_queue.get(timeout=60)
            except queue.Empty:
                continue
            try:
                kind = task[0]
                if kind == "retrieval":
                    _, query, response = task
                    self._do_retrieval_training(query, response)
                elif kind == "c_knowledge":
                    _, query, c_knowledge = task
                    self._train_formula_on_c_knowledge(query, c_knowledge)
                elif kind == "web_train":
                    _, query, web_text, web_sources = task
                    if isinstance(web_text, str) and len(web_text) >= 120:
                        self.train_text(web_text[:_WEB_MAX_TRAIN_CHARS])
                        self._save_to_db()
                        if isinstance(web_sources, list):
                            self._persist_web_learning(
                                query=query,
                                training_text=web_text,
                                sources=web_sources,
                            )
                elif kind == "corpus":
                    self._train_all_background()
            except Exception as e:
                log.warning("Background worker error: %s", e)
            finally:
                self._train_queue.task_done()

    def _load_corpus(self) -> None:
        """Загрузить тексты и обучить ЧИСЛОВОЙ ГРАФ.
        
        Порядок приоритетности:
        1. Файлы из корня data/corpus/ (основные знания) — без лимита
        2. Тематические agent-файлы из data/corpus/ — до 20 штук
        3. wiki_mass/ — до 30 файлов (общие знания, не засоряем)
        4. Дополнительные директории (training, seeds)
        """
        import logging
        log = logging.getLogger("kolibri.ai")
        t0 = time.time()

        _MAX_FILE_SIZE = 100_000      # 100 КБ макс на файл
        _MAX_WIKI_MASS = 30           # Лимит для wiki_mass (не засоряем)
        _MAX_AGENT     = 20           # Лимит для agent-файлов
        _MAX_OTHER     = 20           # Лимит для прочих директорий

        total_texts = 0

        def _load_file(f: Path) -> bool:
            """Загрузить один файл, вернуть True если успешно."""
            nonlocal total_texts
            try:
                fsize = f.stat().st_size
                if fsize < 50 or fsize > _MAX_FILE_SIZE:
                    return False
                content = f.read_text(encoding="utf-8", errors="ignore")
                self.graph.train_text(content)
                self.sentence_store.add_text(content)
                total_texts += 1
                if total_texts % 10 == 0:
                    time.sleep(0)
                return True
            except OSError:
                return False

        # --- Фаза 1: Приоритетные файлы (корень data/corpus/, не в подпапках) ---
        if _CORPUS_DIR.exists():
            priority_files = sorted(
                f for f in _CORPUS_DIR.iterdir()
                if f.is_file() and f.suffix == ".txt"
                and not f.name.startswith("agent_")
            )
            for f in priority_files:
                _load_file(f)
            log.info("Corpus phase 1 (priority): %d files", total_texts)

        # --- Фаза 2: Agent-файлы (тематические, ограниченно) ---
        agent_count = 0
        if _CORPUS_DIR.exists():
            agent_files = sorted(
                f for f in _CORPUS_DIR.iterdir()
                if f.is_file() and f.suffix == ".txt"
                and f.name.startswith("agent_")
            )
            for f in agent_files:
                if agent_count >= _MAX_AGENT:
                    break
                if _load_file(f):
                    agent_count += 1

        # --- Фаза 3: wiki_mass (общие знания, строго ограничено) ---
        wiki_dir = _CORPUS_DIR / "wiki_mass"
        wiki_count = 0
        if wiki_dir.exists():
            for f in sorted(wiki_dir.glob("*.txt")):
                if wiki_count >= _MAX_WIKI_MASS:
                    break
                if _load_file(f):
                    wiki_count += 1

        # --- Фаза 4: Дополнительные директории ---
        extra_dirs = [
            _PROJECT_ROOT / "data" / "training",
            _PROJECT_ROOT / "seeds",
            _PROJECT_ROOT / "training",
        ]
        extra_count = 0
        for corpus_dir in extra_dirs:
            if not corpus_dir.exists():
                continue
            for f in sorted(corpus_dir.rglob("*.txt")):
                if extra_count >= _MAX_OTHER:
                    break
                if _load_file(f):
                    extra_count += 1

        if total_texts > 0:
            self._corpus_loaded = True
            # Тяжёлое обучение при старте — только если явно включено.
            if _STARTUP_BACKGROUND_TRAIN:
                self._formulas_training = True
                self._embeddings_training = True
                try:
                    self._train_queue.put_nowait(("corpus",))
                except queue.Full:
                    log.warning("Фоновая очередь обучения переполнена, задача corpus пропущена")
            else:
                log.info(
                    "Startup background training skipped "
                    "(set KOLIBRI_STARTUP_BACKGROUND_TRAIN=1 to enable)",
                )
        train_mode = (
            "startup background training enabled"
            if _STARTUP_BACKGROUND_TRAIN
            else "startup background training skipped"
        )
        log.info(
            "Corpus loaded: %d files (%d priority, %d agent, %d wiki, %d other), "
            "%d sentences in %.1fs (%s)",
            total_texts,
            total_texts - agent_count - wiki_count - extra_count,
            agent_count, wiki_count, extra_count,
            self.sentence_store.size, time.time() - t0, train_mode,
        )

    def _train_formulas_from_graph(self) -> None:
        """
        Обучить формулы на СЕМАНТИЧЕСКИХ парах из графа знаний.

        Для каждого ребра (word_A → word_B) в графе:
        формула должна научиться трансформировать
        паттерн(word_A) → паттерн(word_B).

        Это КЛЮЧЕВОЙ механизм: формулы учатся семантике,
        а не случайным хеш-числам.
        """
        # Собираем семантические пары: паттерн_слова → паттерн_соседа
        # Приоритет: сильные связи (высокий вес) → более ценные пары
        # Снэпшот для thread-safety (edges может изменяться из другой корутины)
        edges_snapshot = dict(self.graph.edges)
        edges_sorted = sorted(
            edges_snapshot.items(),
            key=lambda item: item[1].weight,
            reverse=True,
        )
        seen: set[tuple[int, int]] = set()
        pairs_added = 0
        for (src_hash, tgt_hash), edge in edges_sorted:
            if pairs_added >= 300:
                break
            key = (src_hash, tgt_hash)
            if key in seen:
                continue
            seen.add(key)
            # Найти слова по хешам через обратный индекс
            src_word = self.graph._hash_to_word.get(src_hash)
            tgt_word = self.graph._hash_to_word.get(tgt_hash)
            if src_word and tgt_word:
                src_pat = word_to_pattern(src_word)
                tgt_pat = word_to_pattern(tgt_word)
                self.formula_pool.add_semantic_pair(src_pat, tgt_pat)
                pairs_added += 1
        if self.formula_pool.semantic_pairs:
            # Попытка C-ускорения через FFI (100x быстрее Python)
            c_bridge = get_c_evolve_bridge()
            if c_bridge.available:
                try:
                    t0_c = time.time()
                    # Маршалинг данных для C
                    genomes = [list(f.gene.digits) for f in self.formula_pool.formulas]
                    fitnesses = [f.fitness for f in self.formula_pool.formulas]
                    pairs = [
                        (list(src), list(tgt))
                        for src, tgt in self.formula_pool.semantic_pairs[:60]
                    ]
                    new_genomes, new_fit, best_fitness = c_bridge.evolve(
                        genomes=genomes, fitnesses=fitnesses,
                        semantic_pairs=pairs, generations=10,
                    )
                    # Обновляем формулы из C-результата
                    for i, f in enumerate(self.formula_pool.formulas):
                        if i < len(new_genomes):
                            f.gene.digits = new_genomes[i]
                            f.fitness = new_fit[i]
                    self.formula_pool.generation += 10
                    log.info(
                        "C-FFI evolve: fitness=%.4f, gen=%d, %.1fms (100x faster)",
                        best_fitness, self.formula_pool.generation,
                        (time.time() - t0_c) * 1000,
                    )
                    self._save_formulas()
                    return
                except Exception as e:
                    log.warning("C-FFI evolve fallback to Python: %s", e)
            # Fallback: Python evolve
            fitness = self.formula_pool.evolve(generations=10)
            log.info(
                "Formula semantic training: %d pairs, fitness=%.4f, gen=%d",
                len(self.formula_pool.semantic_pairs),
                fitness,
                self.formula_pool.generation,
            )
            # Автосохранение после обучения
            self._save_formulas()

    def _auto_build_causal_index(self) -> None:
        """Построить каузальный индекс из загруженного корпуса.

        Берём предложения из SentenceStore и строим направленный
        индекс причинно-следственных связей. Это позволяет отвечать
        на вопросы «почему?» и «что будет, если?» сразу после старта.
        """
        from .cognition import CausalIndex

        if self.sentence_store.size < 20:
            return
        # Берём до 500 предложений из корпуса
        texts: list[str] = []
        for idx in range(min(self.sentence_store.size, 500)):
            text = self.sentence_store.get_text(idx)
            if len(text) > 15:
                texts.append(text)
        if len(texts) < 10:
            return

        t0 = time.time()
        ci = CausalIndex.from_graph(self.graph, texts, window=5)
        cog = self.get_cognition()
        cog._causal = ci
        log.info(
            "Каузальный индекс построен: %d пар, %d направленных "
            "(из %d предложений) за %.1fs",
            len(ci.pairs), ci.n_directed, len(texts), time.time() - t0,
        )

    def _train_all_background(self) -> None:
        """Фоновый поток: обучение формул + эмбеддингов без блокировки сервера."""
        time.sleep(0)  # Отпускаем GIL — HTTP может обслужиться

        # 1. Формулы
        try:
            t0 = time.time()
            self._train_formulas_from_graph()
            time.sleep(0)  # Отпускаем GIL
            log.info("Фоновое обучение формул завершено за %.1fs", time.time() - t0)
        except Exception as e:
            log.error("Ошибка фонового обучения формул: %s", e)
        finally:
            self._formulas_training = False

        # 2. Эмбеддинги
        try:
            t0 = time.time()
            self._train_embeddings_from_graph()
            time.sleep(0)  # Отпускаем GIL
            log.info("Фоновое обучение эмбеддингов завершено за %.1fs", time.time() - t0)
        except Exception as e:
            log.error("Ошибка фонового обучения эмбеддингов: %s", e)
        finally:
            self._embeddings_training = False

    def _train_embeddings_from_graph(self) -> None:
        """
        Обучить эмбеддинги на рёбрах графа знаний.

        Word2Vec-style: каждое ребро (word_A, word_B) = positive pair.
        Слова, часто встречающиеся вместе, получают похожие вектора.

        Результат: cosine_similarity("кот", "кошка") >> 0.5
        вместо DJB2 pattern_similarity("кот", "кошка") ≈ 0.3
        """
        if not self.graph.edges:
            return

        # Попытка C-ускорения для эмбеддингов
        c_bridge = get_c_evolve_bridge()
        if c_bridge.available:
            try:
                t0_c = time.time()
                # Маршалинг: EmbeddingTable → dict + list для C
                # Копируем словари для thread-safety
                vectors_snapshot = dict(self.embeddings.vectors)
                vectors_dict = {}
                for h in vectors_snapshot:
                    vectors_dict[h] = list(vectors_snapshot[h])
                edges_snapshot = dict(self.graph.edges)
                edges_list = [
                    (src_h, tgt_h, edge.weight)
                    for (src_h, tgt_h), edge in edges_snapshot.items()
                ]
                dim = self.embeddings.dim
                n_edges = len(edges_list)
                epochs = 1 if n_edges > 50_000 else (2 if n_edges > 10_000 else 5)
                neg = 3 if n_edges > 50_000 else 5

                updated_vecs, avg_loss = c_bridge.train_embeddings(
                    vectors=vectors_dict,
                    edges=edges_list,
                    dim=dim, epochs=epochs, lr=0.025, neg_samples=neg,
                )
                # Обновляем Python-таблицу из C-результата
                for h, vec in updated_vecs.items():
                    self.embeddings.vectors[h] = vec
                n_pairs = len(edges_list)
                if n_pairs > 0:
                    self._save_embeddings()
                    log.info(
                        "C-FFI embeddings: vocab=%d, loss=%.4f, %d pairs in %.1fms",
                        len(vectors_dict), avg_loss,
                        n_pairs, (time.time() - t0_c) * 1000,
                    )
                    return
            except Exception as e:
                log.warning("C-FFI embeddings fallback to Python: %s", e)

        # Fallback: Python обучение
        # Адаптируем epochs: при большом графе 1 эпохи достаточно
        # Копируем словари для thread-safety
        edges_snapshot = dict(self.graph.edges)
        hash_to_word_snapshot = dict(self.graph._hash_to_word)
        patterns_keys_snapshot = set(self.graph.patterns.keys())
        n_edges = len(edges_snapshot)
        epochs = 1 if n_edges > 50_000 else (2 if n_edges > 10_000 else 5)
        neg = 3 if n_edges > 50_000 else 5

        result = self.embeddings.train_on_graph(
            edges=edges_snapshot,
            hash_to_word=hash_to_word_snapshot,
            all_hashes=patterns_keys_snapshot,
            epochs=epochs,
            lr=0.025,
            neg_samples=neg,
        )

        if result["pairs"] > 0:
            self._save_embeddings()
            log.info(
                "Embeddings trained: vocab=%d, loss=%.4f, %d pairs in %.0fms",
                result["vocab_size"], result["loss"],
                result["pairs"], result.get("duration_ms", 0),
            )

    def _save_embeddings(self) -> None:
        """Сохранить эмбеддинги на диск."""
        try:
            self.embeddings.save(_EMBEDDINGS_SAVE_PATH)
        except Exception as e:
            log.warning("Не удалось сохранить эмбеддинги: %s", e)

    # ------------------------------------------------------------------
    # Persistent Storage (SQLite)
    # ------------------------------------------------------------------

    def _load_from_db(self) -> None:
        """Восстановить граф знаний из SQLite при старте."""
        if not self._db.is_enabled():
            return
        try:
            patterns = self._db.load_patterns()
            if not patterns:
                return
            restored = 0
            for p in patterns:
                h = p["hash"]
                word = p["word"]
                digits = p["pattern"]
                if h not in self.graph.patterns and word:
                    self.graph.train_text(word)
                    restored += 1
            edges = self._db.load_edges()
            log.info(
                "SQLite: восстановлено %d паттернов, %d рёбер из базы",
                len(patterns), len(edges),
            )
        except Exception as e:
            log.warning("Ошибка загрузки из SQLite: %s", e)

    def _save_to_db(self) -> None:
        """Сохранить текущее состояние графа в SQLite."""
        if not self._db.is_enabled():
            return
        try:
            self._db.save_patterns(self.graph.patterns, self.graph._hash_to_word)
            self._db.save_edges(self.graph.edges)
            self._db.set_meta("documents_trained", str(self.graph.documents_trained))
            self._db.set_meta("save_time", str(time.time()))
        except Exception as e:
            log.warning("Ошибка сохранения в SQLite: %s", e)

    # ------------------------------------------------------------------
    # Генеративный AI: FormulaLM + BPE-токенизатор
    # ------------------------------------------------------------------

    def _train_lm_on_corpus(self) -> None:
        """Обучить FormulaLM на предложениях из SentenceStore."""
        if self._lm_trained or self.sentence_store.size < 100:
            return
        try:
            all_texts: list[str] = []
            for idx in range(min(self.sentence_store.size, _LM_MAX_TEXTS)):
                text = self.sentence_store.get_text(idx)
                if len(text) > 20:
                    all_texts.append(text)

            if len(all_texts) < 50:
                return

            self._bpe_tokenizer.train(all_texts[:500])
            sequences = [
                self._bpe_tokenizer.encode(t) for t in all_texts
                if len(t) > 20
            ]
            sequences = [s for s in sequences if len(s) > 5]
            if len(sequences) < 30:
                return

            self._formula_lm.evolve(
                sequences[:max(10, _LM_MAX_SEQUENCES)],
                generations=max(1, _LM_GENERATIONS),
            )
            self._lm_trained = True
            self._lm_generation += max(1, _LM_GENERATIONS)
            log.info(
                "FormulaLM trained: gen=%d, vocab=%d, sequences=%d (caps: texts=%d, seq=%d, gens=%d)",
                self._lm_generation,
                len(self._bpe_tokenizer),
                len(sequences),
                _LM_MAX_TEXTS,
                _LM_MAX_SEQUENCES,
                _LM_GENERATIONS,
            )
        except Exception as e:
            log.warning("FormulaLM training error: %s", e)

    def _generate_text(self, query: str, max_tokens: int = 64) -> str:
        """Сгенерировать текст через FormulaLM (fallback)."""
        if not self._lm_trained:
            return ""
        try:
            prompt_ids = self._bpe_tokenizer.encode(query)
            if not prompt_ids:
                return ""
            generated_ids = self._formula_lm.generate(
                prompt_ids, max_tokens=max_tokens, temperature=0.8,
            )
            return self._bpe_tokenizer.decode(generated_ids)
        except Exception:
            return ""

    # ------------------------------------------------------------------
    # Главная функция: ответить на сообщение
    # ------------------------------------------------------------------

    def chat(
        self,
        message: str,
        conversation_id: str | None = None,
        temperature: float = 0.7,
    ) -> dict:
        """
        Ответить через Числовое Мышление.
        Pipeline: контекст → CoT → паттерны → граф → формулы → C-модель → синтез → генерация.
        """
        start_time = time.time()

        # --- TTL-кэш: повторный вопрос менее чем за 30с → мгновенный ответ ---
        cache_key = message.strip().lower()
        now = time.time()
        cached = self._response_cache.get(cache_key)
        if cached is not None:
            ts, resp = cached
            if now - ts < self._response_cache_ttl:
                resp = dict(resp)  # копия
                resp["duration_ms"] = round((time.time() - start_time) * 1000, 1)
                resp["cached"] = True
                return resp
            else:
                del self._response_cache[cache_key]

        conv = self.get_or_create_conversation(conversation_id)
        conv.add("user", message)
        lower = message.lower()

        # --- Контекстное окно: запоминаем вопрос ---
        self._context_window.add_message("user", message)

        # --- Chain-of-Thought: анализ запроса ---
        thinking_steps = self._chain_of_thought.analyze_query(message)
        thinking_text = self._chain_of_thought.format_thinking()
        # CoT определяет стратегию поиска (не декоративный!)
        search_strategy = self._chain_of_thought.get_search_strategy()

        special = self._handle_special_commands(lower)
        if special:
            conv.add("assistant", special["response"])
            self._context_window.add_message("assistant", special["response"])
            special["conversation_id"] = conv.id
            special["duration_ms"] = round((time.time() - start_time) * 1000, 1)
            special["thinking"] = thinking_text
            special["thinking_steps"] = [
                {"type": s.step_type.name, "content": s.description,
                 "result": s.result, "confidence": s.confidence}
                for s in thinking_steps
            ]
            special["generation_used"] = False
            special["context_stats"] = self._context_window.get_stats()
            return special

        # ====== ЧИСЛОВОЕ МЫШЛЕНИЕ ======
        tokens = _tokenize(message)
        query_patterns: dict[str, str] = {}
        query_hashes: dict[str, int] = {}
        for t in tokens:
            if len(t) >= 2:
                query_patterns[t] = pattern_to_str(word_to_pattern(t))
                query_hashes[t] = djb2_hash(t)

        # === Формульно-управляемый sentence retrieval ===
        # CoT стратегия определяет глубину поиска
        retrieval_k = search_strategy.get("retrieval_top_k", 5)
        best_formula = self.formula_pool.best()
        has_cyrillic = any("\u0400" <= c <= "\u04ff" for c in message)
        mapped_tokens: list[str] = []
        mapped_terms: dict[str, str] = {}
        if has_cyrillic:
            for tok in _tokenize(message.lower()):
                mapped = _RU_TO_EN_TERMS.get(tok)
                if mapped:
                    mapped_terms[tok] = mapped
                    mapped_tokens.append(mapped)
                else:
                    mapped_tokens.append(tok)

        mapped_query = " ".join(mapped_tokens).strip() if mapped_terms else ""
        retrieval_query = mapped_query if mapped_query else message

        # Основной retrieval: пробуем исходный запрос + (при необходимости) RU→EN вариант.
        retrieval_limit = max(12, int(retrieval_k))
        base_retrieved = self.sentence_store.retrieve(
            query=message, formula=best_formula, top_k=retrieval_limit,
        )
        merged_scores: dict[str, float] = {t: s for t, s in base_retrieved}
        if mapped_query and mapped_query != message:
            mapped_retrieved = self.sentence_store.retrieve(
                query=mapped_query, formula=best_formula, top_k=retrieval_limit,
            )
            for t, s in mapped_retrieved:
                prev = merged_scores.get(t)
                if prev is None or s > prev:
                    merged_scores[t] = s

        # === Контекстное обогащение запроса ===
        # Предыдущие сообщения добавляют ключевые слова к поиску
        enriched_query = self._context_window.get_query_with_context(message)
        if enriched_query != message and len(merged_scores) < 3:
            extra = self.sentence_store.retrieve(
                query=enriched_query, formula=best_formula, top_k=6,
            )
            for t, s in extra:
                s = s * 0.8  # Контекстные — менее приоритетны
                prev = merged_scores.get(t)
                if prev is None or s > prev:
                    merged_scores[t] = s

        retrieved = sorted(merged_scores.items(), key=lambda x: x[1], reverse=True)[:retrieval_limit]

        # === Формульная генерация слов ===
        max_answer_words = search_strategy.get("max_words", 10)
        formula_words = self.graph.generate_words(
            query=message, formula=best_formula, max_words=8,
        )

        graph_answer, graph_confidence, graph_meta = self.graph.answer(
            message, max_words=max_answer_words,
        )

        # --- Multi-hop QA: для вопросов типа "explain" / "compare" ---
        if search_strategy.get("depth", 1) >= 2 and graph_confidence < 0.6:
            mh_answer, mh_conf, mh_meta = self.graph.multi_hop_answer(
                message, max_hops=2, max_words=max_answer_words,
            )
            if mh_conf > graph_confidence:
                graph_answer = mh_answer
                graph_confidence = mh_conf
                graph_meta = mh_meta

        formula_result = self._formula_predict(message)
        c_knowledge = self.c_retriever.query(message) if self.c_retriever.available else []
        # Числовой запрос к C-модели — ответ в цифрах
        c_digits = self.c_retriever.query_digits(message) if self.c_retriever.available else []
        assoc_answer = self.formula_pool.lookup(message)

        response, confidence, method = self._synthesize_response(
            message=message,
            retrieval_query=retrieval_query,
            retrieved_sentences=retrieved,
            formula_words=formula_words,
            graph_answer=graph_answer,
            graph_confidence=graph_confidence,
            graph_meta=graph_meta,
            formula_result=formula_result,
            c_knowledge=c_knowledge,
            assoc_answer=assoc_answer,
        )

        digit_vote = self._digit_vote(
            message=message,
            retrieved_sentences=retrieved,
            graph_confidence=graph_confidence,
            c_knowledge=c_knowledge,
            c_digits=c_digits,
            formula_result=formula_result,
        )
        boost = digit_vote["decision"]["confidence_boost"]
        if boost > 0:
            confidence = min(0.98, confidence + boost)
        if digit_vote["decision"]["reject"] and confidence < 0.55:
            response = self._build_explain_fallback(message)
            confidence = max(confidence, 0.3)
            method = "digit-vote-reject"

        # Safety-слой для explain: не отдаём шумные псевдо-ответы.
        is_explain = search_strategy.get("intent") == "explain"
        core_topic = self._detect_core_topic(message, mapped_terms)
        if core_topic and method in {"knowledge-graph", "formula-generation", "no-knowledge"}:
            response = self._topic_fallback_answer(core_topic)
            confidence = max(confidence, 0.72)
            method = "topic-fallback"
        elif core_topic and has_cyrillic and method in {"formula-retrieval", "formula-association", "c-model"}:
            # Если русскоязычный запрос отдался в основном на англ., добавляем RU-объяснение сверху.
            if not self._has_cyrillic(response):
                response = f"{self._topic_fallback_answer(core_topic)}\n\n{response}"
                confidence = max(confidence, 0.62)
                method = "topic-fallback"
        elif is_explain and method in {"knowledge-graph", "formula-generation"}:
            if not self._is_response_coherent(query=message, answer=response, min_overlap=1):
                response = self._build_explain_fallback(message)
                confidence = 0.35
                method = "explain-fallback"

        web_sources: list[dict] = []
        if method in {"no-knowledge", "explain-fallback"}:
            web_result = self._web_search_and_learn(message)
            if web_result is not None:
                response, web_confidence, web_sources = web_result
                confidence = max(confidence, web_confidence)
                method = "web-search-learning"

        llm_polished = self._llm_polish_answer(
            message=message,
            base_answer=response,
            method=method,
            confidence=confidence,
            search_strategy=search_strategy,
            retrieved_sentences=retrieved,
            c_knowledge=c_knowledge,
            web_sources=web_sources,
        )
        if llm_polished:
            response = llm_polished
            confidence = max(confidence, 0.9 if web_sources else 0.88)
            method = "colibri-hybrid-llm"

        # --- CoT: обновление шагов реальными результатами ---
        if len(thinking_steps) >= 2:
            # Шаг RETRIEVE — реальные данные
            retrieved_count = len(retrieved)
            graph_hits = graph_meta.get("candidates_total", 0)
            self._chain_of_thought.update_step(
                1,
                f"Найдено: {retrieved_count} предложений (BM25), "
                f"{graph_hits} кандидатов (граф), "
                f"C-модель: {'да' if c_knowledge else 'нет'}",
                min(0.9, 0.3 + retrieved_count * 0.1 + (0.2 if c_knowledge else 0)),
            )
        if len(thinking_steps) >= 4:
            # Шаг SYNTHESIZE — реальные данные
            self._chain_of_thought.update_step(
                len(thinking_steps) - 2,
                f"Метод: {method}, уверенность: {confidence:.2f}",
                confidence,
            )
        if len(thinking_steps) >= 5:
            # Шаг VERIFY — верификация качества
            verified = confidence >= 0.5 and method != "no-knowledge"
            self._chain_of_thought.update_step(
                len(thinking_steps) - 1,
                f"{'Ответ верифицирован' if verified else 'Низкая уверенность — возможен fallback'}",
                0.9 if verified else 0.3,
            )
        # Обновляем thinking_text с реальными результатами
        thinking_text = self._chain_of_thought.format_thinking()

        # --- CoT-управляемое обогащение ответа ---
        allow_cot_enrich = method in {"knowledge-graph", "formula-generation"}
        if allow_cot_enrich and (search_strategy.get("use_abstract") or search_strategy.get("use_causal")):
            response = self._cot_enrich_response(
                response, message, confidence, search_strategy,
            )

        # --- Генеративный fallback: если уверенность < 0.3, пробуем FormulaLM ---
        generation_used = False
        if confidence < 0.3 and self._lm_trained:
            generated = self._generate_text(message, max_tokens=64)
            if generated and len(generated) > 10:
                response = generated
                method = "formula-lm-generation"
                confidence = max(confidence, 0.25)
                generation_used = True

        # === Непрерывное обучение в ФОНОВОМ worker-потоке (не блокирует ответ) ===
        try:
            if confidence >= 0.5 and method != "no-knowledge":
                self._train_queue.put_nowait(("retrieval", message, response))
            if c_knowledge:
                self._train_queue.put_nowait(("c_knowledge", message, c_knowledge))
        except queue.Full:
            pass  # Очередь переполнена — пропускаем обучение, не блокируем ответ

        full_response = response

        # --- Контекстное окно: запоминаем ответ ---
        self._context_window.add_message("assistant", full_response)

        formula_hex = best_formula.gene.to_hex()
        conv.add("assistant", full_response, formula_used=formula_hex)

        duration = round((time.time() - start_time) * 1000, 1)

        result = {
            "response": full_response,
            "confidence": confidence,
            "sources": [method],
            "web_sources": web_sources,
            "conversation_id": conv.id,
            "knowledge_hits": graph_meta.get("candidates_total", 0),
            "method": method,
            "duration_ms": duration,
            "model_available": self.c_retriever.available,
            "formula_data": {
                "query_patterns": query_patterns,
                "query_hashes": query_hashes,
                "answer_patterns": graph_meta.get("answer_patterns", {}),
                "formula_predict": formula_result.get("predict_value", 0),
                "formula_genome_hex": formula_hex,
                "formula_fitness": round(best_formula.fitness, 4),
                "formula_generation": self.formula_pool.generation,
                "graph_score": graph_meta.get("total_score", 0),
                "graph_candidates": graph_meta.get("candidates_total", 0),
                "retrieved_sentences": [
                    {"text": t[:150], "score": s}
                    for t, s in retrieved[:3]
                ],
                "formula_generated_words": [
                    {"word": w, "score": round(s, 4)}
                    for w, s in formula_words[:5]
                ],
                "sentence_store_size": self.sentence_store.size,
                "memory_digits": self.sentence_store.memory_digits,
                "embedding_vocab": self.embeddings.vocab_size,
                "embedding_trained_pairs": self.embeddings.trained_pairs,
                "web_learning_used": bool(web_sources),
                "llm_hybrid_used": method == "colibri-hybrid-llm",
                "llm_provider": _TEXT_LLM_PROVIDER,
                "digit_voting": digit_vote,
            },
            "graph_stats": self.graph.get_stats(),
            "thinking": thinking_text,
            "thinking_steps": [
                {"type": s.step_type.name, "content": s.description,
                 "result": s.result, "confidence": s.confidence}
                for s in thinking_steps
            ],
            "generation_used": generation_used,
            "context_stats": self._context_window.get_stats(),
            **self._cognitive_enrichment(message),
        }

        # --- Сохраняем в кэш (вытесняем старые при переполнении) ---
        if len(self._response_cache) >= self._response_cache_max:
            # Удаляем 25% самых старых
            sorted_keys = sorted(
                self._response_cache, key=lambda k: self._response_cache[k][0],
            )
            for k in sorted_keys[: self._response_cache_max // 4]:
                self._response_cache.pop(k, None)
        result["cached"] = False
        self._response_cache[cache_key] = (time.time(), result)

        return result

    def _formula_predict(self, message: str) -> dict:
        h = djb2_hash(message.lower())
        x = float(h % 100000) / 100000.0
        best = self.formula_pool.best()
        predicted = best.predict_numeric(x)
        return {
            "input_hash": h,
            "input_normalized": round(x, 6),
            "predict_value": round(predicted, 4),
            "formula_fitness": round(best.fitness, 4),
            "formula_genome_hex": best.gene.to_hex(),
            "formula_generation": self.formula_pool.generation,
        }

    def _synthesize_response(
        self,
        message: str,
        retrieval_query: str,
        retrieved_sentences: list[tuple[str, float]],
        formula_words: list[tuple[str, float]],
        graph_answer: str,
        graph_confidence: float,
        graph_meta: dict,
        formula_result: dict,
        c_knowledge: list[str],
        assoc_answer: str | None,
    ) -> tuple[str, float, str]:
        """
        Синтез ответа — формулы ГЕНЕРИРУЮТ + РАНЖИРУЮТ.

        Приоритеты:
        1. Формульные ассоциации (точное Q→A через FNV1a хеш)
        2. Гибрид: sentence retrieval + формульная генерация слов
        3. C-модель (.klm бинарь)
        4. Чистая формульная генерация (слова из трансформации паттернов)
        5. Граф слов (fallback)
        """
        # 1. Формульные ассоциации (точное совпадение через хеш)
        if assoc_answer:
            return (assoc_answer, 0.95, "formula-association")

        # 2. Гибрид: sentence retrieval + формульная генерация
        #    СВЯЗНАЯ генерация: склеиваем фрагменты в когерентный ответ
        #    Формула участвует В ОБОИХ процессах:
        #    - Re-ranks предложения через predict(query ⊕ sentence)
        #    - Генерирует слова-подсказки через трансформацию паттернов
        if retrieved_sentences:
            best_text, best_score = retrieved_sentences[0]
            # Адаптивный порог: длинные запросы → ниже порог
            # (cosine-нормализация сильнее разбавляет score при > токенах)
            n_tokens = len(_tokenize(retrieval_query))
            min_threshold = 0.35 if n_tokens <= 3 else 0.20 if n_tokens <= 6 else 0.15
            if best_score >= min_threshold:
                answer = self._build_coherent_response(
                    retrieval_query, retrieved_sentences, formula_words, c_knowledge,
                )
                if answer and self._is_response_coherent(
                    query=retrieval_query,
                    answer=answer,
                    min_overlap=1,
                ):
                    confidence = min(0.95, best_score + 0.2)
                    return (answer, confidence, "formula-retrieval")

        # 3. C-модель (.klm) — связная интеграция
        if c_knowledge:
            clean = [
                k for k in c_knowledge
                if len(k) > 10 and "://" not in k
                and not k.startswith("[") and not k.startswith("(")
            ]
            if clean:
                answer = self._merge_c_knowledge(message, clean)
                if self._is_response_coherent(query=message, answer=answer, min_overlap=1):
                    return (answer, 0.5, "c-model")

        # 4. Чистая формульная генерация
        #    Нет retrieved предложений, но формула ПОРОЖДАЕТ слова
        #    Связная генерация: ищем предложения по формульным словам
        if formula_words and len(formula_words) >= 2:
            words_only = [w for w, s in formula_words if s > 0.2]
            if len(words_only) >= 2:
                fw_query = " ".join(words_only[:5])
                fw_sentences = self.sentence_store.retrieve(
                    query=fw_query, formula=None, top_k=5,
                )
                if fw_sentences and fw_sentences[0][1] >= 0.1:
                    answer = self._build_coherent_response(
                        message, fw_sentences, formula_words, c_knowledge,
                    )
                    if answer and self._is_response_coherent(
                        query=message,
                        answer=answer,
                        min_overlap=1,
                    ):
                        avg_score = sum(s for _, s in formula_words[:5]) / min(5, len(formula_words))
                        return (answer, min(0.7, avg_score + 0.15), "formula-generation")

                # Формульная генерация с контекстными связями
                answer = self._generate_from_formula_words(
                    message, words_only, graph_answer, graph_meta,
                )
                if self._is_response_coherent(query=message, answer=answer, min_overlap=1):
                    avg_score = sum(s for _, s in formula_words[:5]) / min(5, len(formula_words))
                    return (answer, min(0.5, avg_score), "formula-generation")

        # 5. Граф слов (fallback)
        if graph_answer and graph_confidence >= 0.15 and self._is_response_coherent(
            query=message,
            answer=graph_answer,
            min_overlap=1,
        ):
            return (graph_answer, min(0.75, graph_confidence), "knowledge-graph")

        return (
            "У меня пока недостаточно знаний по этой теме. "
            "Обучите меня — отправьте текст или URL для обучения.",
            0.1, "no-knowledge",
        )

    def _is_response_coherent(self, query: str, answer: str, min_overlap: int = 1) -> bool:
        """
        Быстрый quality-gate для отсеивания шумных ответов.
        Не заменяет модельную валидацию, но убирает явный бессвязный текст.
        """
        text = (answer or "").strip()
        if len(text) < 24:
            return False

        ans_tokens = [t for t in _tokenize(text.lower()) if len(t) >= 2]
        if len(ans_tokens) < 5:
            return False

        unique_ratio = len(set(ans_tokens)) / max(1, len(ans_tokens))
        if unique_ratio < 0.28:
            return False

        query_tokens = {
            t for t in _tokenize(query.lower())
            if len(t) >= 3 and not _is_stop_word(t) and t not in _GENERIC_QUERY_TOKENS
        }
        answer_tokens = {t for t in ans_tokens if len(t) >= 3 and not _is_stop_word(t)}
        if query_tokens and min_overlap > 0:
            overlap = len(query_tokens & answer_tokens)
            if overlap < min_overlap:
                query_stems = {_stem_ru(t) for t in query_tokens if len(t) >= 4}
                answer_stems = {_stem_ru(t) for t in answer_tokens if len(t) >= 4}
                stem_overlap = len(query_stems & answer_stems) if query_stems and answer_stems else 0
                if stem_overlap < min_overlap:
                    return False

        # Для русскоязычного запроса отсекаем ответы с чрезмерной долей англ./латиницы.
        has_cyr_query = any("\u0400" <= ch <= "\u04ff" for ch in query)
        if has_cyr_query:
            ascii_tokens = [t for t in ans_tokens if t.isascii()]
            if len(ascii_tokens) / max(1, len(ans_tokens)) > 0.45:
                return False

        return True

    def _has_cyrillic(self, text: str) -> bool:
        return any("\u0400" <= ch <= "\u04ff" for ch in text)

    def _detect_core_topic(self, message: str, mapped_terms: dict[str, str] | None = None) -> str | None:
        lowered = message.lower()
        tokens = _tokenize(lowered)
        stems = {_stem_ru(t) for t in tokens if len(t) >= 3}
        mapped_values = set((mapped_terms or {}).values())

        has_qubit_token = "qubit" in tokens or "кубит" in tokens
        has_quantum = any(s.startswith("квант") for s in stems) or "quantum" in tokens
        has_bit = "бит" in tokens or "bit" in tokens
        mapped_qubit = "qubit" in mapped_values
        if has_qubit_token or mapped_qubit or (has_quantum and has_bit):
            return "qubit"

        has_transformer = (
            any(s.startswith("трансформ") for s in stems)
            or "transformer" in tokens
            or "transformer" in mapped_values
        )
        if has_transformer:
            return "transformer"

        has_entropy = (
            any(s.startswith("энтроп") for s in stems)
            or "entropy" in tokens
            or "entropy" in mapped_values
        )
        if has_entropy:
            return "entropy"

        return None

    def _topic_fallback_answer(self, topic: str) -> str:
        return _TOPIC_FALLBACKS.get(
            topic,
            "Не могу дать надёжное объяснение по этому запросу без дополнительного источника.",
        )

    def _build_explain_fallback(self, message: str) -> str:
        return (
            "Не могу дать надёжное объяснение по этому запросу без дополнительного источника. "
            "Если отправите короткий текст или ссылку по теме, отвечу точнее и без догадок."
        )

    def _fetch_weather(self, location: str) -> tuple[str, float, str]:
        """Получить погоду через Open-Meteo. Возвращает (response, confidence, method)."""
        if not _WEATHER_ENABLED:
            return ("Погода сейчас недоступна на этом сервере.", 0.1, "weather-unavailable")
        try:
            geo = requests.get(
                _WEATHER_GEO_URL,
                params={"name": location, "count": 1, "language": "ru", "format": "json"},
                headers={"User-Agent": "KolibriBot/1.0 (weather)"},
                timeout=6,
            )
            geo.raise_for_status()
            geo_data = geo.json()
            results = geo_data.get("results") or []
            if not results:
                return ("Не нашёл такой населённый пункт. Уточните город.", 0.2, "weather-clarify")
            loc = results[0]
            lat = loc.get("latitude")
            lon = loc.get("longitude")
            name = loc.get("name") or location
            country = loc.get("country") or ""
            region = loc.get("admin1") or ""
            if lat is None or lon is None:
                return ("Не смог получить координаты этого места.", 0.2, "weather-unavailable")

            forecast = requests.get(
                _WEATHER_FORECAST_URL,
                params={
                    "latitude": lat,
                    "longitude": lon,
                    "current_weather": "true",
                    "daily": "temperature_2m_max,temperature_2m_min,precipitation_probability_max",
                    "timezone": "auto",
                },
                headers={"User-Agent": "KolibriBot/1.0 (weather)"},
                timeout=6,
            )
            forecast.raise_for_status()
            data = forecast.json()

            current = data.get("current_weather") or {}
            daily = data.get("daily") or {}
            temp = current.get("temperature")
            wind = current.get("windspeed")
            code = current.get("weathercode")

            tmax = None
            tmin = None
            rain = None
            if isinstance(daily, dict):
                tmax_list = daily.get("temperature_2m_max") or []
                tmin_list = daily.get("temperature_2m_min") or []
                rain_list = daily.get("precipitation_probability_max") or []
                if tmax_list:
                    tmax = tmax_list[0]
                if tmin_list:
                    tmin = tmin_list[0]
                if rain_list:
                    rain = rain_list[0]

            place = ", ".join([p for p in [name, region, country] if p])
            parts: list[str] = []
            if temp is not None:
                parts.append(f"Сейчас: {temp} °C")
            if code is not None:
                parts.append(_weather_code_label(int(code)))
            if wind is not None:
                parts.append(f"ветер {wind} км/ч")
            line_now = ", ".join(parts) if parts else "Сейчас: нет данных."

            line_day = ""
            if tmin is not None and tmax is not None:
                line_day = f"Сегодня: от {tmin} до {tmax} °C"
                if rain is not None:
                    line_day += f", осадки до {rain}%"
            elif rain is not None:
                line_day = f"Сегодня: вероятность осадков до {rain}%"

            response = f"{place}\n{line_now}"
            if line_day:
                response += f". {line_day}."
            return (response, 0.85, "weather-open-meteo")
        except Exception:
            return ("Сейчас не могу получить погоду из внешнего источника.", 0.1, "weather-unavailable")

    def _quick_search_duckduckgo(self, query: str, max_results: int = 4) -> list[dict]:
        results: list[dict] = []
        try:
            resp = requests.post(
                "https://html.duckduckgo.com/html/",
                data={"q": query, "b": ""},
                headers=_WEB_HEADERS,
                timeout=6,
            )
            resp.raise_for_status()
            soup = BeautifulSoup(resp.text, "html.parser")
            for result_div in soup.select(".result"):
                a_tag = result_div.select_one("a.result__a")
                if not a_tag:
                    continue
                href = a_tag.get("href", "").strip()
                title = a_tag.get_text(strip=True)
                actual_url = href
                if "uddg=" in href:
                    parsed = urlparse(href)
                    params = parse_qs(parsed.query)
                    uddg = params.get("uddg", [""])[0]
                    if uddg:
                        actual_url = unquote(uddg)
                if not actual_url.startswith(("http://", "https://")):
                    continue
                snippet_tag = result_div.select_one(".result__snippet")
                snippet = snippet_tag.get_text(strip=True) if snippet_tag else ""
                results.append({
                    "url": actual_url,
                    "title": title,
                    "snippet": snippet[:240],
                    "source": "duckduckgo",
                })
                if len(results) >= max_results:
                    break
        except Exception:
            return []
        return results

    def _quick_search_wikipedia(self, query: str, lang: str = "ru", max_results: int = 3) -> list[dict]:
        results: list[dict] = []
        try:
            api_url = f"https://{lang}.wikipedia.org/w/api.php"
            resp = requests.get(
                api_url,
                params={
                    "action": "query",
                    "list": "search",
                    "srsearch": query,
                    "srlimit": max_results,
                    "format": "json",
                    "utf8": 1,
                },
                headers={
                    "User-Agent": "KolibriBot/1.0 (knowledge retrieval)",
                    "Accept": "application/json",
                },
                timeout=5,
            )
            resp.raise_for_status()
            data = resp.json()
            for item in data.get("query", {}).get("search", []):
                title = item.get("title", "").strip()
                if not title:
                    continue
                page_url = f"https://{lang}.wikipedia.org/wiki/{quote_plus(title.replace(' ', '_'))}"
                snippet = BeautifulSoup(item.get("snippet", ""), "html.parser").get_text()
                results.append({
                    "url": page_url,
                    "title": title,
                    "snippet": snippet[:240],
                    "source": f"wikipedia_{lang}",
                })
        except Exception:
            return []
        return results

    def _fetch_web_page_text(self, url: str, timeout: int = _WEB_PAGE_TIMEOUT) -> tuple[str, str]:
        """Загрузить страницу и извлечь очищенный текст."""
        try:
            resp = requests.get(
                url,
                headers=_WEB_HEADERS,
                timeout=timeout,
                allow_redirects=True,
            )
            resp.raise_for_status()
        except Exception:
            return ("", "")

        content_type = (resp.headers.get("content-type") or "").lower()
        if "text/html" not in content_type:
            return ("", "")

        if not resp.encoding or resp.encoding.lower() in {"iso-8859-1", "ascii"}:
            resp.encoding = resp.apparent_encoding or "utf-8"

        soup = BeautifulSoup(resp.text, "html.parser")
        title_tag = soup.find("title")
        title = title_tag.get_text(strip=True) if title_tag else url

        for tag in soup([
            "script", "style", "nav", "footer", "header", "aside", "noscript",
            "iframe", "form", "button", "svg", "img", "video", "audio",
            "figure", "input", "select", "textarea",
        ]):
            tag.decompose()

        main_content = (
            soup.find("article")
            or soup.find("main")
            or soup.find(class_=re.compile(r"content|article|post|entry|text", re.I))
            or soup.find("div", {"id": re.compile(r"content|article|main|body", re.I)})
            or soup.find("body")
        )

        text_raw = (
            main_content.get_text(separator="\n", strip=True)
            if main_content else soup.get_text(separator="\n", strip=True)
        )

        lines: list[str] = []
        seen: set[str] = set()
        total_chars = 0
        for line in text_raw.split("\n"):
            clean = re.sub(r"\s+", " ", line).strip()
            if len(clean) < 40:
                continue
            low = clean.lower()
            if low in seen:
                continue
            if any(bad in low for bad in ("cookie", "privacy policy", "accept all", "all rights reserved")):
                continue
            seen.add(low)
            lines.append(clean)
            total_chars += len(clean)
            if total_chars >= 18000:
                break

        if not lines:
            return (title, "")
        return (title, "\n".join(lines))

    def _build_web_answer_from_docs(self, query: str, docs: list[dict]) -> str:
        """Собрать краткий ответ из найденных веб-документов."""
        query_tokens = {
            t for t in _tokenize(query.lower())
            if len(t) >= 3 and not _is_stop_word(t) and t not in _GENERIC_QUERY_TOKENS
        }
        query_stems = {_stem_ru(t) for t in query_tokens if len(t) >= 4}

        candidates: list[tuple[float, str]] = []
        for doc in docs:
            text = doc.get("text", "")
            if not text:
                continue
            sentences = re.split(r"(?<=[.!?])\s+|\n+", text)
            for sent in sentences:
                clean = sent.strip(" \t\r\n-•")
                if len(clean) < 40 or len(clean) > 360:
                    continue
                tokens = {
                    t for t in _tokenize(clean.lower())
                    if len(t) >= 3 and not _is_stop_word(t)
                }
                if not tokens:
                    continue
                overlap = len(query_tokens & tokens) if query_tokens else 0
                if overlap == 0 and query_stems:
                    sent_stems = {_stem_ru(t) for t in tokens if len(t) >= 4}
                    overlap = len(query_stems & sent_stems)
                if query_tokens and overlap == 0:
                    continue
                score = overlap * 2.0 + min(1.0, len(tokens) / 24.0)
                candidates.append((score, clean))

        if not candidates:
            return ""

        candidates.sort(key=lambda x: x[0], reverse=True)
        selected: list[str] = []
        seen_prefix: set[str] = set()
        for _, sentence in candidates:
            pref = sentence[:80].lower()
            if pref in seen_prefix:
                continue
            seen_prefix.add(pref)
            selected.append(sentence.rstrip(" ."))
            if len(selected) >= 3:
                break

        if not selected:
            return ""

        answer = ". ".join(selected)
        if not answer.endswith((".", "!", "?")):
            answer += "."
        return answer

    def _persist_web_learning(self, query: str, training_text: str, sources: list[dict]) -> None:
        """Сохранить найденные веб-знания в corpus для загрузки после рестарта."""
        try:
            _CORPUS_DIR.mkdir(parents=True, exist_ok=True)
            q_hash = hashlib.sha1(query.lower().encode("utf-8")).hexdigest()[:12]
            path = _CORPUS_DIR / f"web_auto_{q_hash}.txt"
            # Пишем в corpus только обучающий контент (без мета-шапок),
            # чтобы retrieval не начинал отвечать комментариями.
            path.write_text(training_text[:_WEB_MAX_TRAIN_CHARS], encoding="utf-8")
        except Exception as exc:
            log.warning("web learning persist failed: %s", exc)

    def _web_search_and_learn(self, query: str) -> tuple[str, float, list[dict]] | None:
        """Если знаний не хватает: ищем в web, отвечаем и дообучаемся на найденном."""
        if not _WEB_FALLBACK:
            return None

        deadline = time.monotonic() + max(6, _WEB_TOTAL_TIMEOUT)
        target_urls = max(3, _WEB_MAX_URLS)
        search_results: list[dict] = []
        seen_urls: set[str] = set()

        handlers = [
            ("duckduckgo", lambda q: self._quick_search_duckduckgo(q, max_results=target_urls)),
            ("wikipedia_ru", lambda q: self._quick_search_wikipedia(q, max_results=3, lang="ru")),
            ("wikipedia_en", lambda q: self._quick_search_wikipedia(q, max_results=3, lang="en")),
        ]

        for _, handler in handlers:
            if len(search_results) >= target_urls:
                break
            if time.monotonic() >= deadline:
                break
            try:
                candidates = handler(query)
            except Exception:
                candidates = []
            for item in candidates:
                url = (item.get("url") or "").strip()
                if not url:
                    continue
                key = re.sub(r"[?#].*$", "", url.rstrip("/").lower())
                if key in seen_urls:
                    continue
                seen_urls.add(key)
                search_results.append(item)
                if len(search_results) >= target_urls:
                    break

        if not search_results:
            return None

        docs: list[dict] = []
        for result in search_results[: max(3, _WEB_MAX_URLS)]:
            if time.monotonic() >= deadline:
                break
            url = result.get("url", "")
            if not url:
                continue
            title, text = self._fetch_web_page_text(url=url, timeout=_WEB_PAGE_TIMEOUT)
            if len(text) < 250:
                snippet = (result.get("snippet") or "").strip()
                if len(snippet) < 40:
                    continue
                text = f"{result.get('title', '')}. {snippet}"
                if not title:
                    title = result.get("title", "") or url
            docs.append({
                "url": url,
                "title": title or result.get("title", ""),
                "source": result.get("source", ""),
                "text": text[:40000],
            })
            if len(docs) >= max(1, _WEB_MAX_PAGES):
                break

        if not docs:
            return None

        train_chunks: list[str] = []
        for doc in docs:
            train_chunks.append(
                f"{doc.get('title', '')}\n"
                f"Источник: {doc.get('url', '')}\n"
                f"{doc.get('text', '')}"
            )
        train_text = "\n\n".join(train_chunks)[:_WEB_MAX_TRAIN_CHARS]

        if len(train_text) >= 120:
            try:
                src_meta = [
                    {
                        "url": d.get("url", ""),
                        "title": d.get("title", ""),
                        "source": d.get("source", ""),
                    }
                    for d in docs[:5]
                ]
                self._train_queue.put_nowait(("web_train", query, train_text, src_meta))
            except Exception as exc:
                log.warning("web learning enqueue failed: %s", exc)

        answer = self._build_web_answer_from_docs(query=query, docs=docs)
        if not answer:
            return None
        if not self._is_response_coherent(query=query, answer=answer, min_overlap=0):
            return None

        source_list: list[dict] = []
        seen_urls: set[str] = set()
        for doc in docs:
            url = doc.get("url", "")
            if not url or url in seen_urls:
                continue
            seen_urls.add(url)
            source_list.append({
                "url": url,
                "title": doc.get("title", ""),
                "source": doc.get("source", ""),
                "host": urlparse(url).netloc.lower(),
            })
            if len(source_list) >= 3:
                break

        if source_list:
            hosts = ", ".join(s.get("host") or s.get("url") for s in source_list[:2])
            answer = f"{answer}\n\nИсточники: {hosts}"

        confidence = 0.82 if len(source_list) >= 2 else 0.74
        return (answer, confidence, source_list)

    def _extract_gemini_text(self, payload: dict) -> str:
        candidates = payload.get("candidates")
        if not isinstance(candidates, list):
            return ""
        parts_out: list[str] = []
        for cand in candidates:
            if not isinstance(cand, dict):
                continue
            content = cand.get("content")
            if not isinstance(content, dict):
                continue
            parts = content.get("parts")
            if not isinstance(parts, list):
                continue
            for part in parts:
                if isinstance(part, dict) and isinstance(part.get("text"), str):
                    parts_out.append(part["text"])
        return "\n".join(p.strip() for p in parts_out if p and p.strip()).strip()

    def _extract_openrouter_text(self, payload: dict) -> str:
        choices = payload.get("choices")
        if not isinstance(choices, list) or not choices:
            return ""
        first = choices[0]
        if not isinstance(first, dict):
            return ""
        message = first.get("message")
        if not isinstance(message, dict):
            return ""
        content = message.get("content")
        if isinstance(content, str):
            return content.strip()
        if isinstance(content, list):
            chunks: list[str] = []
            for item in content:
                if isinstance(item, dict) and item.get("type") == "text" and isinstance(item.get("text"), str):
                    chunks.append(item["text"])
            return "\n".join(c.strip() for c in chunks if c and c.strip()).strip()
        return ""

    def _sanitize_llm_prompt(self, text: str, max_chars: int = 7000) -> str:
        cleaned = (text or "").replace("\r", "\n")
        cleaned = re.sub(r"[\x00-\x08\x0B\x0C\x0E-\x1F]", " ", cleaned)
        cleaned = re.sub(r"\n{3,}", "\n\n", cleaned)
        cleaned = re.sub(r"[ \t]{2,}", " ", cleaned)
        cleaned = cleaned.strip()
        if len(cleaned) > max_chars:
            cleaned = cleaned[:max_chars].rsplit(" ", 1)[0].rstrip() + "…"
        return cleaned

    def _call_text_llm(self, system_prompt: str, user_prompt: str) -> str:
        global _TEXT_LLM_RUNTIME_DISABLED
        if _TEXT_LLM_RUNTIME_DISABLED:
            return ""
        if not _TEXT_LLM_ENABLE or not _TEXT_LLM_API_KEY or not _TEXT_LLM_PROVIDER:
            return ""
        try:
            if _TEXT_LLM_PROVIDER == "gemini":
                merged_prompt = self._sanitize_llm_prompt(
                    f"{system_prompt}\n\n{user_prompt}",
                    max_chars=7000,
                )
                payload = {
                    "contents": [
                        {"role": "user", "parts": [{"text": merged_prompt}]},
                    ],
                    "generationConfig": {
                        "temperature": max(0.0, min(1.0, _TEXT_LLM_TEMPERATURE)),
                        "maxOutputTokens": max(128, min(2048, _TEXT_LLM_MAX_TOKENS)),
                    },
                }
                url = f"{_TEXT_GEMINI_BASE_URL}/models/{_TEXT_GEMINI_MODEL}:generateContent"
                resp = requests.post(
                    f"{url}?key={_TEXT_LLM_API_KEY}",
                    json=payload,
                    timeout=max(5.0, _TEXT_LLM_TIMEOUT),
                )
                if resp.status_code >= 400:
                    err_text = (resp.text or "")[:400]
                    log.warning("text llm gemini error %s: %s", resp.status_code, err_text)
                    if "location is not supported" in err_text.lower():
                        _TEXT_LLM_RUNTIME_DISABLED = True
                        log.warning("text llm disabled for runtime due to provider geo restriction")
                    return ""
                return self._extract_gemini_text(resp.json())

            if _TEXT_LLM_PROVIDER == "openrouter":
                system_prompt = self._sanitize_llm_prompt(system_prompt, max_chars=2000)
                user_prompt = self._sanitize_llm_prompt(user_prompt, max_chars=6000)
                payload = {
                    "model": _TEXT_OPENROUTER_MODEL,
                    "messages": [
                        {"role": "system", "content": system_prompt},
                        {"role": "user", "content": user_prompt},
                    ],
                    "temperature": max(0.0, min(1.5, _TEXT_LLM_TEMPERATURE)),
                    "max_tokens": max(128, min(2048, _TEXT_LLM_MAX_TOKENS)),
                }
                headers = {
                    "Authorization": f"Bearer {_TEXT_LLM_API_KEY}",
                    "Content-Type": "application/json",
                    "HTTP-Referer": "https://kolibriai.ru",
                    "X-Title": "Kolibri AI",
                }
                resp = requests.post(
                    _TEXT_OPENROUTER_URL,
                    json=payload,
                    headers=headers,
                    timeout=max(5.0, _TEXT_LLM_TIMEOUT),
                )
                if resp.status_code >= 400:
                    err_text = (resp.text or "")[:400]
                    log.warning("text llm openrouter error %s: %s", resp.status_code, err_text)
                    return ""
                return self._extract_openrouter_text(resp.json())
        except Exception as exc:
            log.warning("text llm call failed: %s", exc)
        return ""

    def _should_use_llm_polish(
        self,
        message: str,
        method: str,
        confidence: float,
        search_strategy: dict,
    ) -> bool:
        if not _TEXT_LLM_ENABLE or not _TEXT_LLM_API_KEY or not _TEXT_LLM_PROVIDER:
            return False
        if method in {"greeting", "command", "math-eval"}:
            return False
        tokens = [t for t in _tokenize(message.lower()) if len(t) >= 2]
        if len(tokens) < 3:
            return False
        intent = search_strategy.get("intent", "general")
        if intent in {"explain", "compare", "create"}:
            return True
        if method in {"web-search-learning", "topic-fallback", "no-knowledge", "explain-fallback"}:
            return True
        if method in {"formula-generation", "knowledge-graph", "c-model"}:
            return True
        if method in {"formula-retrieval", "formula-association"} and (confidence < 0.92 or len(tokens) >= 7):
            return True
        return False

    def _llm_polish_answer(
        self,
        message: str,
        base_answer: str,
        method: str,
        confidence: float,
        search_strategy: dict,
        retrieved_sentences: list[tuple[str, float]],
        c_knowledge: list[str],
        web_sources: list[dict],
    ) -> str:
        if not self._should_use_llm_polish(
            message=message,
            method=method,
            confidence=confidence,
            search_strategy=search_strategy,
        ):
            return ""

        retr_lines = [
            f"- ({round(score, 3)}) {text[:220]}"
            for text, score in retrieved_sentences[:5]
            if text and len(text) >= 20
        ]
        c_lines = [f"- {k[:220]}" for k in c_knowledge[:4] if k and len(k) >= 20]
        web_lines = [
            f"- {item.get('title', '')} | {item.get('url', '')}"
            for item in web_sources[:4]
            if item.get("url")
        ]

        system_prompt = (
            "Ты — финальный редактор ответов Colibri AI. "
            "Нужно улучшить ясность и точность, не выдумывать факты. "
            "Используй ТОЛЬКО переданный контекст. "
            "Если контекста мало, честно укажи это."
        )
        user_prompt = (
            f"Запрос пользователя:\n{message}\n\n"
            f"Черновой ответ Colibri:\n{base_answer}\n\n"
            f"Контекст retrieval:\n{chr(10).join(retr_lines) if retr_lines else '- нет'}\n\n"
            f"Контекст C-модели:\n{chr(10).join(c_lines) if c_lines else '- нет'}\n\n"
            f"Веб-источники:\n{chr(10).join(web_lines) if web_lines else '- нет'}\n\n"
            "Сформируй итоговый ответ на русском, 1-3 абзаца, без воды. "
            "Если есть веб-источники, добавь в конце строку вида: "
            "\"Источники: host1, host2\"."
        )
        llm_text = self._call_text_llm(system_prompt=system_prompt, user_prompt=user_prompt)
        if not llm_text:
            return ""

        cleaned = llm_text.strip()
        cleaned = re.sub(r"^```[a-zA-Z0-9_-]*\n?", "", cleaned)
        cleaned = re.sub(r"\n?```$", "", cleaned).strip()
        if len(cleaned) > 3200:
            cleaned = cleaned[:3200].rsplit(" ", 1)[0].rstrip() + "…"

        # Для LLM-полировки достаточно базовой связности: он может перефразировать
        # без буквального совпадения токенов с запросом.
        if not self._is_response_coherent(query=message, answer=cleaned, min_overlap=0):
            return ""
        return cleaned

    # ------------------------------------------------------------------
    # Связная генерация: когерентные ответы вместо склейки фрагментов
    # ------------------------------------------------------------------

    def _build_coherent_response(
        self,
        query: str,
        sentences: list[tuple[str, float]],
        formula_words: list[tuple[str, float]],
        c_knowledge: list[str],
    ) -> str:
        """
        Связная генерация ответа из найденных фрагментов.

        Вместо простой склейки ". ".join():
        1. Ранжирование по релевантности к запросу
        2. Удаление дублирующей информации
        3. Логическое упорядочивание (от общего к частному)
        4. Добавление связующих конструкций
        5. Интеграция формульных слов как контекстных подсказок
        """
        query_tokens = set(_tokenize(query.lower()))
        # Значимые токены (без стоп-слов) для оценки релевантности
        meaningful_query = {
            t for t in query_tokens
            if not _is_stop_word(t) and t not in _GENERIC_QUERY_TOKENS
        }
        # --- Стемы для морфологического совпадения («искусственном» ≈ «искусственный») ---
        meaningful_stems = {_stem_ru(t) for t in meaningful_query if len(t) >= 4}
        scored_sentences: list[tuple[str, float, int]] = []

        # Семантический вектор запроса (для embedding-ранжирования)
        query_vec = None
        use_embeddings = len(self.embeddings.vectors) > 100
        strict_lexical_match = any("\u0400" <= ch <= "\u04ff" for ch in query)
        if use_embeddings:
            query_vec = self.embeddings.sentence_vector(query)

        # Шаг 1: Ранжируем и дедуплицируем
        seen_content: list[str] = []  # Полные тексты для near-duplicate check
        for text, base_score in sentences[:8]:
            text = text.strip()
            if not text or len(text) < 15:
                continue
            # Не подмешиваем self-referential текст о Kolibri,
            # если пользователь не спрашивал именно о Kolibri.
            lower_text = text.lower()
            lower_query = query.lower()
            if (
                ("kolibri" in lower_text or "колибри" in lower_text)
                and ("kolibri" not in lower_query and "колибри" not in lower_query)
            ):
                continue
            # Проверка на near-duplicate: не добавлять предложения с >60% пересечением слов
            text_words = set(_tokenize(text.lower()))
            meaningful_tw = {t for t in text_words if not _is_stop_word(t) and len(t) >= 3}
            is_dup = False
            for seen_text in seen_content:
                seen_words = set(_tokenize(seen_text.lower()))
                meaningful_sw = {t for t in seen_words if not _is_stop_word(t) and len(t) >= 3}
                if meaningful_sw and meaningful_tw:
                    common = len(meaningful_tw & meaningful_sw)
                    ratio = common / min(len(meaningful_tw), len(meaningful_sw))
                    if ratio > 0.6:
                        is_dup = True
                        break
            if is_dup:
                continue
            seen_content.append(text)

            meaningful_text = {t for t in text_words if not _is_stop_word(t)}
            # Пересечение по точным словам
            overlap = len(meaningful_query & meaningful_text)
            # Морфологическое совпадение через стемминг
            if overlap == 0 and meaningful_stems:
                text_stems = {_stem_ru(t) for t in meaningful_text if len(t) >= 4}
                stem_overlap = len(meaningful_stems & text_stems)
                overlap = stem_overlap  # Стемы работают как fallback

            # Семантическое сходство через эмбеддинги: boost даже без overlap слов
            emb_sim = 0.0
            if query_vec is not None:
                sent_vec = self.embeddings.sentence_vector(text)
                if sent_vec is not None and query_vec is not None:
                    dot = sum(a * b for a, b in zip(query_vec, sent_vec))
                    emb_sim = max(0.0, dot)  # cosine (уже L2-нормализованы)

            # Адаптивный порог: если embedding sim высокий, пропускаем overlap-check
            min_overlap = 2 if len(meaningful_query) >= 3 else 1
            if meaningful_query and overlap < min_overlap:
                # Для русскоязычных запросов требуем явное лексическое совпадение:
                # эмбеддинги здесь чаще дают шумные "псевдо-сходства".
                if strict_lexical_match:
                    continue
                if emb_sim < 0.35:
                    continue
            relevance = base_score + overlap * 0.15 + emb_sim * 0.25
            len_bonus = min(1.0, len(text) / 200) * 0.1
            if len(text) > 300:
                len_bonus -= 0.05
            scored_sentences.append((text, relevance + len_bonus, len(text)))

        if not scored_sentences:
            return ""

        # Шаг 2: Сортировка — самое релевантное первым
        scored_sentences.sort(key=lambda x: x[1], reverse=True)

        # Шаг 3: Отбираем до 4 фрагментов
        selected = scored_sentences[:4]
        if len(selected) > 1:
            # Предпочитаем начинать с полноценного предложения, не со списка
            main_idx = 0
            for j, (txt, sc, _) in enumerate(selected):
                if not txt.lstrip().startswith(("-", "•", "–", "—")):
                    main_idx = j
                    break
            if main_idx > 0:
                selected[0], selected[main_idx] = selected[main_idx], selected[0]
            main = selected[0]
            rest = sorted(selected[1:], key=lambda x: x[2])
            selected = [main] + rest

        # Шаг 4: Очистка и склеивание
        parts: list[str] = []
        for i, (text, score, _) in enumerate(selected):
            # Очистка предложения
            text = text.strip().rstrip(" .")
            # Убираем ведущие маркеры списка
            text = text.lstrip("-•–— ")
            # Отбрасываем обрезанные предложения (заканчиваются на «(т.е.», «(т.»)
            if text.endswith(("(т.е", "(т.", "(напр", "(т")):
                text = text[:text.rfind("(")].rstrip(" ,")
            # Отбрасываем слишком короткие после очистки
            if len(text) < 15:
                continue
            
            if i == 0:
                parts.append(text)
            else:
                curr_meaningful = {
                    t for t in _tokenize(text.lower())
                    if not _is_stop_word(t) and len(t) >= 3
                }
                q_overlap = len(meaningful_query & curr_meaningful)
                if q_overlap == 0 and meaningful_stems:
                    curr_stems = {_stem_ru(t) for t in curr_meaningful if len(t) >= 4}
                    q_overlap = len(meaningful_stems & curr_stems)
                if meaningful_query and q_overlap == 0:
                    continue

                prev_tokens = set(_tokenize(parts[-1].lower()))
                curr_tokens = set(_tokenize(text.lower()))
                new_info = len(curr_tokens - prev_tokens)
                if new_info < 2:
                    continue
                if score > 0.5:
                    parts.append(text)
                elif i == 1 and len(text) > 1:
                    parts.append(f"Кроме того, {text[0].lower()}{text[1:]}")
                elif len(text) > 1:
                    parts.append(f"Также {text[0].lower()}{text[1:]}")
                else:
                    parts.append(text)

        answer = ". ".join(parts)
        # Очистка артефактов склейки
        answer = answer.replace(":.", ":").replace(".. ", ". ").replace("..", ".")
        if not answer.endswith((".", "!", "?")):
            answer += "."

        # Шаг 5: Формульные слова — пока только во внутренней аналитике
        # (formula_data.formula_generated_words в JSON ответе).
        # Показываем только если формула достаточно обучена
        # и слова семантически пересекаются с запросом.
        if formula_words and self.formula_pool.generation >= 2000:
            query_stems = {_stem_ru(t) for t in _tokenize(query.lower()) if len(t) >= 4 and not _is_stop_word(t)}
            answer_stems = {_stem_ru(t) for t in _tokenize(answer.lower()) if len(t) >= 4}
            fw_unique = []
            for w, s in formula_words[:8]:
                if s <= 1.0:
                    continue
                wl = w.lower()
                if _is_stop_word(wl) or len(wl) < 4:
                    continue
                if any('\u0400' <= c <= '\u04ff' for c in query) and wl.isascii():
                    continue
                ws = _stem_ru(wl)
                if ws in answer_stems or ws in query_stems:
                    continue
                fw_unique.append(wl)
                if len(fw_unique) >= 3:
                    break
            if len(fw_unique) >= 2:
                hint = ", ".join(fw_unique)
                answer += f" Связанные понятия: {hint}."

        return answer

    def _generate_from_formula_words(
        self,
        query: str,
        formula_words: list[str],
        graph_answer: str,
        graph_meta: dict,
    ) -> str:
        """Генерация связного ответа из формульных слов."""
        context_phrases: list[str] = []
        for fw in formula_words[:5]:
            neighbors = self.graph.find_similar(fw, limit=3)
            if neighbors:
                neighbor_words = [w for w, _ in neighbors]
                context_phrases.append(f"{fw} ({', '.join(neighbor_words[:2])})")
            else:
                context_phrases.append(fw)

        if graph_answer and len(graph_answer) > 20:
            return (
                f"{graph_answer} "
                f"Числовой анализ также выявляет связи с: {', '.join(context_phrases[:4])}."
            )
        return (
            f"По вашему запросу числовое мышление выявило ключевые понятия: "
            f"{', '.join(context_phrases[:6])}. "
            f"Каждое слово закодировано в 64-цифровой паттерн, связи определены "
            f"через формульную трансформацию."
        )

    def _merge_c_knowledge(self, query: str, knowledge: list[str]) -> str:
        """Связная интеграция знаний из C-модели."""
        if len(knowledge) == 1:
            return knowledge[0] if knowledge[0].endswith((".", "!", "?")) else knowledge[0] + "."
        unique: list[str] = []
        seen: set[str] = set()
        for k in knowledge[:5]:
            key = k[:30].lower()
            if key not in seen:
                unique.append(k.strip().rstrip("."))
                seen.add(key)
        if len(unique) == 1:
            return unique[0] + "."
        result = unique[0]
        for i, part in enumerate(unique[1:], 1):
            if i == 1:
                result += f". {part}"
            elif len(part) > 1:
                result += f". Кроме того, {part[0].lower()}{part[1:]}"
            else:
                result += f". {part}"
        if not result.endswith((".", "!", "?")):
            result += "."
        return result

    def _format_numeric_data(
        self,
        query_patterns: dict[str, str],
        graph_meta: dict,
        formula_result: dict,
        formula_words: list[tuple[str, float]] | None = None,
    ) -> str:
        sections: list[str] = []

        if query_patterns:
            lines = ["🔢 **Числовые паттерны запроса:**"]
            for word, pattern in list(query_patterns.items())[:5]:
                h = djb2_hash(word)
                lines.append(f"  `{word}` → `{pattern[:32]}…` (hash: {h})")
            sections.append("\n".join(lines))

        answer_patterns = graph_meta.get("answer_patterns", {})
        if answer_patterns:
            lines = ["🧬 **Паттерны ответа:**"]
            for word, pattern in list(answer_patterns.items())[:5]:
                lines.append(f"  `{word}` → `{pattern[:32]}…`")
            sections.append("\n".join(lines))

        if formula_result:
            genome_hex = formula_result.get("formula_genome_hex", "")[:32]
            sections.append(
                f"⚡ **Формула:** predict={formula_result.get('predict_value', 0)} "
                f"| fitness={formula_result.get('formula_fitness', 0)} "
                f"| gen={formula_result.get('formula_generation', 0)} "
                f"| genome=`{genome_hex}…`"
            )

        cands = graph_meta.get("candidates_total", 0)
        score = graph_meta.get("total_score", 0)
        if cands > 0:
            sections.append(f"📊 **Граф знаний:** {cands} кандидатов, score={score}")

        return "\n".join(sections)

    def _digit_vote(
        self,
        message: str,
        retrieved_sentences: list[tuple[str, float]],
        graph_confidence: float,
        c_knowledge: list[str],
        c_digits: list[int],
        formula_result: dict,
    ) -> dict:
        """0..9 voting layer for canonical chat decisions."""
        digits = c_digits[:200]
        if not digits:
            try:
                digits = text_to_digits(message)[:200]
            except Exception:
                digits = []
        counts = Counter(digits) if digits else Counter()
        total = sum(counts.values()) or 1
        strength = {d: round(counts.get(d, 0) / total, 4) for d in range(10)}

        low_signal = not retrieved_sentences and not c_knowledge and graph_confidence < 0.2
        has_math = self._try_math_eval(message.strip().lower()) is not None
        has_digits = any(c.isdigit() for c in message)
        has_structure = any(t in message.lower() for t in ("структур", "как устроен", "как устроено", "схема"))
        has_causal = any(t in message.lower() for t in ("почему", "из-за", "причин", "следств"))
        has_tool = _match_weather_intent(message) or "http://" in message or "https://" in message

        base = {
            0: 0.7 if low_signal else 0.15,
            1: 0.6 if (retrieved_sentences or c_knowledge) else 0.25,
            2: 0.6 if has_structure else 0.2,
            3: 0.6 if has_causal else 0.2,
            4: 0.8 if (has_math or has_digits) else 0.2,
            5: 0.6 if (retrieved_sentences or graph_confidence >= 0.4) else 0.2,
            6: 0.7 if c_knowledge else 0.25,
            7: 0.5 if low_signal else 0.2,
            8: 0.7 if has_tool else 0.15,
            9: 0.6 if (retrieved_sentences and c_knowledge) else 0.3,
        }

        channels = {d: round(min(1.0, base[d] + strength.get(d, 0)), 4) for d in range(10)}
        reject = channels[0] > 0.7 and channels[9] < 0.45
        needs_tool = channels[8] > 0.6
        boost = min(0.12, channels[9] * 0.12)

        return {
            "channels": channels,
            "strength": strength,
            "decision": {
                "reject": reject,
                "needs_tool": needs_tool,
                "confidence_boost": round(boost, 4),
                "low_signal": low_signal,
            },
            "input_digits": len(digits),
            "formula_predict": formula_result.get("predict_value", 0),
        }

    # ------------------------------------------------------------------
    # Обучение
    # ------------------------------------------------------------------

    def _save_formulas(self) -> None:
        """Сохранить формулы на диск (атомарная запись)."""
        try:
            self.formula_pool.save(_FORMULA_SAVE_PATH)
        except Exception as e:
            log.warning("Не удалось сохранить формулы: %s", e)

    def train_text(self, text: str) -> dict:
        """Обучить на тексте — реально обновляет числовой граф + предложения + эмбеддинги."""
        result = self.graph.train_text(text)
        self.sentence_store.add_text(text)
        self._train_formulas_from_graph()
        # Инкрементальное обучение эмбеддингов на новых рёбрах
        tokens = _tokenize(text)
        if tokens:
            new_edges: list[tuple[int, int, float]] = []
            for i, t in enumerate(tokens):
                if len(t) < 2:
                    continue
                h_t = djb2_hash(t.lower())
                self.embeddings.get_or_create(h_t, t.lower())
                for j in range(max(0, i - 5), min(len(tokens), i + 6)):
                    if i != j and len(tokens[j]) >= 2:
                        h_j = djb2_hash(tokens[j].lower())
                        self.embeddings.get_or_create(h_j, tokens[j].lower())
                        key = (min(h_t, h_j), max(h_t, h_j))
                        edge = self.graph.edges.get(key)
                        if edge:
                            new_edges.append((h_t, h_j, edge.weight))
            if new_edges:
                all_h = list(self.embeddings.vectors.keys())
                self.embeddings.train_incremental(new_edges[:100], all_h, lr=0.01)
        return result

    def train_and_verify(self, text: str) -> dict:
        """Обучить И ПОКАЗАТЬ результат."""
        stats_before = self.graph.get_stats()
        result = self.train_text(text)
        stats_after = self.graph.get_stats()

        tokens = _tokenize(text)
        sample_patterns = {}
        for t in tokens[:10]:
            if len(t) >= 3:
                sample_patterns[t] = pattern_to_str(word_to_pattern(t))

        return {
            **result,
            "before": stats_before,
            "after": stats_after,
            "sample_patterns": sample_patterns,
            "formula_generation": self.formula_pool.generation,
            "formula_fitness": round(self.formula_pool.best().fitness, 4),
        }

    def _do_retrieval_training(self, query: str, answer_text: str) -> None:
        """
        Feedback loop: формулы УЧАТСЯ на каждом успешном ответе.
        Вызывается из единого фонового worker-потока.
        """
        q_tokens = _tokenize(query)
        a_tokens = _tokenize(answer_text)
        if not q_tokens or not a_tokens:
            return

        pairs_added = 0
        for qt in q_tokens:
            if len(qt) < 3:
                continue
            q_pat = word_to_pattern(qt)
            for at in a_tokens:
                if len(at) < 3 or at == qt:
                    continue
                a_pat = word_to_pattern(at)
                self.formula_pool.add_semantic_pair(q_pat, a_pat)
                pairs_added += 1
                if pairs_added >= 40:
                    break
            if pairs_added >= 40:
                break

        self.formula_pool.add_association(query, answer_text)

        if pairs_added > 0:
            self._evolution_counter += 1
            fitness = self.formula_pool.evolve(generations=1)
            log.debug(
                "Formula feedback: +%d pairs, fitness=%.4f, gen=%d",
                pairs_added, fitness, self.formula_pool.generation,
            )
            self._incremental_embedding_train(q_tokens, a_tokens)
            if self._evolution_counter % 10 == 0:
                self._save_formulas()
                self._save_embeddings()

    def _train_formula_on_c_knowledge(self, query: str, c_knowledge: list[str]) -> None:
        """
        Кросс-обучение: C-модель → Python-формулы.

        Когда C-модель (.klm) возвращает знания по запросу,
        мы используем их как семантические пары для формул.
        Это связывает два уровня AI (C числовой + Python формульный).
        """
        if not c_knowledge:
            return
        q_tokens = _tokenize(query)
        if not q_tokens:
            return

        pairs_added = 0
        for knowledge in c_knowledge[:3]:
            k_tokens = _tokenize(knowledge)
            for qt in q_tokens:
                if len(qt) < 3:
                    continue
                q_pat = word_to_pattern(qt)
                for kt in k_tokens:
                    if len(kt) < 3 or kt == qt:
                        continue
                    k_pat = word_to_pattern(kt)
                    self.formula_pool.add_semantic_pair(q_pat, k_pat)
                    pairs_added += 1
                    if pairs_added >= 30:
                        break
                if pairs_added >= 30:
                    break
            if pairs_added >= 30:
                break

        if pairs_added > 0:
            self.formula_pool.evolve(generations=2)
            log.debug(
                "C→Python cross-training: +%d pairs, fitness=%.4f",
                pairs_added, self.formula_pool.best().fitness,
            )

    # ------------------------------------------------------------------
    # Специальные команды
    # ------------------------------------------------------------------

    def _incremental_embedding_train(
        self,
        q_tokens: list[str],
        a_tokens: list[str],
    ) -> None:
        """
        Инкрементальное обучение эмбеддингов на паре (query, answer).

        Слова запроса и ответа → co-occurrence пары → SGD update.
        Быстро (мс) — можно вызывать после каждого ответа.
        """
        if not q_tokens or not a_tokens:
            return

        new_edges: list[tuple[int, int, float]] = []
        for qt in q_tokens:
            if len(qt) < 3:
                continue
            h_q = djb2_hash(qt.lower())
            self.embeddings.get_or_create(h_q, qt.lower())
            for at in a_tokens:
                if len(at) < 3 or at == qt:
                    continue
                h_a = djb2_hash(at.lower())
                self.embeddings.get_or_create(h_a, at.lower())
                # Вес = 0.5 для Q-A пар (слабее чем graph co-occurrence)
                new_edges.append((h_q, h_a, 0.5))
                if len(new_edges) >= 50:
                    break
            if len(new_edges) >= 50:
                break

        if new_edges:
            all_h = list(self.embeddings.vectors.keys())
            self.embeddings.train_incremental(new_edges, all_h, lr=0.01)

    def _basic_formula_data(self) -> dict:
        """Базовый formula_data для команд/приветствий (без query-паттернов)."""
        best = self.formula_pool.best()
        return {
            "query_patterns": {},
            "query_hashes": {},
            "answer_patterns": {},
            "formula_predict": 0,
            "formula_genome_hex": best.gene.to_hex(),
            "formula_fitness": round(best.fitness, 4),
            "formula_generation": self.formula_pool.generation,
            "graph_score": 0,
            "graph_candidates": 0,
            "retrieved_sentences": [],
            "formula_generated_words": [],
            "sentence_store_size": self.sentence_store.size,
            "memory_digits": self.sentence_store.memory_digits,
        }

    # ------------------------------------------------------------------
    # Математический вычислитель
    # ------------------------------------------------------------------

    _MATH_EXPR_RE = re.compile(
        r'^[\d\s\+\-\*/\(\)\.\,\^%]+$'
        r'|^(?:сколько будет|чему равно|посчитай|вычисли|calculate)\s+.+$',
        re.IGNORECASE,
    )
    _MATH_CLEAN_RE = re.compile(
        r'^(?:сколько будет|чему равно|посчитай|вычисли|calculate)\s+',
        re.IGNORECASE,
    )

    def _try_math_eval(self, expr: str) -> dict | None:
        """Безопасное вычисление математических выражений."""
        if not expr or len(expr) > 200:
            return None

        # Проверяем, похоже ли на математику
        clean = self._MATH_CLEAN_RE.sub('', expr).strip()
        # Должно содержать хотя бы одну цифру и оператор
        has_digit = any(c.isdigit() for c in clean)
        has_op = any(c in clean for c in '+-*/^%')
        has_func = any(fn in clean for fn in ('sqrt', 'sin', 'cos', 'tan', 'log', 'abs', 'pow',
                                               'корень', 'степень', 'факториал'))
        if not has_digit or (not has_op and not has_func):
            return None

        # Только разрешённые символы
        allowed = set('0123456789+-*/().,%^ sqrtincoablgpwefh')
        if not all(c in allowed or c.isspace() for c in clean):
            return None

        # Подготовка выражения
        safe_expr = clean.replace('^', '**').replace(',', '.').replace('×', '*').replace('÷', '/')
        # Русские функции
        safe_expr = safe_expr.replace('корень', 'sqrt')

        # Безопасные функции
        safe_globals: dict = {"__builtins__": {}}
        safe_locals = {
            "sqrt": math.sqrt, "sin": math.sin, "cos": math.cos,
            "tan": math.tan, "log": math.log, "log2": math.log2,
            "log10": math.log10, "abs": abs, "pow": pow,
            "pi": math.pi, "e": math.e, "factorial": math.factorial,
            "ceil": math.ceil, "floor": math.floor,
        }

        try:
            # Компилируем и проверяем AST (защита от инъекций)
            import ast
            tree = ast.parse(safe_expr, mode='eval')
            for node in ast.walk(tree):
                if isinstance(node, (ast.Import, ast.ImportFrom, ast.Attribute,
                                      ast.FunctionDef, ast.AsyncFunctionDef)):
                    return None
                # SECURITY: ограничиваем числовые литералы для предотвращения DoS
                if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
                    if abs(node.value) > 1e15:
                        return None
                # Блокируем factorial(N) при N > 1000
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
                    if node.func.id == 'factorial' and node.args:
                        if isinstance(node.args[0], ast.Constant) and isinstance(node.args[0].value, (int, float)):
                            if node.args[0].value > 1000:
                                return None

            result = eval(compile(tree, '<math>', 'eval'), safe_globals, safe_locals)  # noqa: S307
        except Exception:
            return None

        if isinstance(result, complex):
            return None

        # Форматируем результат
        if isinstance(result, float):
            if result == int(result) and abs(result) < 1e15:
                formatted = str(int(result))
            else:
                formatted = f"{result:.10g}"
        else:
            formatted = str(result)

        # Обучаем формулу на этом примере (вход→выход)
        try:
            in_hash = djb2_hash(clean)
            out_val = int(float(formatted)) if float(formatted) == int(float(formatted)) else int(float(formatted) * 1000)
            self.formula_pool.add_semantic_pair(
                clean, formatted, in_hash % 1000000, out_val % 1000000,
            )
        except Exception:
            pass

        resp = (
            f"🔢 **{expr}** = **{formatted}**\n\n"
            f"_Вычислено Kolibri числовым мышлением_"
        )
        return {
            "response": resp, "confidence": 1.0,
            "sources": ["math-engine"], "method": "math-eval",
            "knowledge_hits": 0,
            "formula_data": self._basic_formula_data(),
            "graph_stats": self.graph.get_stats(),
        }

    # ------------------------------------------------------------------
    # Когнитивный модуль (lazy init)
    # ------------------------------------------------------------------

    def get_cognition(self) -> SwarmCognition:
        """Вернуть SwarmCognition, создав при первом вызове."""
        if self._cognition is None:
            self._cognition = SwarmCognition(self.graph)
        return self._cognition

    def _cot_enrich_response(
        self, response: str, message: str, confidence: float,
        strategy: dict,
    ) -> str:
        """
        CoT-управляемое обогащение ответа.
        
        Если intent=explain → добавляем абстрактное обобщение и каузальные связи.
        Это РЕАЛЬНОЕ влияние CoT на ответ, не декоративное.
        """
        try:
            cog = self.get_cognition()
            additions: list[str] = []
            
            # Абстрактное обобщение (2-хоповое)
            if strategy.get("use_abstract"):
                abstract_res = cog.abstract(message, depth=2)
                if abstract_res.answer and abstract_res.confidence > 0.3:
                    # Извлекаем новые слова, которых нет в основном ответе
                    resp_words = set(_tokenize(response.lower()))
                    abstract_words = [
                        w for w in _tokenize(abstract_res.answer.lower())
                        if w not in resp_words and not _is_stop_word(w) and len(w) > 3
                    ]
                    if abstract_words:
                        unique_concepts = list(dict.fromkeys(abstract_words))[:4]
                        additions.append(
                            f"В более широком контексте это связано с: "
                            f"{', '.join(unique_concepts)}"
                        )
            
            # Каузальные связи
            if strategy.get("use_causal") and cog.causal_index is not None:
                why_res = cog.why(message, max_chain=2)
                if why_res.chain:
                    chain_words = [w for w, s in why_res.chain if s > 0.5]
                    if chain_words:
                        additions.append(
                            f"Причинные факторы: {', '.join(chain_words[:3])}"
                        )
            
            if additions:
                extra = ". ".join(additions)
                if not response.endswith("."):
                    response += "."
                response += f" {extra}."
        except Exception:
            pass  # Не ломаем ответ если когнитивный модуль упал
        
        return response

    def _cognitive_enrichment(self, message: str) -> dict:
        """
        Когнитивное обогащение ответа — абстракция + самомоделирование.
        Вызывается из chat() для каждого сообщения.
        Возвращает dict с дополнительными полями.
        """
        try:
            cog = self.get_cognition()
            # Самомоделирование — быстрая оценка компетентности
            intro = cog.introspect(message)
            self_model = intro.introspection or {}

            # Абстрактное мышление — 2-хоповое обобщение
            abstract_result = cog.abstract(message, depth=2)

            # Каузальная цепочка (если индекс обучен)
            causal_chain: list[tuple[str, float]] = []
            if cog.causal_index is not None:
                why_result = cog.why(message, max_chain=3)
                causal_chain = why_result.chain or []

            return {
                "cognitive": {
                    "self_model": self_model,
                    "abstract_answer": abstract_result.answer,
                    "abstract_confidence": abstract_result.confidence,
                    "causal_chain": [
                        {"word": w, "score": round(s, 4)}
                        for w, s in causal_chain
                    ],
                    "predicted_confidence": self_model.get("predicted_confidence", 0.0),
                },
            }
        except Exception as exc:
            log.warning("cognitive enrichment failed: %s", exc, exc_info=True)
            return {"cognitive": None}

    def _handle_special_commands(self, lower: str) -> dict | None:
        stripped = lower.strip().rstrip("?!.")

        # --- Математические выражения ---
        math_result = self._try_math_eval(stripped)
        if math_result is not None:
            return math_result

        # --- Погода ---
        if _match_weather_intent(stripped):
            location = _extract_weather_location(stripped)
            if not location:
                return {
                    "response": "Уточните город для прогноза погоды.",
                    "confidence": 0.2,
                    "sources": ["weather"],
                    "method": "weather-clarify",
                    "knowledge_hits": 0,
                    "formula_data": self._basic_formula_data(),
                    "graph_stats": self.graph.get_stats(),
                }
            resp, conf, method = self._fetch_weather(location)
            return {
                "response": resp,
                "confidence": conf,
                "sources": ["open-meteo"] if method == "weather-open-meteo" else ["weather"],
                "method": method,
                "knowledge_hits": 0,
                "formula_data": self._basic_formula_data(),
                "graph_stats": self.graph.get_stats(),
            }

        # --- Приветствия ---
        _GREETINGS = {
            "привет", "здравствуй", "здравствуйте", "хай", "хей",
            "hello", "hi", "hey", "приветствую", "салют", "йо",
        }
        if stripped in _GREETINGS or stripped.startswith(("добрый ", "доброе ")):
            g = self.graph.get_stats()
            best = self.formula_pool.best()
            resp = (
                f"👋 Привет! Я **Kolibri AI** — система Числового Формульного Мышления.\n\n"
                f"🧠 Мой мозг:\n"
                f"• **{g['patterns']:,}** числовых паттернов (64 цифры каждый)\n"
                f"• **{g['edges']:,}** связей в графе знаний\n"
                f"• **{self.sentence_store.size:,}** знаний в числовом хранилище "
                f"(**{self.sentence_store.memory_digits:,}** цифр)\n"
                f"• Формулы: поколение **{self.formula_pool.generation}**, "
                f"fitness **{round(best.fitness, 3)}**\n\n"
                f"Спросите меня о чём-нибудь! Напишите `помощь` для команд."
            )
            return {
                "response": resp, "confidence": 1.0,
                "sources": ["system"], "method": "greeting",
                "knowledge_hits": 0,
                "formula_data": self._basic_formula_data(),
                "graph_stats": self.graph.get_stats(),
            }

        if stripped in ("help", "помощь", "помоги", "что умеешь", "что ты умеешь", "помощ"):
            resp = (
                "🧠 **Kolibri AI — Числовое Формульное Мышление**\n\n"
                "• 🔢 **Все знания в ЧИСЛАХ** — каждое слово = 64 цифры\n"
                "• ⚡ **Формулы** — 4000 цифр генома, до 500 слоёв, 12 операций\n"
                "• 🧬 **Эволюция** — мутация + кроссовер + селекция формул\n"
                "• 🕸️ **Граф знаний** — связи между числовыми паттернами\n"
                "• 🔄 **Децентрализация** — обмен знаниями между узлами\n\n"
                "**Команды:**\n"
                "• `паттерн слово` — показать числовой паттерн\n"
                "• `покажи формулу` — показать текущую формулу\n"
                "• `покажи статистику` — статистика модели\n"
                "• Любой URL → обучение на странице"
            )
            return {"response": resp, "confidence": 1.0, "sources": ["system"], "method": "command", "knowledge_hits": 0, "formula_data": self._basic_formula_data(), "graph_stats": self.graph.get_stats()}

        if "статистик" in lower or ("модел" in lower and "покаж" in lower):
            g = self.graph.get_stats()
            c = self._get_model_stats()
            best = self.formula_pool.best()
            resp = (
                f"📊 **Kolibri AI — Числовое Мышление**\n\n"
                f"**Числовой граф (Python):**\n"
                f"• Паттернов: **{g['patterns']:,}** / {g['max_patterns']:,}\n"
                f"• Рёбер: **{g['edges']:,}** / {g['max_edges']:,}\n"
                f"• Документов: **{g['documents_trained']}**\n"
                f"• Токенов: **{g['tokens_processed']:,}**\n"
                f"• Числовое хранилище: **{self.sentence_store.size:,}** записей "
                f"(**{self.sentence_store.memory_digits:,}** цифр)\n"
                f"• Avg fitness: **{g['avg_fitness']}** | Avg weight: **{g['avg_weight']}**\n\n"
                f"**Формулы:**\n"
                f"• Поколение: **{self.formula_pool.generation}**\n"
                f"• Лучшая fitness: **{round(best.fitness, 4)}**\n"
                f"• Геном: `{best.gene.to_hex()[:48]}…`\n\n"
                f"**C-модель (.klm):**\n"
                f"• {'✅ Загружена' if c.get('exists') else '❌ Не найдена'}\n"
                f"• Паттернов: **{c.get('patterns', 0):,}** | Рёбер: **{c.get('edges', 0):,}**\n"
                f"• Размер: **{c.get('size_mb', 0)} МБ**"
            )
            return {"response": resp, "confidence": 1.0, "sources": ["system"], "method": "command", "knowledge_hits": 0, "formula_data": self._basic_formula_data(), "graph_stats": self.graph.get_stats()}

        if lower.startswith("паттерн ") or lower.startswith("pattern "):
            word = lower.split(maxsplit=1)[1].strip()
            p = pattern_to_str(word_to_pattern(word))
            h = djb2_hash(word)
            digits = text_to_digits(word)
            recovered = digits_to_text(digits)
            sim_words = self.graph.find_similar(word, limit=5)
            sim_list = "\n".join(f"  • `{w}` — сходство {s}" for w, s in sim_words) if sim_words else "  (пока нет данных)"
            resp = (
                f"🔢 **Числовой паттерн: `{word}`**\n\n"
                f"• Паттерн (64 цифры): `{p}`\n"
                f"• DJB2 хеш: `{h}`\n"
                f"• FNV-1a хеш: `{fnv1a_hash(word)}`\n"
                f"• Текст→Цифры: `{''.join(str(d) for d in digits[:30])}…` ({len(digits)} цифр)\n"
                f"• Восстановление: `{recovered}`\n\n"
                f"**Похожие паттерны в графе:**\n{sim_list}"
            )
            return {"response": resp, "confidence": 1.0, "sources": ["number-mind"], "method": "pattern-lookup", "knowledge_hits": len(sim_words), "formula_data": self._basic_formula_data(), "graph_stats": self.graph.get_stats()}

        if "формул" in lower and ("покаж" in lower or "расскаж" in lower):
            best = self.formula_pool.best()
            gene_preview = best.gene.digits[:64]
            resp = (
                f"⚡ **Формула Kolibri (лучшая из 16)**\n\n"
                f"• Поколение: **{self.formula_pool.generation}**\n"
                f"• Fitness: **{round(best.fitness, 6)}**\n"
                f"• Ассоциаций: **{len(best.associations)}**\n"
                f"• Сложность: **{round(best.gene.complexity(), 3)}**\n\n"
                f"**Геном (64 из 4000 цифр):**\n"
                f"`{''.join(str(d) for d in gene_preview)}`\n\n"
                f"**Hex:** `{best.gene.to_hex()}`\n\n"
                f"**500 слоёв × 12 операций (fast=100):**\n"
                f"linear, inverse, modular, quadratic, XOR, AND, sin, saturate, OR, gaussian, tanh, sigmoid"
            )
            return {"response": resp, "confidence": 1.0, "sources": ["formula-pool"], "method": "formula-inspect", "knowledge_hits": 0, "formula_data": self._basic_formula_data(), "graph_stats": self.graph.get_stats()}

        # Системные метрики — ТОЛЬКО если это прямой запрос о системе компьютера
        _SYSTEM_TRIGGERS = (
            "покажи систем", "системные метрик", "метрики систем",
            "cpu", "загрузка процессор", "использование памят",
            "сколько памят", "покажи cpu", "show system",
        )
        if any(t in lower for t in _SYSTEM_TRIGGERS):
            try:
                import psutil
                cpu = psutil.cpu_percent(interval=0.1)
                mem = psutil.virtual_memory()
                resp = (
                    f"🖥️ **Системные метрики**\n\n"
                    f"• CPU: **{cpu}%**\n"
                    f"• Память: **{mem.percent}%** ({round(mem.used / (1024**3), 2)} ГБ / {round(mem.total / (1024**3), 2)} ГБ)"
                )
            except Exception:
                resp = "❌ Не удалось получить системные метрики."
            return {"response": resp, "confidence": 1.0, "sources": ["system"], "method": "command", "knowledge_hits": 0, "formula_data": self._basic_formula_data(), "graph_stats": self.graph.get_stats()}

        if "здоров" in lower or "health" in lower or "статус" in lower:
            g = self.graph.get_stats()
            resp = (
                f"🟢 **Kolibri AI — Числовое Мышление**\n\n"
                f"• Граф: **{g['patterns']:,}** паттернов, **{g['edges']:,}** рёбер\n"
                f"• Предложений: **{self.sentence_store.size:,}**\n"
                f"• Формулы: поколение **{self.formula_pool.generation}**\n"
                f"• C-модель: **{'✅' if self.c_retriever.available else '❌'}**\n"
                f"• Диалогов: **{len(self.conversations)}**\n"
                f"• Движок: **Числовое Формульное Мышление**"
            )
            return {"response": resp, "confidence": 1.0, "sources": ["system"], "method": "command", "knowledge_hits": 0, "formula_data": self._basic_formula_data(), "graph_stats": self.graph.get_stats()}

        return None

    # ------------------------------------------------------------------
    # Утилиты
    # ------------------------------------------------------------------

    def get_or_create_conversation(self, conv_id: str | None = None) -> Conversation:
        if conv_id and conv_id in self.conversations:
            return self.conversations[conv_id]
        new_id = conv_id or hashlib.md5(str(time.time()).encode()).hexdigest()[:12]
        conv = Conversation(id=new_id)
        self.conversations[new_id] = conv
        return conv

    def _get_model_stats(self) -> dict:
        now = time.time()
        if self._stats_cache and now - self._stats_cache_time < 30:
            return self._stats_cache
        self._stats_cache = self.c_retriever.get_stats()
        self._stats_cache_time = now
        return self._stats_cache

    def reload_corpus(self) -> dict:
        """Перезагрузить корпус и пересобрать числовой граф. Формулы сохраняются."""
        self.graph = KnowledgeGraph()
        # После пересоздания графа нужно заново привязать эмбеддинги и swarm.
        self.graph.embeddings = self.embeddings
        try:
            from .swarm_sync import get_swarm_manager
            get_swarm_manager().set_knowledge_graph(self.graph)
        except Exception:
            pass
        # Формулы НЕ сбрасываем — они продолжают эволюцию
        # Загружаем с диска (если сохранены) или оставляем текущие
        self.sentence_store = SentenceStore()
        self.sentence_store.embeddings = self.embeddings
        self._corpus_loaded = False
        self._load_corpus()
        # Сохраняем обновлённые формулы
        self._save_formulas()
        g = self.graph.get_stats()
        return {
            "corpus_loaded": self._corpus_loaded,
            "documents": g["documents_trained"],
            "vocab_size": g["patterns"],
            "edges": g["edges"],
            "formula_generation": self.formula_pool.generation,
            "formula_fitness": round(self.formula_pool.best().fitness, 4),
        }


# ---------------------------------------------------------------------------
# Singleton
# ---------------------------------------------------------------------------

import threading as _threading

_engine_instance: KolibriAIEngine | None = None
_engine_initializing = False
_engine_lock = _threading.Lock()


def get_engine() -> KolibriAIEngine:
    global _engine_instance, _engine_initializing
    if _engine_instance is not None:
        return _engine_instance
    with _engine_lock:
        if _engine_instance is not None:
            return _engine_instance
        if _engine_initializing:
            # Другой поток уже инициализирует — ждём
            pass
        _engine_initializing = True
    try:
        engine = KolibriAIEngine()
        with _engine_lock:
            _engine_instance = engine
            _engine_initializing = False
    except Exception:
        with _engine_lock:
            _engine_initializing = False
        raise
    return _engine_instance


def pre_init_engine() -> None:
    """Предзагрузка движка при старте сервера (вызывается из main.py startup event)."""
    log.info("Pre-initializing AI engine...")
    t0 = time.time()
    get_engine()
    log.info("AI engine ready in %.1fs", time.time() - t0)
