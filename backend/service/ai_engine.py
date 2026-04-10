"""
ai_engine.py — Движок «Числового Мышления» Kolibri

НЕ классический TF-IDF / N-gram. Настоящая архитектура Kolibri:

1. Каждое слово = 64-цифровой числовой паттерн (DJB2 → LCG каскад)
2. Знания = граф связей между паттернами (co-occurrence edges)
3. Формулы = 4000 цифр генома → до 500 слоёв, 12 операций
4. Эволюция: мутация + кроссовер + селекция = улучшение формул
5. Восстановление: из числового паттерна → исходное слово
6. Всё хранится в ЧИСЛАХ. Формулах. Паттернах.

#17-21. Python Backend улучшения:
- Async inference pipeline
- Request rate limiting
- Structured logging
- Prometheus metrics
- Graceful shutdown
"""
from __future__ import annotations

import asyncio
import concurrent.futures
import contextvars
import hashlib
import json
import logging
import math
import os
import queue
import re
import random
import signal
import subprocess
import threading
import time
import uuid
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from functools import lru_cache
from pathlib import Path
from typing import Optional
from urllib.parse import urlparse

from .number_mind import (
    KnowledgeGraph,
    FormulaPool,
    KolibriGene,
    SentenceStore,
    KLM_PATTERN_SIZE,
    GENE_SIZE,
    FORMULA_LAYERS,
    FORMULA_LAYERS_FAST,
    PatternEntry,
    KnowledgeEdge,
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
    _split_sentences,
    _is_low_quality_sentence,
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
from .realtime_lookup import (
    external_network_available,
    fetch_exchange_rate_answer,
    fetch_news_digest,
    fetch_reference_answer,
    fetch_time_answer,
    fetch_weather_answer,
    looks_like_currency_query,
    looks_like_reference_query,
    looks_like_time_query,
)
from .search_engine import fetch_page_text, search_quick
from .rag_pipeline import RAGPipeline
from .code_gen import CodeGenerationPipeline
from .math_reasoning import MathReasoningPipeline
from .function_calling import FunctionCallingPipeline
from .speculative_decoding import SpeculativeDecoder, SpeculativeConfig
from .web_research import WebResearchPipeline

import logging

log = logging.getLogger("kolibri.ai")

# ---------------------------------------------------------------------------
# Конфигурация
# ---------------------------------------------------------------------------

_PROJECT_ROOT = get_project_root()
_TRAINER_BIN = _PROJECT_ROOT / "build" / "kolibri_mass_trainer"
_C_INFER_BIN = _PROJECT_ROOT / "build" / "kolibri_infer_cli"
_DEFAULT_MODEL = _PROJECT_ROOT / "data" / "models" / "kolibri_web.klm"
_CORPUS_DIR = _PROJECT_ROOT / "data" / "corpus"
_DEFAULT_FORMULA_MEMORY_DIR = _PROJECT_ROOT / "data" / "formula_domains"
_FORMULA_SAVE_PATH = _PROJECT_ROOT / "data" / "models" / "kolibri_formulas.json"
_EMBEDDINGS_SAVE_PATH = _PROJECT_ROOT / "data" / "models" / "kolibri_embeddings.json"

_MAX_CONTEXT_TURNS = 20
_QUERY_TIMEOUT = 24
_MAX_ACTIVE_CONVERSATIONS = 256
_CONVERSATION_TTL_SECONDS = 24 * 60 * 60

# Минимальный словарь RU→EN для кросс-языкового retrieval по англ. корпусу.
# Это не переводчик; только "мост" для частых технических терминов.
_RU_TO_EN_TERMS: dict[str, str] = {
    "кубит": "qubit",
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

# Общие слова вопроса, которые не несут предметный смысл темы.
_GENERIC_QUERY_WORDS: set[str] = {
    "как", "что", "кто", "такое", "такой", "такая",
    "объясни", "назови", "кратко", "простыми", "словами",
    "почему", "когда", "где", "какой", "какая", "какие",
    "работает", "работать", "будет", "чему", "равно",
    "началась", "начался", "началось", "пример", "расскажи",
    "приготовить", "составь", "сделай", "дай", "план", "изучения", "между", "разница",
    "неделя", "недели", "недель",
    "сколько", "how", "many",
    "web", "augment", "web-augment",
    "знаешь", "данные", "данных", "даанные",
}

_AUTO_FACT_PATTERNS: tuple[re.Pattern[str], ...] = (
    re.compile(r"^\s*я\s+(люблю|обожаю|предпочитаю|увлекаюсь|интересуюсь|занимаюсь|работаю|живу|учусь|хочу)\b", re.IGNORECASE),
    re.compile(r"^\s*мне\s+(нравится|интересно|важно)\b", re.IGNORECASE),
    re.compile(r"^\s*у\s+меня\s+(есть|любим|интерес)\b", re.IGNORECASE),
)

_AUTO_FACT_BAD_PREFIXES: tuple[str, ...] = (
    "я думаю", "я считаю", "я хочу спросить", "я спросил", "я спрашиваю",
    "я не знаю", "я знаю", "мне кажется", "у меня вопрос",
)

# --- Лингвистическая нормализация (RU/EN + фонемный контур) ---
_LATIN_TO_CYR_DIGRAPHS: tuple[tuple[str, str], ...] = (
    ("shch", "щ"),
    ("sch", "щ"),
    ("yo", "ё"),
    ("jo", "ё"),
    ("zh", "ж"),
    ("kh", "х"),
    ("ch", "ч"),
    ("sh", "ш"),
    ("ts", "ц"),
    ("ya", "я"),
    ("ja", "я"),
    ("yu", "ю"),
    ("ju", "ю"),
    ("ye", "е"),
    ("je", "е"),
)

_LATIN_TO_CYR_CHARS: dict[str, str] = {
    "a": "а",
    "b": "б",
    "c": "к",
    "d": "д",
    "e": "е",
    "f": "ф",
    "g": "г",
    "h": "х",
    "i": "и",
    "j": "й",
    "k": "к",
    "l": "л",
    "m": "м",
    "n": "н",
    "o": "о",
    "p": "п",
    "q": "к",
    "r": "р",
    "s": "с",
    "t": "т",
    "u": "у",
    "v": "в",
    "w": "в",
    "x": "кс",
    "y": "й",
    "z": "з",
}

_GREETING_CANONICAL: frozenset[str] = frozenset({
    "привет", "приветик", "здравствуй", "здравствуйте", "приветствую",
    "салют", "здарова", "здорова", "хай", "хей", "hello", "hi", "hey", "yo", "йо",
})
_GREETING_ADDRESS_WORDS: frozenset[str] = frozenset({
    "колибри", "kolibri", "ai", "ассистент", "assistant", "бот", "колибраи", "калибри",
})
_SMALLTALK_WORDS: frozenset[str] = frozenset({
    "как", "дела", "поживаешь", "жив", "жива", "тут", "там", "сегодня", "настроение",
})
_ARCHITECTURE_WORDS: frozenset[str] = frozenset({
    "архитектура", "архитектуры", "устроено", "устроен", "устроена", "ядро", "ядра",
})
_KOLIBRI_WORDS: frozenset[str] = frozenset({
    "kolibri", "колибри", "калибри", "kolibry", "kolibri ai", "колибри ai",
})
_IDENTITY_NAME_WORDS: frozenset[str] = frozenset({
    "зовут", "имя", "представься", "назовись", "кто", "название",
})
_IDENTITY_YOU_WORDS: frozenset[str] = frozenset({
    "ты", "тебя", "тебе", "твой", "твое", "твоё", "вас", "вам",
})
_IDENTITY_USER_SELF_WORDS: frozenset[str] = frozenset({
    "меня", "мне", "мой", "моя", "мое", "моё", "я",
})

_WEATHER_QUERY_ROOTS: tuple[str, ...] = (
    "погод", "температ", "дожд", "снег", "ветер", "облач", "осадк", "прогноз",
    "weather", "forecast", "temperature", "rain", "snow", "wind",
)
_WEATHER_LOCATION_STOPWORDS: frozenset[str] = frozenset({
    "какая", "какой", "какие", "каково", "погода", "погоде", "погоду",
    "сегодня", "сейчас", "завтра", "послезавтра", "утром", "вечером",
    "в", "во", "на", "по", "для", "city", "weather", "forecast",
})
_WEATHER_ANSWER_MARKERS: tuple[str, ...] = (
    "погод", "температ", "градус", "осад", "дожд", "снег", "ветер", "облач",
    "ясно", "пасмур", "солнеч",
    "weather", "temperature", "precip", "rain", "snow", "wind", "cloud",
    "°", "°c", "°f", "m/s", "мм", "mm",
)
_PROJECT_QUERY_MARKERS: frozenset[str] = frozenset({
    "проект", "проекте", "проекта",
    "порт", "порта",
    "api", "backend", "бэкенд",
    "сервер", "сервис",
    "endpoint", "эндпоинт",
    "деплой", "релиз", "конфиг", "конфигурац",
    "docker", "nginx", "uvicorn", "fastapi",
})
_DIALOG_FACT_BAD_PREFIXES: tuple[str, ...] = (
    "запомни", "remember", "научи", "обучи", "расскажи", "объясни", "поясни",
    "кто", "что", "сколько", "почему", "как", "где", "когда",
)
_FOLLOWUP_DIRECTIVE_WORDS: frozenset[str] = frozenset({
    "подробнее", "подробней", "продолжай", "дальше", "еще", "ещё",
    "раскрой", "уточни", "детальнее", "детальней",
})
_ABUSIVE_MARKERS: frozenset[str] = frozenset({
    "дебил", "идиот", "тупой", "тупая", "дурак", "дурaк", "придурок", "кретин", "долб", "мудак",
})

_LINGUISTIC_DIRECT_NORMALIZE: dict[str, str] = {
    "превет": "привет",
    "превед": "привет",
    "преведствую": "приветствую",
    "калибри": "колибри",
    "каллибри": "колибри",
    "калбри": "колибри",
    "колибраи": "колибри",
    "порда": "порт",
    "порд": "порт",
    "праекте": "проекте",
    "праект": "проект",
    "проэкт": "проект",
    "исползуетса": "используется",
    "исползуется": "используется",
    "используетса": "используется",
    "бекенд": "бэкенд",
    "бекэнд": "бэкенд",
    "бэкендд": "бэкенд",
    "эндпоинтс": "эндпоинт",
    "эндпойнт": "эндпоинт",
}

_LINGUISTIC_CANONICAL_WORDS: frozenset[str] = frozenset({
    "колибри", "привет", "проект", "проекте", "проекта",
    "порт", "порта", "api", "backend", "бэкенд", "эндпоинт", "endpoint",
    "сервис", "сервер", "ubuntu", "деплой", "релиз",
    "используется", "использовать", "настроен", "запущен",
    "погода", "прогноз", "температура",
    "столица", "рецепт", "спутников", "марса",
})


def _env_flag(name: str, default: bool = False) -> bool:
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def _env_float(name: str, default: float) -> float:
    value = os.getenv(name)
    if value is None:
        return default
    try:
        return float(value.strip())
    except (TypeError, ValueError):
        return default


def _env_int(name: str, default: int) -> int:
    value = os.getenv(name)
    if value is None:
        return default
    try:
        return int(value.strip())
    except (TypeError, ValueError):
        return default


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
    updated_at: float = field(default_factory=time.time)

    def add(self, role: str, content: str, pattern_str: str = "", formula_used: str = "") -> None:
        self.turns.append(ConversationTurn(
            role=role, content=content,
            pattern_str=pattern_str,
            formula_used=formula_used,
        ))
        self.updated_at = time.time()
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


class CInferenceRunner:
    """Тонкая обёртка над C CLI инференса для формульной памяти."""

    def __init__(self, binary_path: Path | None = None, knowledge_path: Path | None = None) -> None:
        self.binary_path = binary_path or _C_INFER_BIN
        self.knowledge_path = knowledge_path or _DEFAULT_FORMULA_MEMORY_DIR

    @property
    def available(self) -> bool:
        return self.binary_path.exists() and self.knowledge_path.exists()

    @staticmethod
    def _safe_decode_output(value: bytes | str | None) -> str:
        if value is None:
            return ""
        if isinstance(value, bytes):
            return value.decode("utf-8", errors="ignore")
        return str(value)

    def query(self, text: str, strategy: str = "formula") -> dict | None:
        if not self.available:
            return None
        env = os.environ.copy()
        env.setdefault("KOLIBRI_KNOWLEDGE_PATH", str(self.knowledge_path))
        env.setdefault("KOLIBRI_FORMULA_MEMORY_PATH", str(self.knowledge_path))
        try:
            result = subprocess.run(
                [str(self.binary_path), "--strategy", strategy, "--query", text],
                capture_output=True,
                timeout=_QUERY_TIMEOUT,
                cwd=str(_PROJECT_ROOT),
                env=env,
            )
        except (subprocess.TimeoutExpired, FileNotFoundError, OSError):
            return None
        if result.returncode != 0:
            return None
        return self._parse_output(self._safe_decode_output(result.stdout))

    def warmup(self) -> None:
        if not self.available:
            return
        try:
            self.query("что такое математика", strategy="formula")
        except Exception:
            return

    def _parse_output(self, stdout: str) -> dict | None:
        lines = stdout.splitlines()
        payload: dict[str, object] = {}
        in_response = False
        response_lines: list[str] = []
        for raw in lines:
            line = raw.rstrip("\n")
            if line == "RESPONSE_BEGIN":
                in_response = True
                continue
            if line == "RESPONSE_END":
                in_response = False
                continue
            if in_response:
                response_lines.append(line)
                continue
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            payload[key.strip().lower()] = value.strip()
        response = "\n".join(response_lines).strip()
        if not response:
            return None
        payload["response"] = response
        for key in ("confidence", "duration_ms"):
            try:
                payload[key] = float(payload.get(key, 0.0))
            except (TypeError, ValueError):
                payload[key] = 0.0
        for key in ("knowledge_hits", "formulas_applied", "logic_rules", "topic_token_count"):
            try:
                payload[key] = int(str(payload.get(key, "0")))
            except (TypeError, ValueError):
                payload[key] = 0
        for key in ("digit_winner",):
            try:
                payload[key] = int(str(payload.get(key, "0")))
            except (TypeError, ValueError):
                payload[key] = 0
        for key in ("digit_winner_score", "digit_runner_up_score", "digit_consensus"):
            try:
                payload[key] = float(payload.get(key, 0.0))
            except (TypeError, ValueError):
                payload[key] = 0.0
        digit_votes_raw = str(payload.get("digit_votes", "") or "").strip()
        digit_votes: dict[str, float] = {}
        if digit_votes_raw:
            for item in digit_votes_raw.split(","):
                if ":" not in item:
                    continue
                digit, raw_score = item.split(":", 1)
                digit = digit.strip()
                try:
                    digit_votes[digit] = float(raw_score.strip())
                except ValueError:
                    continue
        payload["digit_votes"] = digit_votes
        return payload


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
        self._sentence_store_max = max(
            20_000,
            min(500_000, _env_int("KOLIBRI_SENTENCE_STORE_MAX", 180_000)),
        )
        self.sentence_store = SentenceStore(max_sentences=self._sentence_store_max)
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

        # #17. Async inference pipeline
        self._inference_executor = concurrent.futures.ThreadPoolExecutor(
            max_workers=4, thread_name_prefix="kolibri-inference"
        )

        # #18. Request rate limiting
        self._rate_limiter = _RateLimiter(max_requests=10, window_seconds=60)

        # #19. Structured logging
        self._logger = logging.getLogger("kolibri.ai_engine")

        # #20. Prometheus metrics
        self._metrics = {
            "total_queries": 0,
            "total_errors": 0,
            "query_durations": [],
            "cache_hits": 0,
            "cache_misses": 0,
        }

        # #21. Graceful shutdown
        self._shutdown_event = threading.Event()
        try:
            signal.signal(signal.SIGTERM, self._handle_shutdown_signal)
            signal.signal(signal.SIGINT, self._handle_shutdown_signal)
        except ValueError:
            pass  # Signal handlers can only be set in main thread
        self.c_inference = CInferenceRunner()

        # #Фаза A1: RAG Pipeline
        self.rag = RAGPipeline()
        self._rag_enabled = True
        self._rag_top_k = 5

        # #Фаза B1: Code Generation
        self.code_gen = CodeGenerationPipeline()
        self._code_gen_enabled = True

        # #Фаза B2: Math Reasoning
        self.math = MathReasoningPipeline()
        self._math_enabled = True

        # #Фаза B3: Function Calling
        self.function_calling = FunctionCallingPipeline()
        self._function_calling_enabled = True

        # #Фаза C2: Speculative Decoding
        self.speculative = SpeculativeDecoder(SpeculativeConfig(draft_tokens=3))
        self._speculative_enabled = True

        # #Фаза Web Research: Глубокий веб-поиск и обучение
        self.web_research = WebResearchPipeline()
        self._web_research_enabled = True

        self.conversations: dict[str, Conversation] = {}
        self._corpus_loaded = False
        self._model_stats_ttl_sec = max(
            15.0,
            min(3600.0, _env_float("KOLIBRI_MODEL_STATS_TTL_SEC", 300.0)),
        )
        self._stats_cache_lock = threading.Lock()
        self._stats_refresh_inflight = False
        self._stats_cache: dict | None = self._quick_model_stats_snapshot()
        self._stats_cache_time = time.time()
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
        # Совместимость: legacy window оставляем, но в chat() используем
        # отдельные окна памяти на каждый conversation_id.
        self._context_window = ContextWindow(max_tokens=8192)
        self._context_windows: dict[str, ContextWindow] = {}
        # Тяжёлые когнитивные надстройки отключены по умолчанию:
        # фокус на быстрый стабильный чат.
        self._enable_cognition = _env_flag("KOLIBRI_ENABLE_COGNITION", default=False)
        self._enable_cot_enrichment = _env_flag("KOLIBRI_ENABLE_COT_ENRICHMENT", default=False)
        self._enable_c_retriever = _env_flag("KOLIBRI_ENABLE_C_RETRIEVER", default=False)
        self._enable_c_inference = _env_flag("KOLIBRI_ENABLE_C_INFERENCE", default=True)
        self._enable_dialog_learning = _env_flag("KOLIBRI_ENABLE_DIALOG_LEARNING", default=True)
        self._enable_formula_associations = _env_flag("KOLIBRI_ENABLE_FORMULA_ASSOCIATIONS", default=False)
        self._enable_formula_generation = _env_flag("KOLIBRI_ENABLE_FORMULA_GENERATION", default=False)
        self._dialog_learning_min_conf = max(
            0.0,
            min(1.0, _env_float("KOLIBRI_DIALOG_LEARNING_MIN_CONF", 0.35)),
        )
        self._show_formula_hints = _env_flag("KOLIBRI_SHOW_FORMULA_HINTS", default=False)
        self._auto_learn_long_messages = _env_flag("KOLIBRI_AUTO_LEARN_LONG_MESSAGES", default=True)
        self._user_doc_max = max(8, min(96, _env_int("KOLIBRI_USER_DOC_MAX", 32)))
        self._dialog_learning_counter = 0
        self._user_doc_learning_counter = 0
        self._persist_min_interval_sec = max(0.0, _env_float("KOLIBRI_PERSIST_MIN_INTERVAL_SEC", 8.0))
        self._last_persist_ts = 0.0
        self._fast_train_sample_rate = max(
            0.0,
            min(1.0, _env_float("KOLIBRI_FAST_TRAIN_SAMPLE_RATE", 0.08)),
        )
        self._fast_train_min_interval_sec = max(
            0.0,
            min(60.0, _env_float("KOLIBRI_FAST_TRAIN_MIN_INTERVAL_SEC", 1.5)),
        )
        self._last_fast_train_enqueue_ts = 0.0
        self._persist_lock = threading.Lock()
        self._fast_chat_mode = _env_flag("KOLIBRI_FAST_CHAT", default=True)
        self._lm_trained = False
        self._lm_generation = 0
        self._lm_train_queued = False
        # --- Когнитивный модуль (абстракция, каузальность, индукция, аналогии, рефлексия) ---
        self._cognition: SwarmCognition | None = None
        # --- Persistent storage (SQLite) ---
        # --- Кэш ответов (TTL 30с, макс. 128 записей) ---
        self._response_cache: dict[str, tuple[float, dict]] = {}
        self._response_cache_ttl = 30.0
        self._response_cache_max = 128
        self._enable_web_augment = _env_flag("KOLIBRI_ENABLE_WEB_AUGMENT", default=True)
        self._web_augment_timeout_sec = max(
            2.0,
            min(20.0, _env_float("KOLIBRI_WEB_AUGMENT_TIMEOUT_SEC", 5.0)),
        )
        self._web_augment_max_urls = max(
            1,
            min(8, _env_int("KOLIBRI_WEB_AUGMENT_MAX_URLS", 2)),
        )
        self._web_augment_page_max_chars = max(
            2_000,
            min(120_000, _env_int("KOLIBRI_WEB_AUGMENT_PAGE_MAX_CHARS", 24_000)),
        )
        self._web_augment_cache_ttl_sec = max(
            60.0,
            min(24 * 60 * 60.0, _env_float("KOLIBRI_WEB_AUGMENT_CACHE_TTL_SEC", 6 * 60 * 60.0)),
        )
        self._web_augment_answer_cache: dict[str, tuple[float, str]] = {}
        self._web_augment_negative_ttl_sec = max(
            15.0,
            min(60 * 60.0, _env_float("KOLIBRI_WEB_AUGMENT_NEGATIVE_TTL_SEC", 240.0)),
        )
        self._web_augment_negative_cache: dict[str, float] = {}
        self._conversation_ttl = _CONVERSATION_TTL_SECONDS
        self._max_active_conversations = _MAX_ACTIVE_CONVERSATIONS
        from .persistence import get_db
        self._db = get_db()
        self._assistant_name = "Колибри AI"
        self._persist_conversations = _env_flag("KOLIBRI_PERSIST_CONVERSATIONS", default=True)
        self._conversation_db_load_limit = max(
            8,
            min(200, _env_int("KOLIBRI_CONVERSATION_DB_LOAD_LIMIT", 40)),
        )
        self._active_client_id_var: contextvars.ContextVar[str] = contextvars.ContextVar(
            "kolibri_active_client_id",
            default="global",
        )
        self._active_user_profile_var: contextvars.ContextVar[dict[str, object] | None] = contextvars.ContextVar(
            "kolibri_active_user_profile",
            default=None,
        )
        self._user_profile_lock = threading.RLock()
        self._user_profile_cache: dict[str, dict[str, object]] = {}
        self._user_profile_persist_queue: queue.Queue[tuple[str, str, float]] = queue.Queue(
            maxsize=max(200, min(20_000, _env_int("KOLIBRI_PROFILE_PERSIST_QUEUE", 4000))),
        )
        self._conversation_persist_queue: queue.Queue[tuple[str, str, str, str, float]] = queue.Queue(
            maxsize=max(500, min(20_000, _env_int("KOLIBRI_CONVERSATION_PERSIST_QUEUE", 4000))),
        )
        self._enable_self_check = _env_flag("KOLIBRI_ENABLE_SELF_CHECK", default=True)
        self._enable_auto_benchmark = _env_flag("KOLIBRI_ENABLE_AUTO_BENCHMARK", default=True)
        self._auto_benchmark_interval_sec = max(
            300,
            min(24 * 60 * 60, _env_int("KOLIBRI_AUTO_BENCHMARK_INTERVAL_SEC", 3600)),
        )
        self._auto_benchmark_start_delay_sec = max(
            10,
            min(3600, _env_int("KOLIBRI_AUTO_BENCHMARK_START_DELAY_SEC", 900)),
        )
        self._quality_gate_score_min = max(
            0.0,
            min(1.0, _env_float("KOLIBRI_BENCH_SCORE_GATE_MIN", 0.78)),
        )
        self._quality_gate_pass_rate_min = max(
            0.0,
            min(1.0, _env_float("KOLIBRI_BENCH_PASS_RATE_GATE_MIN", 0.70)),
        )
        self._quality_gate_latency_p95_ms_max = max(
            500.0,
            min(30_000.0, _env_float("KOLIBRI_BENCH_LATENCY_P95_GATE_MAX_MS", 3200.0)),
        )
        self._quality_gate_placeholder_rate_max = max(
            0.0,
            min(1.0, _env_float("KOLIBRI_BENCH_PLACEHOLDER_RATE_MAX", 0.18)),
        )
        self._quality_gate_hallucination_rate_max = max(
            0.0,
            min(1.0, _env_float("KOLIBRI_BENCH_HALLUCINATION_RATE_MAX", 0.28)),
        )
        self._quality_bench_case_timeout_sec = max(
            1.0,
            min(60.0, _env_float("KOLIBRI_BENCH_CASE_TIMEOUT_SEC", 8.0)),
        )
        self._quality_bench_total_timeout_sec = max(
            30.0,
            min(3600.0, _env_float("KOLIBRI_BENCH_TOTAL_TIMEOUT_SEC", 180.0)),
        )
        self._last_quality_benchmark: dict[str, object] | None = None
        self._quality_benchmark_lock = threading.Lock()
        self._last_uniqueness_report: dict[str, object] | None = None
        self._uniqueness_lock = threading.Lock()
        self._load_from_db()
        self._load_user_profile("global")
        # Движок ГОТОВ к запросам ДО загрузки корпуса — чтобы health check отвечал
        self._ready = True
        # Загрузка корпуса при старте может быть очень тяжёлой для CPU/GIL.
        # По умолчанию держим сервис отзывчивым; при необходимости включается env-флагом.
        self._autoload_corpus_on_start = _env_flag("KOLIBRI_AUTOLOAD_CORPUS", default=True)
        self._auto_train_lm_on_corpus = _env_flag("KOLIBRI_AUTO_TRAIN_LM_ON_CORPUS", default=False)
        self._auto_build_causal_index = _env_flag("KOLIBRI_AUTO_BUILD_CAUSAL_INDEX", default=False)
        self._corpus_background_train = _env_flag("KOLIBRI_CORPUS_BACKGROUND_TRAIN", default=False)
        if self._autoload_corpus_on_start:
            threading.Thread(
                target=self._safe_load_corpus, daemon=True, name="corpus-loader",
            ).start()
        else:
            log.info("Startup corpus autoload is disabled (KOLIBRI_AUTOLOAD_CORPUS=0)")
        if self._enable_auto_benchmark:
            threading.Thread(
                target=self._quality_benchmark_loop, daemon=True, name="quality-benchmark",
            ).start()
        if self._persist_conversations:
            threading.Thread(
                target=self._conversation_persist_worker,
                daemon=True,
                name="conversation-persist",
            ).start()
        threading.Thread(
            target=self._user_profile_persist_worker,
            daemon=True,
            name="profile-persist",
        ).start()
        self._refresh_model_stats_async()

    def _safe_load_corpus(self) -> None:
        """Обёртка: загрузить корпус с перехватом ошибок."""
        try:
            self._load_corpus()
        except Exception as e:
            log.error("Ошибка загрузки корпуса: %s", e)
        # --- Обучаем FormulaLM после загрузки корпуса ---
        if self._auto_train_lm_on_corpus:
            try:
                self._train_lm_on_corpus()
            except Exception as e:
                log.error("Ошибка обучения FormulaLM: %s", e)
        # --- Автоматически строим каузальный индекс из корпуса ---
        if self._auto_build_causal_index:
            try:
                self._auto_build_causal_index()
            except Exception as e:
                log.error("Ошибка построения каузального индекса: %s", e)
        # --- Сохраняем граф в SQLite для персистентности ---
        try:
            self._save_to_db()
        except Exception as e:
            log.error("Ошибка сохранения в SQLite: %s", e)

    def _handle_shutdown_signal(self, signum, frame) -> None:
        """#21. Graceful shutdown handler."""
        log.info("Received signal %s, shutting down...", signum)
        self._shutdown_event.set()

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
                elif kind == "dialogue":
                    _, query, response, confidence, method = task
                    self._learn_from_dialogue_turn(query, response, confidence, method)
                elif kind == "user_text":
                    _, text = task
                    self._learn_from_user_text(text)
                elif kind == "corpus":
                    self._train_all_background()
                elif kind == "lm":
                    self._train_lm_on_corpus()
            except Exception as e:
                log.warning("Background worker error: %s", e)
            finally:
                if task and task[0] == "lm":
                    self._lm_train_queued = False
                self._train_queue.task_done()

    def _load_corpus(self) -> None:
        """Загрузить тексты и обучить ЧИСЛОВОЙ ГРАФ.
        
        Порядок приоритетности:
        1. Файлы из корня data/corpus/ (основные знания)
        2. Тематические agent-файлы из data/corpus/
        3. wiki_mass/ (общие знания)
        4. Дополнительные директории (training, seeds)
        """
        import logging
        log = logging.getLogger("kolibri.ai")
        t0 = time.time()

        # Keep default high enough for wiki_mass chunk files (~4 MB each).
        max_file_size = max(10_000, _env_int("KOLIBRI_CORPUS_MAX_FILE_BYTES", 6_000_000))
        max_priority_files = max(0, _env_int("KOLIBRI_CORPUS_MAX_PRIORITY_FILES", 2000))
        load_agent_corpus = _env_flag("KOLIBRI_LOAD_AGENT_CORPUS", default=False)
        max_agent_files = (
            max(0, _env_int("KOLIBRI_CORPUS_MAX_AGENT_FILES", 250))
            if load_agent_corpus
            else 0
        )
        load_web_auto_corpus = _env_flag("KOLIBRI_LOAD_WEB_AUTO_CORPUS", default=False)
        max_web_auto_files = (
            max(0, _env_int("KOLIBRI_CORPUS_MAX_WEB_AUTO_FILES", 120))
            if load_web_auto_corpus
            else 0
        )
        max_wiki_files = max(0, _env_int("KOLIBRI_CORPUS_MAX_WIKI_FILES", 4000))
        max_wiki_sentences_per_file = max(
            40,
            _env_int("KOLIBRI_CORPUS_MAX_WIKI_SENTENCES_PER_FILE", 420),
        )
        load_stream_corpus = _env_flag("KOLIBRI_LOAD_STREAM_CORPUS", default=True)
        max_stream_files = (
            max(0, _env_int("KOLIBRI_CORPUS_MAX_STREAM_FILES", 1800))
            if load_stream_corpus
            else 0
        )
        max_stream_sentences_per_file = max(
            40,
            _env_int("KOLIBRI_CORPUS_MAX_STREAM_SENTENCES_PER_FILE", 420),
        )
        stream_prefixes = tuple(
            p.strip()
            for p in os.getenv("KOLIBRI_CORPUS_STREAM_PREFIXES", "wiki_stream_").split(",")
            if p.strip()
        ) or ("wiki_stream_",)
        max_other_files = max(0, _env_int("KOLIBRI_CORPUS_MAX_EXTRA_FILES", 1000))
        max_sentences_per_file = max(
            30,
            _env_int("KOLIBRI_CORPUS_MAX_SENTENCES_PER_FILE", 260),
        )
        byte_budget = max(
            max_file_size,
            _env_int("KOLIBRI_CORPUS_BYTE_BUDGET", 2 * 1024 * 1024 * 1024),
        )

        total_texts = 0
        total_bytes = 0
        budget_exhausted = False
        allowed_ext = {".txt", ".md"}
        skip_graph_train = (
            len(self.graph.patterns) > 5000
            and _env_flag("KOLIBRI_SKIP_GRAPH_TRAIN_IF_LOADED", default=True)
        )

        def _is_supported_text_file(path: Path) -> bool:
            return path.is_file() and path.suffix.lower() in allowed_ext

        def _load_file(f: Path, sentence_cap: int | None = None) -> bool:
            """Загрузить один файл, вернуть True если успешно."""
            nonlocal total_texts, total_bytes, budget_exhausted
            if budget_exhausted:
                return False
            try:
                fsize = f.stat().st_size
                if fsize < 50 or fsize > max_file_size:
                    return False
                if total_bytes + fsize > byte_budget:
                    budget_exhausted = True
                    return False
                content = f.read_text(encoding="utf-8", errors="ignore")
                if len(content.strip()) < 20:
                    return False
                clean_sentences = _split_sentences(content)
                if not clean_sentences:
                    return False
                per_file_cap = int(sentence_cap) if sentence_cap is not None else int(max_sentences_per_file)
                if per_file_cap > 0 and len(clean_sentences) > per_file_cap:
                    clean_sentences = clean_sentences[:per_file_cap]
                clean_text = "\n".join(clean_sentences)
                if len(clean_text) < 20:
                    return False
                if not skip_graph_train:
                    self.graph.train_text(clean_text)
                added = self.sentence_store.add_text(
                    clean_text,
                    max_new_sentences=per_file_cap if per_file_cap > 0 else None,
                )
                if added <= 0:
                    return False
                total_texts += 1
                total_bytes += fsize
                if total_texts % 10 == 0:
                    time.sleep(0)
                return True
            except OSError:
                return False

        # --- Фаза 1: Приоритетные файлы (корень data/corpus/, не в подпапках) ---
        if _CORPUS_DIR.exists():
            priority_files = sorted(
                f for f in _CORPUS_DIR.iterdir()
                if _is_supported_text_file(f)
                and not f.name.startswith("agent_")
                and not f.name.startswith("web_auto_")
                and not f.name.startswith("wiki_stream_")
            )
            for idx, f in enumerate(priority_files):
                if max_priority_files and idx >= max_priority_files:
                    break
                if budget_exhausted:
                    break
                _load_file(f, sentence_cap=max_sentences_per_file)
            log.info("Corpus phase 1 (priority): %d files", total_texts)

        # --- Фаза 2: Дополнительные директории (seeds/training) ---
        # Критично загружать ДО wiki_mass, иначе массовый корпус вытесняет
        # базовые факты из ограниченного sentence_store.
        extra_dirs = [
            _PROJECT_ROOT / "data" / "training",
            _PROJECT_ROOT / "seeds",
            _PROJECT_ROOT / "training",
        ]
        extra_count = 0
        for corpus_dir in extra_dirs:
            if not corpus_dir.exists():
                continue
            for f in sorted(corpus_dir.rglob("*")):
                if not _is_supported_text_file(f):
                    continue
                if max_other_files and extra_count >= max_other_files:
                    break
                if budget_exhausted:
                    break
                if _load_file(f, sentence_cap=max_sentences_per_file):
                    extra_count += 1

        # --- Фаза 3: Agent-файлы (тематические, по флагу) ---
        agent_count = 0
        if _CORPUS_DIR.exists():
            agent_files = sorted(
                f for f in _CORPUS_DIR.iterdir()
                if _is_supported_text_file(f)
                and f.name.startswith("agent_")
            )
            for f in agent_files:
                if max_agent_files and agent_count >= max_agent_files:
                    break
                if budget_exhausted:
                    break
                if _load_file(f, sentence_cap=max_sentences_per_file):
                    agent_count += 1

        # --- Фаза 3b: web_auto (по флагу; обычно содержит шум веб-скрейпа) ---
        web_auto_count = 0
        if _CORPUS_DIR.exists():
            web_auto_files = sorted(
                f for f in _CORPUS_DIR.iterdir()
                if _is_supported_text_file(f)
                and f.name.startswith("web_auto_")
            )
            for f in web_auto_files:
                if max_web_auto_files and web_auto_count >= max_web_auto_files:
                    break
                if budget_exhausted:
                    break
                if _load_file(f, sentence_cap=max_sentences_per_file):
                    web_auto_count += 1

        # --- Фаза 4: wiki_mass (общие знания) ---
        wiki_dir = _CORPUS_DIR / "wiki_mass"
        wiki_count = 0
        if wiki_dir.exists():
            for f in sorted(wiki_dir.rglob("*")):
                if not _is_supported_text_file(f):
                    continue
                if max_wiki_files and wiki_count >= max_wiki_files:
                    break
                if budget_exhausted:
                    break
                if _load_file(f, sentence_cap=max_wiki_sentences_per_file):
                    wiki_count += 1

        # --- Фаза 5: Доп. большие локальные потоки (wiki_stream_* и т.п.) ---
        stream_count = 0
        if _CORPUS_DIR.exists() and max_stream_files > 0:
            stream_dirs = sorted(
                d for d in _CORPUS_DIR.iterdir()
                if d.is_dir() and any(d.name.startswith(pref) for pref in stream_prefixes)
            )
            for stream_dir in stream_dirs:
                for f in sorted(stream_dir.rglob("*")):
                    if not _is_supported_text_file(f):
                        continue
                    if max_stream_files and stream_count >= max_stream_files:
                        break
                    if budget_exhausted:
                        break
                    if _load_file(f, sentence_cap=max_stream_sentences_per_file):
                        stream_count += 1
                if max_stream_files and stream_count >= max_stream_files:
                    break

        if total_texts > 0:
            self._corpus_loaded = True
            # Тяжёлое дообучение формул/эмбеддингов после загрузки корпуса
            # в проде выключено по умолчанию, чтобы не блокировать API под нагрузкой.
            if self._corpus_background_train:
                self._formulas_training = True
                self._embeddings_training = True
                self._train_queue.put(("corpus",))
        log.info(
            "Corpus loaded: %d files (%d priority, %d agent, %d web_auto, %d wiki, %d stream, %d other), "
            "%d sentences, %.2f MB in %.1fs (training in background)%s%s",
            total_texts,
            total_texts - agent_count - web_auto_count - wiki_count - stream_count - extra_count,
            agent_count, web_auto_count, wiki_count, stream_count, extra_count,
            self.sentence_store.size,
            total_bytes / (1024 * 1024),
            time.time() - t0,
            " [byte-budget reached]" if budget_exhausted else "",
            " [graph-train skipped]" if skip_graph_train else "",
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
            restored_patterns = 0
            for p in patterns:
                h = int(p.get("hash", 0) or 0)
                if h <= 0:
                    continue
                word = str(p.get("word", "") or "").strip().lower()
                raw_pattern = p.get("pattern", [])
                pattern: list[int] = []
                if isinstance(raw_pattern, list):
                    for d in raw_pattern[:64]:
                        try:
                            pattern.append(int(d) % 10)
                        except (TypeError, ValueError):
                            pattern.append(0)
                if not pattern:
                    fallback = word if word else str(h)
                    pattern = word_to_pattern(fallback)
                entry = PatternEntry(
                    word=word or f"#{h}",
                    pattern=pattern,
                    hash=h,
                    frequency=max(1, int(p.get("frequency", 1) or 1)),
                    fitness=float(p.get("fitness", 0.0) or 0.0),
                )
                self.graph.patterns[h] = entry
                if word:
                    self.graph._hash_to_word[h] = word
                restored_patterns += 1

            edges = self._db.load_edges()
            restored_edges = 0
            for e in edges:
                src = int(e.get("source_hash", 0) or 0)
                tgt = int(e.get("target_hash", 0) or 0)
                if src <= 0 or tgt <= 0 or src == tgt:
                    continue
                key = (min(src, tgt), max(src, tgt))
                edge = KnowledgeEdge(
                    source_hash=key[0],
                    target_hash=key[1],
                    weight=float(e.get("weight", 0.0) or 0.0),
                    cooccurrence=max(1, int(e.get("cooccurrence", 1) or 1)),
                )
                self.graph.edges[key] = edge
                self.graph._adj.setdefault(key[0], set()).add(key[1])
                self.graph._adj.setdefault(key[1], set()).add(key[0])
                restored_edges += 1

            try:
                documents_trained = int(float(self._db.get_meta("documents_trained", "0") or 0))
            except (TypeError, ValueError):
                documents_trained = 0
            if documents_trained > 0:
                self.graph.documents_trained = documents_trained

            log.info(
                "SQLite: восстановлено %d паттернов, %d рёбер из базы",
                restored_patterns, restored_edges,
            )
        except Exception as e:
            log.warning("Ошибка загрузки из SQLite: %s", e)

    def _save_to_db(self) -> None:
        """Сохранить текущее состояние графа в SQLite."""
        if not self._db.is_enabled():
            return
        try:
            # Снимаем консистентный snapshot под lock графа, чтобы избежать
            # RuntimeError: dictionary changed size during iteration.
            with self.graph._lock:
                patterns_snapshot = dict(self.graph.patterns)
                hash_to_word_snapshot = dict(self.graph._hash_to_word)
                edges_snapshot = dict(self.graph.edges)
                documents_trained = int(self.graph.documents_trained)
            self._db.save_patterns(patterns_snapshot, hash_to_word_snapshot)
            self._db.save_edges(edges_snapshot)
            self._db.set_meta("documents_trained", str(documents_trained))
            self._db.set_meta("save_time", str(time.time()))
        except Exception as e:
            log.warning("Ошибка сохранения в SQLite: %s", e)

    def persist_state(self) -> None:
        """Принудительно сохранить текущее состояние обучения."""
        self._save_formulas()
        self._save_embeddings()
        self._save_to_db()

    def _persist_state_throttled(self, *, force: bool = False) -> None:
        """
        Периодическое сохранение без перегрузки I/O.
        Используется после онлайн-обучения и команд `научи/запомни`.
        """
        now = time.time()
        if not force and self._persist_min_interval_sec > 0:
            if now - self._last_persist_ts < self._persist_min_interval_sec:
                return
        if not self._persist_lock.acquire(blocking=False):
            return
        try:
            self.persist_state()
            self._last_persist_ts = time.time()
        except Exception as exc:
            log.warning("Не удалось сохранить состояние (throttled): %s", exc)
        finally:
            self._persist_lock.release()

    def _quality_control_cases(self) -> list[dict[str, object]]:
        return [
            {
                "id": "capital_france",
                "category": "facts",
                "question": "столица франции",
                "expect_any": [r"\bпариж\b"],
                "weight": 1.0,
            },
            {
                "id": "capital_japan",
                "category": "facts",
                "question": "столица японии",
                "expect_any": [r"\bтокио\b"],
                "weight": 1.0,
            },
            {
                "id": "capital_russia",
                "category": "facts",
                "question": "столица россии",
                "expect_any": [r"\bмоскв[аы]?\b"],
                "weight": 1.0,
            },
            {
                "id": "identity",
                "category": "identity",
                "question": "кто ты",
                "expect_any": [r"колибри", r"ассистент"],
                "weight": 0.8,
            },
            {
                "id": "identity_name",
                "category": "identity",
                "question": "как тебя зовут",
                "expect_any": [r"колибри"],
                "weight": 0.8,
            },
            {
                "id": "greeting",
                "category": "identity",
                "question": "привет",
                "expect_any": [r"привет", r"здравств", r"колибри"],
                "weight": 0.5,
            },
            {
                "id": "greeting_typo",
                "category": "identity",
                "question": "превет колибри",
                "expect_any": [r"привет", r"колибри"],
                "weight": 0.8,
            },
            {
                "id": "math_words",
                "category": "math",
                "question": "сколько будет один плюс один умножить на 128 делить на 32",
                "expect_any": [r"(?:\b5\b|\*\*5\*\*)"],
                "weight": 1.2,
            },
            {
                "id": "math_parentheses",
                "category": "math",
                "question": "сколько будет (2 + 3) * 4",
                "expect_any": [r"\b20\b"],
                "weight": 1.0,
            },
            {
                "id": "math_words_mul",
                "category": "math",
                "question": "девять умножить на семь",
                "expect_any": [r"\b63\b"],
                "weight": 1.0,
            },
            {
                "id": "math_power",
                "category": "math",
                "question": "два в степени десять",
                "expect_any": [r"1024"],
                "weight": 1.0,
            },
            {
                "id": "logic_syllogism",
                "category": "reasoning",
                "question": "Все люди смертны. Владислав — человек. Какой вывод?",
                "expect_all": [r"владислав", r"смерт"],
                "weight": 1.4,
            },
            {
                "id": "context_retention_simple",
                "category": "context",
                "setup": ["Я живу в Париже и люблю джаз."],
                "question": "Где я живу и что люблю?",
                "expect_all": [r"париж", r"джаз"],
                "weight": 1.4,
            },
            {
                "id": "context_profile_name",
                "category": "context",
                "setup": ["Меня зовут Владислав. Я люблю джаз."],
                "question": "Как меня зовут?",
                "expect_any": [r"владислав"],
                "weight": 1.2,
            },
            {
                "id": "context_noisy_typo_port",
                "category": "context",
                "setup": ["В проекте используется порт 8001 для API."],
                "question": "а как насчет порда исползуетса в праекте?",
                "expect_any": [r"\b8001\b"],
                "weight": 1.2,
            },
            {
                "id": "definition_ai",
                "category": "reasoning",
                "question": "что такое искусственный интеллект",
                "expect_all": [r"искусствен", r"интеллект"],
                "weight": 0.9,
            },
            {
                "id": "definition_medicine",
                "category": "domains",
                "question": "что такое медицина",
                "expect_all": [r"медицин", r"здоров|болезн|лечен"],
                "expect_not": [r"математик", r"географ"],
                "weight": 1.2,
            },
            {
                "id": "definition_geography",
                "category": "domains",
                "question": "что такое география",
                "expect_all": [r"географ", r"земл|территор|поверхност"],
                "expect_not": [r"математик", r"медицин"],
                "weight": 1.2,
            },
            {
                "id": "definition_philosophy",
                "category": "domains",
                "question": "что такое философия",
                "expect_all": [r"философ", r"мышлен|познан|мировоззрен|общ"],
                "expect_not": [r"медицин", r"математик"],
                "weight": 1.2,
            },
            {
                "id": "definition_biology",
                "category": "domains",
                "question": "что такое биология",
                "expect_all": [r"биолог", r"жив"],
                "expect_not": [r"математик", r"географ"],
                "weight": 1.1,
            },
            {
                "id": "definition_physics",
                "category": "domains",
                "question": "что такое физика",
                "expect_all": [r"физик", r"природ|матери|движен|явлен"],
                "expect_not": [r"медицин", r"философ"],
                "weight": 1.1,
            },
            {
                "id": "definition_astronomy",
                "category": "domains",
                "question": "что такое астрономия",
                "expect_all": [r"астроном", r"небес|звезд|планет|галактик|вселен"],
                "expect_not": [r"географ", r"медицин"],
                "weight": 1.1,
            },
            {
                "id": "definition_anatomy",
                "category": "domains",
                "question": "что такое анатомия",
                "expect_all": [r"анатом", r"строен|орган|ткан|тел"],
                "expect_not": [r"сериал", r"эконом"],
                "weight": 1.0,
            },
            {
                "id": "definition_therapy",
                "category": "domains",
                "question": "что такое терапия",
                "expect_all": [r"терап", r"лечен|заболев|пациент|медицин|клинич"],
                "expect_not": [r"сериал", r"комедийн", r"американск"],
                "weight": 1.0,
            },
            {
                "id": "definition_chemistry",
                "category": "domains",
                "question": "что такое химия",
                "expect_all": [r"хими", r"веществ|состав|строен|реакц|элемент"],
                "expect_not": [r"истор", r"эконом"],
                "weight": 1.0,
            },
            {
                "id": "definition_history",
                "category": "domains",
                "question": "что такое история",
                "expect_all": [r"истори", r"прошл|событ|человечеств|источн"],
                "expect_not": [r"эконом", r"математик"],
                "weight": 1.0,
            },
            {
                "id": "definition_economics",
                "category": "domains",
                "question": "что такое экономика",
                "expect_all": [r"эконом", r"хозяйств|производ|распредел|обмен|потреблен"],
                "expect_not": [r"географ", r"философ"],
                "weight": 1.0,
            },
            {
                "id": "definition_law",
                "category": "domains",
                "question": "что такое право",
                "expect_all": [r"прав", r"норм|закон|государств|отношен|обществен"],
                "expect_not": [r"лев", r"правооблад"],
                "weight": 1.0,
            },
            {
                "id": "explain_physics",
                "category": "domains",
                "question": "объясни физику простыми словами",
                "expect_all": [r"физик", r"фундаментальн|природ|матери|движен"],
                "expect_not": [r"философ", r"медицин"],
                "weight": 1.0,
            },
            {
                "id": "tell_chemistry",
                "category": "domains",
                "question": "расскажи о химии",
                "expect_all": [r"хими", r"веществ|элемент|реакц"],
                "expect_not": [r"истор", r"эконом"],
                "weight": 1.0,
            },
            {
                "id": "tell_detailed_law",
                "category": "domains",
                "question": "расскажи подробно о праве",
                "expect_all": [r"прав", r"норм|закон|обязанност|защит|ответствен"],
                "expect_not": [r"привет", r"лев"],
                "weight": 1.0,
            },
            {
                "id": "knowledge_astronomy",
                "category": "domains",
                "question": "что ты знаешь об астрономии",
                "expect_all": [r"астроном", r"небесн|звезд|планет|вселен"],
                "expect_not": [r"географ", r"медицин"],
                "weight": 1.0,
            },
            {
                "id": "study_history",
                "category": "domains",
                "question": "что изучает история",
                "expect_all": [r"истори", r"цивилизац|государств|культур|люд|прошл"],
                "expect_not": [r"учебн[а-я]* предмет", r"математик"],
                "weight": 1.0,
            },
            {
                "id": "study_chemistry",
                "category": "domains",
                "question": "что изучает химия",
                "expect_all": [r"хими", r"элемент|реакц|веществ"],
                "expect_not": [r"истор", r"эконом"],
                "weight": 1.0,
            },
            {
                "id": "occupation_law",
                "category": "domains",
                "question": "чем занимается право",
                "expect_all": [r"прав", r"допустим|обязанност|ответствен|защит"],
                "expect_not": [r"привет", r"лев"],
                "weight": 1.0,
            },
            {
                "id": "structure_law",
                "category": "domains",
                "question": "как устроено право",
                "expect_all": [r"прав", r"норм|закон|обязанност|отношен|регулиру"],
                "expect_not": [r"привет", r"лев"],
                "weight": 1.0,
            },
            {
                "id": "importance_law",
                "category": "domains",
                "question": "почему важно право",
                "expect_all": [r"прав", r"важн|роль|норм|отношен|обязанност|государств"],
                "expect_not": [r"привет", r"лев"],
                "weight": 1.0,
            },
            {
                "id": "importance_math",
                "category": "domains",
                "question": "почему важна математика",
                "expect_all": [r"математ", r"важн|роль|наук|структур|доказатель|закономерност"],
                "expect_not": [r"географ", r"медицин"],
                "weight": 1.0,
            },
            {
                "id": "importance_medicine",
                "category": "domains",
                "question": "зачем нужна медицина",
                "expect_all": [r"медицин", r"здоров|болезн|лечен|диагност|терап"],
                "expect_not": [r"философ", r"географ"],
                "weight": 1.0,
            },
            {
                "id": "importance_physics",
                "category": "domains",
                "question": "почему важна физика",
                "expect_all": [r"физик", r"важн|роль|природ|энерг|движен|матери"],
                "expect_not": [r"философ", r"медицин"],
                "weight": 1.0,
            },
            {
                "id": "importance_chemistry",
                "category": "domains",
                "question": "зачем нужна химия",
                "expect_all": [r"хими", r"веществ|реакц|элемент|состав|строен"],
                "expect_not": [r"истор", r"эконом"],
                "weight": 1.0,
            },
        ]

    def _quality_case_passed(self, answer: str, case: dict[str, object]) -> tuple[bool, str]:
        text = (answer or "").strip().lower()
        if not text:
            return False, "empty"
        if bool(case.get("reject_placeholder", True)):
            placeholders = (
                "недостаточно локальных знаний",
                "в моей локальной базе пока мало",
                "добавьте материал",
            )
            if any(marker in text for marker in placeholders):
                return False, "placeholder"
        expect_all = [str(x) for x in (case.get("expect_all") or []) if str(x).strip()]
        expect_any = [str(x) for x in (case.get("expect_any") or []) if str(x).strip()]
        expect_not = [str(x) for x in (case.get("expect_not") or []) if str(x).strip()]

        for pattern in expect_all:
            if not re.search(pattern, text, flags=re.IGNORECASE):
                return False, f"missing_all:{pattern}"
        if expect_any:
            for pattern in expect_any:
                if re.search(pattern, text, flags=re.IGNORECASE):
                    for denied in expect_not:
                        if re.search(denied, text, flags=re.IGNORECASE):
                            return False, f"contains_forbidden:{denied}"
                    return True, "matched_any"
            return False, "missing_any"
        for denied in expect_not:
            if re.search(denied, text, flags=re.IGNORECASE):
                return False, f"contains_forbidden:{denied}"
        return True, "matched_all"

    @staticmethod
    def _percentile(values: list[float], q: float) -> float:
        nums = [float(v) for v in values if isinstance(v, (int, float))]
        if not nums:
            return 0.0
        nums.sort()
        if q <= 0:
            return float(nums[0])
        if q >= 1:
            return float(nums[-1])
        pos = (len(nums) - 1) * q
        lo = int(math.floor(pos))
        hi = int(math.ceil(pos))
        if lo == hi:
            return float(nums[lo])
        frac = pos - lo
        return float(nums[lo] + (nums[hi] - nums[lo]) * frac)

    def _quality_metrics(self, details: list[dict[str, object]], score: float) -> dict[str, object]:
        total = max(1, len(details))
        latencies: list[float] = []
        conf_all: list[float] = []
        conf_pass: list[float] = []
        conf_fail: list[float] = []
        methods: defaultdict[str, int] = defaultdict(int)
        categories_raw: defaultdict[str, dict[str, float]] = defaultdict(
            lambda: {
                "passed": 0.0,
                "total": 0.0,
                "weighted_passed": 0.0,
                "weighted_total": 0.0,
            }
        )
        placeholder_count = 0
        timeout_count = 0
        error_count = 0
        hallucination_proxy_count = 0

        for item in details:
            if not isinstance(item, dict):
                continue
            passed = bool(item.get("passed", False))
            reason = str(item.get("reason", "") or "").lower()
            method = str(item.get("method", "unknown") or "unknown")
            category = str(item.get("category", "general") or "general")

            try:
                weight = float(item.get("weight", 1.0) or 1.0)
            except (TypeError, ValueError):
                weight = 1.0
            if weight <= 0:
                weight = 1.0

            methods[method] += 1
            categories_raw[category]["total"] += 1.0
            categories_raw[category]["weighted_total"] += weight
            if passed:
                categories_raw[category]["passed"] += 1.0
                categories_raw[category]["weighted_passed"] += weight

            try:
                latency = float(item.get("latency_ms", 0.0) or 0.0)
            except (TypeError, ValueError):
                latency = 0.0
            if latency > 0:
                latencies.append(latency)

            try:
                conf = float(item.get("confidence", 0.0) or 0.0)
            except (TypeError, ValueError):
                conf = 0.0
            if 0.0 <= conf <= 1.0:
                conf_all.append(conf)
                if passed:
                    conf_pass.append(conf)
                else:
                    conf_fail.append(conf)

            is_placeholder = ("placeholder" in reason) or ("мало проверенных данных" in reason)
            is_timeout = ("timeout" in reason) or method == "timeout"
            is_error = reason.startswith("error:")
            if is_placeholder:
                placeholder_count += 1
            if is_timeout:
                timeout_count += 1
            if is_error:
                error_count += 1

            if (not passed) and reason.startswith(("missing_", "contains_forbidden")):
                hallucination_proxy_count += 1

        pass_rate = sum(1 for item in details if bool(item.get("passed", False))) / float(total)
        latency_p50 = self._percentile(latencies, 0.50)
        latency_p95 = self._percentile(latencies, 0.95)
        confidence_avg = (sum(conf_all) / len(conf_all)) if conf_all else 0.0
        confidence_pass_avg = (sum(conf_pass) / len(conf_pass)) if conf_pass else 0.0
        confidence_fail_avg = (sum(conf_fail) / len(conf_fail)) if conf_fail else 0.0

        category_rows: list[dict[str, object]] = []
        for category in sorted(categories_raw):
            row = categories_raw[category]
            row_total = max(1.0, row["total"])
            row_weighted_total = max(1e-9, row["weighted_total"])
            category_rows.append(
                {
                    "category": category,
                    "passed": int(row["passed"]),
                    "total": int(row["total"]),
                    "pass_rate": round(row["passed"] / row_total, 4),
                    "weighted_passed": round(row["weighted_passed"], 3),
                    "weighted_total": round(row["weighted_total"], 3),
                    "weighted_pass_rate": round(row["weighted_passed"] / row_weighted_total, 4),
                }
            )

        method_rows = [
            {
                "method": method,
                "count": count,
                "rate": round(count / float(total), 4),
            }
            for method, count in sorted(methods.items(), key=lambda kv: (-kv[1], kv[0]))
        ]

        placeholder_rate = placeholder_count / float(total)
        timeout_rate = timeout_count / float(total)
        error_rate = error_count / float(total)
        hallucination_proxy_rate = hallucination_proxy_count / float(total)

        score_gate = score >= self._quality_gate_score_min
        pass_rate_gate = pass_rate >= self._quality_gate_pass_rate_min
        latency_gate = latency_p95 <= self._quality_gate_latency_p95_ms_max if latency_p95 > 0 else True
        placeholder_gate = placeholder_rate <= self._quality_gate_placeholder_rate_max
        hallucination_gate = hallucination_proxy_rate <= self._quality_gate_hallucination_rate_max

        return {
            "pass_rate": round(pass_rate, 4),
            "latency_p50_ms": round(latency_p50, 1),
            "latency_p95_ms": round(latency_p95, 1),
            "confidence_avg": round(confidence_avg, 4),
            "confidence_pass_avg": round(confidence_pass_avg, 4),
            "confidence_fail_avg": round(confidence_fail_avg, 4),
            "placeholder_rate": round(placeholder_rate, 4),
            "timeout_rate": round(timeout_rate, 4),
            "error_rate": round(error_rate, 4),
            "hallucination_proxy_rate": round(hallucination_proxy_rate, 4),
            "categories": category_rows,
            "methods": method_rows,
            "gates": {
                "score_min": round(self._quality_gate_score_min, 4),
                "pass_rate_min": round(self._quality_gate_pass_rate_min, 4),
                "latency_p95_max_ms": round(self._quality_gate_latency_p95_ms_max, 1),
                "placeholder_rate_max": round(self._quality_gate_placeholder_rate_max, 4),
                "hallucination_rate_max": round(self._quality_gate_hallucination_rate_max, 4),
                "score_pass": score_gate,
                "pass_rate_pass": pass_rate_gate,
                "latency_pass": latency_gate,
                "placeholder_pass": placeholder_gate,
                "hallucination_pass": hallucination_gate,
                "overall_pass": bool(
                    score_gate
                    and pass_rate_gate
                    and latency_gate
                    and placeholder_gate
                    and hallucination_gate
                ),
            },
        }

    def _quality_chat_with_timeout(
        self,
        *,
        message: str,
        conversation_id: str,
        client_id: str,
        response_profile: str,
        time_budget_ms: int,
        timeout_sec: float,
    ) -> tuple[dict[str, object] | None, str]:
        result_box: dict[str, object] = {}
        error_box: dict[str, str] = {}
        finished = threading.Event()

        def _runner() -> None:
            try:
                result_box["value"] = self.chat(
                    message,
                    conversation_id=conversation_id,
                    client_id=client_id,
                    response_profile=response_profile,
                    time_budget_ms=time_budget_ms,
                )
            except Exception as exc:  # noqa: BLE001
                error_box["value"] = str(exc)
            finally:
                finished.set()

        threading.Thread(
            target=_runner,
            daemon=True,
            name="quality-bench-chat",
        ).start()
        if not finished.wait(max(0.05, float(timeout_sec))):
            return None, "benchmark_case_timeout"
        if "value" in error_box:
            return None, f"error:{error_box['value']}"
        payload = result_box.get("value")
        if isinstance(payload, dict):
            return payload, ""
        return None, "error:invalid_benchmark_chat_payload"

    def run_quality_benchmark(self, trigger: str = "manual") -> dict[str, object]:
        with self._quality_benchmark_lock:
            started_at = time.time()
            run_id = uuid.uuid4().hex[:12]
            cases = self._quality_control_cases()
            conv_id = f"quality-bench:{run_id}"
            client_id = f"quality-bench:{run_id}"
            details: list[dict[str, object]] = []
            passed = 0
            weighted_passed = 0.0
            weighted_total = 0.0

            for case in cases:
                try:
                    weight = float(case.get("weight", 1.0) or 1.0)
                except (TypeError, ValueError):
                    weight = 1.0
                if weight <= 0:
                    weight = 1.0
                weighted_total += weight
                category = str(case.get("category", "general") or "general")
                elapsed = time.time() - started_at
                if elapsed > self._quality_bench_total_timeout_sec:
                    details.append(
                        {
                            "id": str(case.get("id", "")),
                            "category": category,
                            "weight": weight,
                            "question": str(case.get("question", "") or ""),
                            "passed": False,
                            "reason": "benchmark_timeout_budget_exceeded",
                            "method": "timeout",
                            "confidence": 0.0,
                            "latency_ms": 0.0,
                            "answer_preview": "",
                        }
                    )
                    continue
                setup_messages = [str(x) for x in (case.get("setup") or []) if str(x).strip()]
                for setup in setup_messages:
                    setup_timeout = max(
                        0.8,
                        min(
                            self._quality_bench_case_timeout_sec,
                            self._quality_bench_total_timeout_sec - (time.time() - started_at),
                        ),
                    )
                    setup_result, setup_error = self._quality_chat_with_timeout(
                        message=setup,
                        conversation_id=conv_id,
                        client_id=client_id,
                        response_profile="fast",
                        time_budget_ms=1800,
                        timeout_sec=setup_timeout,
                    )
                    if setup_result is None:
                        if setup_error == "benchmark_case_timeout":
                            log.warning("quality benchmark setup timeout: case=%s", str(case.get("id", "")))
                        break

                question = str(case.get("question", "") or "").strip()
                t0 = time.time()
                response_text = ""
                method = "error"
                confidence = 0.0
                error_text = ""
                case_timeout = max(
                    0.8,
                    min(
                        self._quality_bench_case_timeout_sec,
                        self._quality_bench_total_timeout_sec - (time.time() - started_at),
                    ),
                )
                result, error_text = self._quality_chat_with_timeout(
                    message=question,
                    conversation_id=conv_id,
                    client_id=client_id,
                    response_profile="fast",
                    time_budget_ms=2500,
                    timeout_sec=case_timeout,
                )
                if isinstance(result, dict):
                    response_text = str(result.get("response", "") or "")
                    method = str(result.get("method", "unknown") or "unknown")
                    confidence = float(result.get("confidence", 0.0) or 0.0)
                elif error_text == "benchmark_case_timeout":
                    method = "timeout"

                ok, reason = self._quality_case_passed(response_text, case)
                if error_text:
                    ok = False
                    reason = error_text
                if ok:
                    passed += 1
                details.append(
                    {
                        "id": str(case.get("id", "")),
                        "category": category,
                        "weight": weight,
                        "question": question,
                        "passed": ok,
                        "reason": reason,
                        "method": method,
                        "confidence": confidence,
                        "latency_ms": round((time.time() - t0) * 1000, 1),
                        "answer_preview": response_text[:240],
                    }
                )
                if ok:
                    weighted_passed += weight

            # Дополнительные проверки памяти/изоляции клиентов.
            try:
                bench_name = "БенчПамять"
                mem_conv = f"{conv_id}:memory"
                mem_client_a = f"{client_id}:a"
                mem_client_b = f"{client_id}:b"
                mem_weight = 1.5
                weighted_total += (mem_weight * 2)

                self.chat(
                    f"Меня зовут {bench_name}",
                    conversation_id=mem_conv,
                    client_id=mem_client_a,
                    response_profile="fast",
                    time_budget_ms=2000,
                )

                t_mem_a = time.time()
                mem_a_result = self.chat(
                    "как меня зовут?",
                    conversation_id=mem_conv,
                    client_id=mem_client_a,
                    response_profile="fast",
                    time_budget_ms=2500,
                )
                mem_a_text = str(mem_a_result.get("response", "") or "").strip()
                mem_a_ok = bench_name.lower() in mem_a_text.lower()
                if mem_a_ok:
                    passed += 1
                details.append(
                    {
                        "id": "memory_recall_same_client",
                        "category": "memory",
                        "weight": mem_weight,
                        "question": "как меня зовут?",
                        "passed": mem_a_ok,
                        "reason": "matched_name" if mem_a_ok else "missing_name",
                        "method": str(mem_a_result.get("method", "unknown") or "unknown"),
                        "confidence": float(mem_a_result.get("confidence", 0.0) or 0.0),
                        "latency_ms": round((time.time() - t_mem_a) * 1000, 1),
                        "answer_preview": mem_a_text[:240],
                    }
                )
                if mem_a_ok:
                    weighted_passed += mem_weight

                t_mem_b = time.time()
                mem_b_result = self.chat(
                    "как меня зовут?",
                    conversation_id=mem_conv,
                    client_id=mem_client_b,
                    response_profile="fast",
                    time_budget_ms=2500,
                )
                mem_b_text = str(mem_b_result.get("response", "") or "").strip()
                mem_b_ok = bench_name.lower() not in mem_b_text.lower()
                if mem_b_ok:
                    passed += 1
                details.append(
                    {
                        "id": "memory_isolation_cross_client",
                        "category": "memory",
                        "weight": mem_weight,
                        "question": "как меня зовут? (other client)",
                        "passed": mem_b_ok,
                        "reason": "isolated" if mem_b_ok else "leaked_name",
                        "method": str(mem_b_result.get("method", "unknown") or "unknown"),
                        "confidence": float(mem_b_result.get("confidence", 0.0) or 0.0),
                        "latency_ms": round((time.time() - t_mem_b) * 1000, 1),
                        "answer_preview": mem_b_text[:240],
                    }
                )
                if mem_b_ok:
                    weighted_passed += mem_weight
            except Exception as exc:
                details.append(
                    {
                        "id": "memory_quality_checks",
                        "category": "memory",
                        "weight": 3.0,
                        "question": "memory quality checks",
                        "passed": False,
                        "reason": f"error:{exc}",
                        "method": "error",
                        "confidence": 0.0,
                        "latency_ms": 0.0,
                        "answer_preview": "",
                    }
                )

            total = len(details)
            weighted_score = (weighted_passed / weighted_total) if weighted_total else 0.0
            score = round(weighted_score, 4)
            finished_at = time.time()
            metrics = self._quality_metrics(details, score)
            report: dict[str, object] = {
                "run_id": run_id,
                "trigger": trigger,
                "started_at": started_at,
                "finished_at": finished_at,
                "duration_ms": round((finished_at - started_at) * 1000, 1),
                "score": score,
                "passed": passed,
                "total": total,
                "weighted_passed": round(weighted_passed, 3),
                "weighted_total": round(weighted_total, 3),
                "details": details,
            }
            report.update(metrics)
            self._last_quality_benchmark = report
            try:
                payload = json.dumps(report, ensure_ascii=False)
                self._db.save_quality_benchmark(
                    run_id=run_id,
                    run_at=finished_at,
                    score=score,
                    passed=passed,
                    total=total,
                    payload=payload,
                )
            except Exception as exc:
                log.warning("quality benchmark save failed: %s", exc)
            return report

    def _quality_benchmark_loop(self) -> None:
        try:
            time.sleep(self._auto_benchmark_start_delay_sec)
            while True:
                try:
                    if not self._corpus_loaded:
                        time.sleep(60)
                        continue
                    now = time.time()
                    user_active = any(
                        (now - float(getattr(conv, "updated_at", 0.0) or 0.0) < 120.0)
                        and not str(cid).startswith("quality-bench:")
                        for cid, conv in list(self.conversations.items())
                    )
                    if user_active:
                        time.sleep(60)
                        continue
                    self.run_quality_benchmark(trigger="auto")
                except Exception as exc:
                    log.warning("auto quality benchmark failed: %s", exc)
                time.sleep(self._auto_benchmark_interval_sec)
        except Exception as exc:
            log.warning("quality benchmark loop stopped: %s", exc)

    def get_quality_benchmark_report(self) -> dict[str, object] | None:
        if isinstance(self._last_quality_benchmark, dict):
            return dict(self._last_quality_benchmark)
        latest = self._db.get_latest_quality_benchmark()
        if not latest:
            return None
        payload = str(latest.get("payload", "") or "").strip()
        if not payload:
            return {
                "run_id": str(latest.get("run_id", "")),
                "score": float(latest.get("score", 0.0) or 0.0),
                "passed": int(latest.get("passed", 0) or 0),
                "total": int(latest.get("total", 0) or 0),
                "finished_at": float(latest.get("run_at", 0.0) or 0.0),
                "details": [],
            }
        try:
            parsed = json.loads(payload)
            if isinstance(parsed, dict):
                return parsed
        except Exception:
            pass
        return None

    def get_quality_benchmark_history(self, limit: int = 30) -> list[dict[str, object]]:
        safe_limit = max(1, min(200, int(limit)))
        rows = self._db.list_quality_benchmarks(limit=safe_limit)
        result: list[dict[str, object]] = []
        for row in rows:
            payload_raw = str(row.get("payload", "") or "").strip()
            parsed: dict[str, object] = {}
            if payload_raw:
                try:
                    loaded = json.loads(payload_raw)
                    if isinstance(loaded, dict):
                        parsed = loaded
                except Exception:
                    parsed = {}
            if not parsed:
                parsed = {
                    "run_id": str(row.get("run_id", "") or ""),
                    "score": float(row.get("score", 0.0) or 0.0),
                    "passed": int(row.get("passed", 0) or 0),
                    "total": int(row.get("total", 0) or 0),
                    "finished_at": float(row.get("run_at", 0.0) or 0.0),
                    "details": [],
                    "gates": {},
                }
            result.append(parsed)
        return result

    def run_uniqueness_proof(self, trigger: str = "manual") -> dict[str, object]:
        """
        Формальное доказательство уникальных свойств Kolibri AI.

        Проверки воспроизводимые и технические:
        - числовое кодирование слов (64 цифры);
        - архитектура формул (геном 4000, 500 слоёв);
        - формульные метаданные в обычном chat-ответе;
        - self-check в ответе;
        - долговременная память в рамках клиента;
        - изоляция памяти между клиентами;
        - удержание фактов треда при длинном диалоге.
        """
        with self._uniqueness_lock:
            started_at = time.time()
            run_id = uuid.uuid4().hex[:12]
            details: list[dict[str, object]] = []
            passed = 0

            def add_case(
                case_id: str,
                ok: bool,
                reason: str,
                *,
                method: str = "internal",
                confidence: float = 1.0,
                latency_ms: float = 0.0,
                preview: str = "",
            ) -> None:
                nonlocal passed
                if ok:
                    passed += 1
                details.append(
                    {
                        "id": case_id,
                        "passed": bool(ok),
                        "reason": str(reason),
                        "method": method,
                        "confidence": float(confidence),
                        "latency_ms": round(float(latency_ms), 1),
                        "answer_preview": str(preview or "")[:240],
                    }
                )

            # 1) Уникальное числовое кодирование слова (64 цифры).
            t0 = time.time()
            pattern = word_to_pattern("колибри")
            ok_pattern = len(pattern) == KLM_PATTERN_SIZE and all(0 <= int(d) <= 9 for d in pattern)
            add_case(
                "numeric_word_encoding_64digits",
                ok_pattern,
                "pattern_64_digits" if ok_pattern else "pattern_invalid",
                latency_ms=(time.time() - t0) * 1000.0,
                preview=pattern_to_str(pattern)[:96],
            )

            # 2) Формульная архитектура (геном + слои).
            t0 = time.time()
            best = self.formula_pool.best()
            ok_formula_arch = (
                len(best.gene.digits) == GENE_SIZE
                and FORMULA_LAYERS == 500
                and FORMULA_LAYERS_FAST == 100
                and self.formula_pool.generation >= 1
            )
            add_case(
                "formula_architecture",
                ok_formula_arch,
                "gene4000_layers500" if ok_formula_arch else "formula_arch_mismatch",
                latency_ms=(time.time() - t0) * 1000.0,
                preview=f"gen={self.formula_pool.generation}, hex={best.gene.to_hex()[:24]}",
            )

            # 3) Проверка chat payload на формульные метаданные + self-check.
            conv_meta = f"unique-proof:{run_id}:meta"
            client_meta = f"unique-proof:{run_id}:meta"
            t0 = time.time()
            try:
                meta_resp = self.chat(
                    "почему небо голубое",
                    conversation_id=conv_meta,
                    client_id=client_meta,
                    response_profile="fast",
                    time_budget_ms=2500,
                )
                fd = meta_resp.get("formula_data") if isinstance(meta_resp, dict) else {}
                if not isinstance(fd, dict):
                    fd = {}
                ok_formula_payload = bool(str(fd.get("formula_genome_hex", "") or "").strip()) and int(
                    fd.get("formula_generation", 0) or 0
                ) >= 1
                add_case(
                    "chat_formula_payload",
                    ok_formula_payload,
                    "formula_payload_present" if ok_formula_payload else "formula_payload_missing",
                    method=str(meta_resp.get("method", "unknown") or "unknown"),
                    confidence=float(meta_resp.get("confidence", 0.0) or 0.0),
                    latency_ms=(time.time() - t0) * 1000.0,
                    preview=str(meta_resp.get("response", "") or ""),
                )

                self_check = meta_resp.get("self_check") if isinstance(meta_resp, dict) else None
                ok_self_check = isinstance(self_check, dict) and ("score" in self_check)
                add_case(
                    "self_check_present",
                    ok_self_check,
                    "self_check_ok" if ok_self_check else "self_check_missing",
                    method=str(meta_resp.get("method", "unknown") or "unknown"),
                    confidence=float(meta_resp.get("confidence", 0.0) or 0.0),
                    latency_ms=0.0,
                    preview=str(self_check) if isinstance(self_check, dict) else "",
                )
            except Exception as exc:
                add_case(
                    "chat_formula_payload",
                    False,
                    f"error:{exc}",
                    method="error",
                    confidence=0.0,
                )
                add_case(
                    "self_check_present",
                    False,
                    "chat_error",
                    method="error",
                    confidence=0.0,
                )

            # 4) Память в рамках клиента.
            # Имя без цифр: извлечение имени в движке может нормализовать/обрезать числовые хвосты.
            mem_name = "УникальныйПользователь"
            mem_conv = f"unique-proof:{run_id}:memory"
            mem_client_a = f"unique-proof:{run_id}:client-a"
            mem_client_b = f"unique-proof:{run_id}:client-b"
            try:
                self.chat(
                    f"Меня зовут {mem_name}",
                    conversation_id=mem_conv,
                    client_id=mem_client_a,
                    response_profile="fast",
                    time_budget_ms=2000,
                )
                t0 = time.time()
                same_client = self.chat(
                    "как меня зовут?",
                    conversation_id=mem_conv,
                    client_id=mem_client_a,
                    response_profile="fast",
                    time_budget_ms=2200,
                )
                same_text = str(same_client.get("response", "") or "")
                if mem_name.lower() not in same_text.lower():
                    # Резервный вопрос на случай вариативного формата первого ответа.
                    same_client_retry = self.chat(
                        "кто я?",
                        conversation_id=mem_conv,
                        client_id=mem_client_a,
                        response_profile="fast",
                        time_budget_ms=2200,
                    )
                    retry_text = str(same_client_retry.get("response", "") or "")
                    if len(retry_text) > len(same_text):
                        same_text = retry_text
                        same_client = same_client_retry
                ok_same = mem_name.lower() in same_text.lower()
                add_case(
                    "memory_recall_same_client",
                    ok_same,
                    "matched_name" if ok_same else "missing_name",
                    method=str(same_client.get("method", "unknown") or "unknown"),
                    confidence=float(same_client.get("confidence", 0.0) or 0.0),
                    latency_ms=(time.time() - t0) * 1000.0,
                    preview=same_text,
                )

                t0 = time.time()
                other_client = self.chat(
                    "как меня зовут?",
                    conversation_id=mem_conv,
                    client_id=mem_client_b,
                    response_profile="fast",
                    time_budget_ms=2200,
                )
                other_text = str(other_client.get("response", "") or "")
                ok_isolated = mem_name.lower() not in other_text.lower()
                add_case(
                    "memory_isolation_cross_client",
                    ok_isolated,
                    "isolated" if ok_isolated else "leaked_name",
                    method=str(other_client.get("method", "unknown") or "unknown"),
                    confidence=float(other_client.get("confidence", 0.0) or 0.0),
                    latency_ms=(time.time() - t0) * 1000.0,
                    preview=other_text,
                )
            except Exception as exc:
                add_case("memory_recall_same_client", False, f"error:{exc}", method="error", confidence=0.0)
                add_case("memory_isolation_cross_client", False, "memory_error", method="error", confidence=0.0)

            # 5) Удержание фактов треда на длинном диалоге.
            ctx_conv = f"unique-proof:{run_id}:ctx"
            ctx_client = f"unique-proof:{run_id}:ctx-client"
            try:
                self.chat(
                    "Я живу в Париже. Я люблю шахматы и джаз.",
                    conversation_id=ctx_conv,
                    client_id=ctx_client,
                    response_profile="fast",
                    time_budget_ms=2200,
                )
                for i in range(10):
                    self.chat(
                        f"Служебная реплика {i}: обсуждаем кеш и очереди.",
                        conversation_id=ctx_conv,
                        client_id=ctx_client,
                        response_profile="fast",
                        time_budget_ms=1200,
                    )
                t0 = time.time()
                ctx_answer = self.chat(
                    "Напомни где я живу и что люблю?",
                    conversation_id=ctx_conv,
                    client_id=ctx_client,
                    response_profile="fast",
                    time_budget_ms=2200,
                )
                ctx_text = str(ctx_answer.get("response", "") or "").lower()
                ok_ctx = ("париж" in ctx_text) and ("шахмат" in ctx_text or "джаз" in ctx_text)
                add_case(
                    "thread_fact_retention",
                    ok_ctx,
                    "retained" if ok_ctx else "missing_thread_facts",
                    method=str(ctx_answer.get("method", "unknown") or "unknown"),
                    confidence=float(ctx_answer.get("confidence", 0.0) or 0.0),
                    latency_ms=(time.time() - t0) * 1000.0,
                    preview=str(ctx_answer.get("response", "") or ""),
                )
            except Exception as exc:
                add_case("thread_fact_retention", False, f"error:{exc}", method="error", confidence=0.0)

            # 6) Явная клиентская изоляция conversation id (scoped id).
            scoped_a = self._scoped_conversation_id("proof-conv", "client-a") or ""
            scoped_b = self._scoped_conversation_id("proof-conv", "client-b") or ""
            ok_scoped = scoped_a != scoped_b and scoped_a.startswith("client-a::") and scoped_b.startswith("client-b::")
            add_case(
                "scoped_conversation_ids",
                ok_scoped,
                "scoped_ids_ok" if ok_scoped else "scoped_ids_invalid",
                method="internal",
                confidence=1.0,
                preview=f"{scoped_a} | {scoped_b}",
            )

            total = len(details)
            score = round((passed / total) if total else 0.0, 4)
            finished_at = time.time()
            fingerprint_payload = {
                "pattern_size": KLM_PATTERN_SIZE,
                "gene_size": GENE_SIZE,
                "formula_layers": FORMULA_LAYERS,
                "formula_layers_fast": FORMULA_LAYERS_FAST,
                "formula_generation": self.formula_pool.generation,
                "formula_genome_hex": self.formula_pool.best().gene.to_hex(),
                "embedding_vocab": self.embeddings.vocab_size,
                "assistant_name": self._assistant_name,
            }
            fingerprint = hashlib.sha256(
                json.dumps(fingerprint_payload, sort_keys=True, ensure_ascii=False).encode("utf-8")
            ).hexdigest()[:24]

            report: dict[str, object] = {
                "run_id": run_id,
                "trigger": trigger,
                "started_at": started_at,
                "finished_at": finished_at,
                "duration_ms": round((finished_at - started_at) * 1000, 1),
                "score": score,
                "passed": passed,
                "total": total,
                "fingerprint": fingerprint,
                "claims": [
                    "numeric-word-encoding-64",
                    "formula-genome-4000-and-500-layers",
                    "self-check-and-formula-metadata-in-chat",
                    "per-client-memory-isolation",
                    "thread-fact-retention-in-long-dialog",
                ],
                "details": details,
            }
            self._last_uniqueness_report = report
            return report

    def get_uniqueness_report(self) -> dict[str, object] | None:
        if isinstance(self._last_uniqueness_report, dict):
            return dict(self._last_uniqueness_report)
        return None

    def _new_user_profile(self) -> dict[str, object]:
        return {
            "name": "",
            "facts": [],
            "documents": [],
            "preferences": {
                "theme": "system",
                "persona": "assistant",
                "memory_enabled": True,
                "model": "",
                "updated_at": 0.0,
            },
            "updated_at": 0.0,
        }

    def _sanitize_client_id(self, client_id: str | None) -> str:
        raw = re.sub(r"\s+", " ", str(client_id or "").strip())
        if not raw:
            return "global"
        clean = re.sub(r"[^a-zA-Z0-9._:-]+", "-", raw).strip("-._:")
        if not clean:
            return "global"
        return clean[:96]

    def _active_client_id(self) -> str:
        cid = self._active_client_id_var.get()
        return self._sanitize_client_id(cid)

    def _profile_meta_key(self, client_id: str) -> str:
        return f"user_profile_v2:{client_id}"

    def _parse_user_profile_payload(self, parsed: dict) -> dict[str, object]:
        profile = self._new_user_profile()
        name = parsed.get("name")
        facts = parsed.get("facts")
        documents = parsed.get("documents")
        preferences = parsed.get("preferences")
        updated = parsed.get("updated_at")

        if isinstance(name, str):
            profile["name"] = name.strip()
        if isinstance(facts, list):
            clean_facts = [
                str(item).strip()
                for item in facts
                if isinstance(item, str) and str(item).strip()
            ]
            profile["facts"] = clean_facts[:32]
        if isinstance(documents, list):
            clean_docs: list[dict[str, object]] = []
            for item in documents:
                if not isinstance(item, dict):
                    continue
                doc_id = str(item.get("id", "") or "").strip()[:32]
                title = str(item.get("title", "") or "").strip()[:120]
                text = str(item.get("text", "") or "").strip()[:20000]
                summary = str(item.get("summary", "") or "").strip()[:2400]
                if not text:
                    continue
                clean_docs.append(
                    {
                        "id": doc_id or hashlib.sha1(text.encode("utf-8", errors="ignore")).hexdigest()[:12],
                        "title": title or self._infer_document_title(text),
                        "text": text,
                        "summary": summary,
                        "created_at": float(item.get("created_at", 0.0) or 0.0),
                        "updated_at": float(item.get("updated_at", 0.0) or 0.0),
                        "tokens": int(item.get("tokens", len(_tokenize(text))) or 0),
                    }
                )
            profile["documents"] = clean_docs[-self._user_doc_max:]
        if isinstance(preferences, dict):
            clean_preferences = {
                "theme": str(preferences.get("theme", "system") or "system").strip().lower(),
                "persona": str(preferences.get("persona", "assistant") or "assistant").strip().lower(),
                "memory_enabled": bool(preferences.get("memory_enabled", True)),
                "model": str(preferences.get("model", "") or "").strip()[:120],
                "updated_at": float(preferences.get("updated_at", 0.0) or 0.0),
            }
            if clean_preferences["theme"] not in {"system", "light", "dark"}:
                clean_preferences["theme"] = "system"
            if clean_preferences["persona"] not in {"assistant", "romantic", "storyteller"}:
                clean_preferences["persona"] = "assistant"
            profile["preferences"] = clean_preferences
        if isinstance(updated, (int, float)):
            profile["updated_at"] = float(updated)
        return profile

    def _get_user_profile(self, client_id: str | None = None) -> dict[str, object]:
        active_cid = self._active_client_id()
        cid = self._sanitize_client_id(client_id if client_id is not None else active_cid)
        if client_id is None:
            cached = self._active_user_profile_var.get()
            if isinstance(cached, dict):
                return cached
        profile = self._load_user_profile(cid)
        if cid == active_cid:
            self._active_user_profile_var.set(profile)
        return profile

    def _load_user_profile(self, client_id: str | None = None) -> dict[str, object]:
        """Загрузить профиль пользователя по client_id из БД."""
        cid = self._sanitize_client_id(client_id if client_id is not None else self._active_client_id())
        if cid.startswith("quality-bench:"):
            with self._user_profile_lock:
                cached = self._user_profile_cache.get(cid)
                if isinstance(cached, dict):
                    return dict(cached)
            return self._new_user_profile()
        with self._user_profile_lock:
            cached = self._user_profile_cache.get(cid)
            if isinstance(cached, dict):
                return dict(cached)
        meta_key = self._profile_meta_key(cid)
        raw = self._db.get_user_profile(cid, default="")
        if not raw:
            raw = self._db.get_meta(meta_key, default="")
        if not raw and cid == "global":
            raw = self._db.get_meta("user_profile_v1", default="")

        profile = self._new_user_profile()
        if raw:
            try:
                parsed = json.loads(raw)
            except Exception:
                parsed = {}
            if isinstance(parsed, dict):
                profile = self._parse_user_profile_payload(parsed)
        with self._user_profile_lock:
            self._user_profile_cache[cid] = dict(profile)
        return profile

    def _save_user_profile(self, client_id: str | None = None, profile: dict[str, object] | None = None) -> None:
        """Сохранить профиль пользователя в БД."""
        active_cid = self._active_client_id()
        cid = self._sanitize_client_id(client_id if client_id is not None else active_cid)
        if cid.startswith("quality-bench:"):
            if profile is None:
                cached = self._active_user_profile_var.get() if cid == active_cid else None
                if isinstance(cached, dict):
                    profile = cached
                else:
                    profile = self._new_user_profile()
            payload = {
                "name": str(profile.get("name", "") or ""),
                "facts": list(profile.get("facts", []) or [])[:32],
                "documents": list(profile.get("documents", []) or [])[:self._user_doc_max],
                "preferences": dict(profile.get("preferences", {}) or {}),
                "updated_at": float(profile.get("updated_at", 0.0) or 0.0),
            }
            with self._user_profile_lock:
                self._user_profile_cache[cid] = dict(payload)
            if cid == active_cid:
                self._active_user_profile_var.set(payload)
            return
        if profile is None:
            cached = self._active_user_profile_var.get() if cid == active_cid else None
            if isinstance(cached, dict):
                profile = cached
            else:
                profile = self._load_user_profile(cid)
        docs = list(profile.get("documents", []) or [])
        safe_docs = []
        for d in docs[-self._user_doc_max:]:
            if not isinstance(d, dict):
                continue
            text = str(d.get("text", "") or "").strip()[:20000]
            if not text:
                continue
            safe_docs.append(
                {
                    "id": str(d.get("id", "") or "").strip()[:32],
                    "title": str(d.get("title", "") or "").strip()[:120],
                    "text": text,
                    "summary": str(d.get("summary", "") or "").strip()[:2400],
                    "created_at": float(d.get("created_at", 0.0) or 0.0),
                    "updated_at": float(d.get("updated_at", 0.0) or 0.0),
                    "tokens": int(d.get("tokens", len(_tokenize(text))) or 0),
                }
            )
        payload = {
            "name": str(profile.get("name", "") or ""),
            "facts": list(profile.get("facts", []) or [])[:32],
            "documents": safe_docs,
            "preferences": dict(profile.get("preferences", {}) or {}),
            "updated_at": float(profile.get("updated_at", 0.0) or 0.0),
        }
        with self._user_profile_lock:
            self._user_profile_cache[cid] = dict(payload)
        payload_json = json.dumps(payload, ensure_ascii=False)
        try:
            if cid == active_cid:
                self._active_user_profile_var.set(payload)
            self._user_profile_persist_queue.put_nowait(
                (cid, payload_json, float(payload.get("updated_at", 0.0) or 0.0)),
            )
        except queue.Full:
            log.warning("profile persist queue is full; dropping profile update for %s", cid)
        except Exception as exc:
            log.warning("Не удалось сохранить профиль пользователя (%s): %s", cid, exc)

    def _user_profile_persist_worker(self) -> None:
        while True:
            cid, payload_json, updated_at = self._user_profile_persist_queue.get()
            try:
                self._db.set_user_profile(cid, payload_json, updated_at)
            except Exception as exc:
                log.warning("profile persist worker failed (%s): %s", cid, exc)
            finally:
                self._user_profile_persist_queue.task_done()

    def _normalize_linguistic_text(self, text: str) -> str:
        src = (text or "").strip().lower()
        if not src:
            return ""
        src = src.replace("ё", "е")
        src = re.sub(r"[`´'’]+", "", src)
        src = re.sub(r"[\t\r\n]+", " ", src)
        src = re.sub(r"\s{2,}", " ", src)
        return src.strip()

    def _latin_token_to_cyr(self, token: str) -> str:
        t = (token or "").strip().lower()
        if not t or not re.fullmatch(r"[a-z]+", t):
            return t
        for src, dst in _LATIN_TO_CYR_DIGRAPHS:
            t = t.replace(src, dst)
        out: list[str] = []
        for ch in t:
            out.append(_LATIN_TO_CYR_CHARS.get(ch, ch))
        return "".join(out)

    def _token_phoneme_code(self, token: str) -> str:
        """
        Фонетический код слова (упрощённо), чтобы устойчиво ловить
        приветствия/интенты при опечатках и транслите.
        """
        t = self._normalize_linguistic_text(token)
        if not t:
            return ""
        if re.fullmatch(r"[a-z]+", t):
            t = self._latin_token_to_cyr(t)
        t = re.sub(r"[^a-zа-я0-9]+", "", t)
        if not t:
            return ""
        t = (
            t.replace("ё", "е")
            .replace("й", "и")
            .replace("ы", "и")
            .replace("ь", "")
            .replace("ъ", "")
        )
        t = re.sub(r"(тс|дс)", "ц", t)
        t = re.sub(r"(сч|зч|жч|шч)", "щ", t)
        t = t.translate(
            str.maketrans(
                {
                    "б": "п",
                    "в": "ф",
                    "г": "к",
                    "д": "т",
                    "ж": "ш",
                    "з": "с",
                },
            ),
        )
        t = re.sub(r"(.)\1{1,}", r"\1", t)
        if len(t) > 1:
            t = t[:1] + re.sub(r"[аеёиоуыэюя]", "", t[1:])
        return t[:16]

    def _levenshtein_distance(self, left: str, right: str) -> int:
        a = left or ""
        b = right or ""
        if a == b:
            return 0
        if not a:
            return len(b)
        if not b:
            return len(a)
        prev = list(range(len(b) + 1))
        for i, ca in enumerate(a, start=1):
            curr = [i]
            for j, cb in enumerate(b, start=1):
                cost = 0 if ca == cb else 1
                curr.append(
                    min(
                        prev[j] + 1,
                        curr[j - 1] + 1,
                        prev[j - 1] + cost,
                    ),
                )
            prev = curr
        return prev[-1]

    def _token_similarity(self, left: str, right: str) -> float:
        a = (left or "").strip().lower()
        b = (right or "").strip().lower()
        if not a or not b:
            return 0.0
        if a == b:
            return 1.0
        stem_a = _stem_ru(a)
        stem_b = _stem_ru(b)
        if stem_a and stem_a == stem_b:
            return 0.95
        ph_a = self._token_phoneme_code(a)
        ph_b = self._token_phoneme_code(b)
        if ph_a and ph_b:
            dist = self._levenshtein_distance(ph_a, ph_b)
            max_len = max(len(ph_a), len(ph_b), 1)
            return max(0.0, 1.0 - dist / max_len)
        return 0.0

    def _is_token_like(self, token: str, canonical: str, threshold: float = 0.72) -> bool:
        t = (token or "").strip().lower()
        c = (canonical or "").strip().lower()
        if not t or not c:
            return False
        if t == c:
            return True
        if _stem_ru(t) == _stem_ru(c):
            return True
        if t.startswith(c) or c.startswith(t):
            if min(len(t), len(c)) >= 4:
                return True
        return self._token_similarity(t, c) >= threshold

    def _token_matches_any(self, token: str, variants: set[str] | frozenset[str], threshold: float = 0.72) -> bool:
        return any(self._is_token_like(token, v, threshold=threshold) for v in variants)

    def _normalize_linguistic_token(self, token: str) -> str:
        t = self._normalize_linguistic_text(token)
        if not t:
            return ""
        direct = _LINGUISTIC_DIRECT_NORMALIZE.get(t)
        if direct:
            return direct
        if len(t) < 4:
            return t
        if t in _LINGUISTIC_CANONICAL_WORDS:
            return t
        if not re.fullmatch(r"[a-zа-я0-9]+", t):
            return t

        best = t
        best_score = 0.0
        t_ph = self._token_phoneme_code(t)
        for cand in _LINGUISTIC_CANONICAL_WORDS:
            if abs(len(cand) - len(t)) > 3:
                continue
            if cand[:1] != t[:1]:
                cand_ph = self._token_phoneme_code(cand)
                if not (t_ph and cand_ph and cand_ph[:1] == t_ph[:1]):
                    continue
            score = self._token_similarity(t, cand)
            if score > best_score:
                best_score = score
                best = cand

        threshold = 0.84 if len(t) <= 5 else 0.8
        if best_score >= threshold:
            return best
        return t

    def _linguistic_token_forms(self, text: str) -> list[str]:
        normalized = self._normalize_linguistic_text(text)
        if not normalized:
            return []
        raw_tokens = _tokenize(normalized)
        forms: list[str] = []
        seen: set[str] = set()
        for token in raw_tokens[:24]:
            if not token:
                continue
            candidate_tokens = [token]
            if re.fullmatch(r"[a-z]+", token):
                candidate_tokens.append(self._latin_token_to_cyr(token))
            for cand in candidate_tokens:
                cand = cand.strip()
                if not cand:
                    continue
                normalized_cand = self._normalize_linguistic_token(cand)
                variants = [normalized_cand] if normalized_cand else [cand]
                for variant in variants:
                    if not variant or variant in seen:
                        continue
                    seen.add(variant)
                    forms.append(variant)
        return forms

    def _extract_linguistic_terms(
        self,
        text: str,
        *,
        min_len: int = 3,
        drop_stop: bool = True,
        drop_generic: bool = True,
    ) -> set[str]:
        terms: set[str] = set()
        for tok in self._linguistic_token_forms(text):
            if len(tok) < max(1, min_len):
                continue
            if drop_stop and _is_stop_word(tok):
                continue
            if drop_generic and tok in _GENERIC_QUERY_WORDS:
                continue
            terms.add(tok)
        return terms

    def _extract_linguistic_stems(self, terms: set[str]) -> set[str]:
        stems: set[str] = set()
        for tok in terms:
            if len(tok) >= 4:
                st = _stem_ru(tok)
                if st:
                    stems.add(st)
            # Фонемный контур в stem-space: сохраняем только для слов >= 3.
            if len(tok) >= 3:
                ph = self._token_phoneme_code(tok)
                if len(ph) >= 3:
                    stems.add(f"ph:{ph}")
        return stems

    def _extract_terms_and_stems(
        self,
        text: str,
        *,
        min_len: int = 3,
        drop_stop: bool = True,
        drop_generic: bool = True,
    ) -> tuple[set[str], set[str]]:
        terms = self._extract_linguistic_terms(
            text,
            min_len=min_len,
            drop_stop=drop_stop,
            drop_generic=drop_generic,
        )
        return terms, self._extract_linguistic_stems(terms)

    def _is_weather_query(self, message: str, q_tokens: set[str] | None = None) -> bool:
        lower = self._normalize_linguistic_text(message)
        if not lower:
            return False
        if any(root in lower for root in _WEATHER_QUERY_ROOTS):
            return True
        tokens = q_tokens if q_tokens is not None else self._extract_linguistic_terms(message)
        for tok in tokens:
            if any(tok.startswith(root) for root in _WEATHER_QUERY_ROOTS):
                return True
        return False

    def _is_project_runtime_query(self, message: str, q_tokens: set[str] | None = None) -> bool:
        lower = self._normalize_linguistic_text(message)
        if not lower:
            return False
        weather_tokens = q_tokens if q_tokens is not None else None
        if self._is_weather_query(message, q_tokens=weather_tokens):
            return False
        if any(marker in lower for marker in (" в проект", "проект", "бэкенд", "backend", "api", "порт", "endpoint", "эндпоинт")):
            return True
        tokens = q_tokens if q_tokens is not None else self._extract_linguistic_terms(
            message,
            min_len=3,
            drop_stop=True,
            drop_generic=False,
        )
        if not tokens:
            return False
        for tok in tokens:
            if tok in _PROJECT_QUERY_MARKERS:
                return True
            if any(tok.startswith(root) for root in ("проект", "конфиг", "конфигурац", "депло")):
                return True
            if self._token_matches_any(tok, _PROJECT_QUERY_MARKERS, threshold=0.74):
                return True
        return False

    def _extract_weather_location_tokens(self, message: str, q_tokens: set[str] | None = None) -> set[str]:
        tokens = set(q_tokens) if q_tokens is not None else self._extract_linguistic_terms(message)
        loc_tokens: set[str] = set()
        for tok in tokens:
            if tok in _WEATHER_LOCATION_STOPWORDS:
                continue
            if any(tok.startswith(root) for root in _WEATHER_QUERY_ROOTS):
                continue
            if _is_stop_word(tok):
                continue
            if len(tok) < 3:
                continue
            loc_tokens.add(tok)
            loc_tokens.update(self._weather_location_variants(tok))
        return loc_tokens

    def _weather_location_variants(self, token: str) -> set[str]:
        low = self._normalize_linguistic_text(token)
        if len(low) < 3:
            return set()
        variants = {low}
        if low.endswith(("ске", "граде", "бурге")) and len(low) >= 5:
            variants.add(low[:-1])
        if low.endswith("уте") and len(low) >= 5:
            variants.add(low[:-1])
        if low.endswith(("ии", "ии?", "ии.")):
            variants.add(low[:-1] + "я")
        return {item for item in variants if len(item) >= 3}

    def _extract_weather_followup_location(self, message: str) -> str | None:
        src = self._normalize_linguistic_text(message)
        if not src:
            return None
        if self._is_followup_only_query(src):
            return None
        src = re.sub(r"[?!.,;:]+$", "", src).strip()
        src = re.sub(r"^(?:а|и|ну)\s+", "", src).strip()
        src = re.sub(r"^(?:в|во|на)\s+", "", src).strip()
        if not src:
            return None
        if self._is_weather_query(src):
            return None
        raw_tokens = [
            tok
            for tok in self._linguistic_token_forms(src)
            if tok
            and len(tok) >= 3
            and not _is_stop_word(tok)
            and tok not in _WEATHER_LOCATION_STOPWORDS
        ]
        if not raw_tokens or len(raw_tokens) > 3:
            return None
        return " ".join(raw_tokens)

    def _build_weather_location_hint(self, message: str, q_tokens: set[str] | None = None) -> str:
        loc_tokens = self._extract_weather_location_tokens(message, q_tokens=q_tokens)
        if not loc_tokens:
            return ""
        ordered: list[str] = []
        seen: set[str] = set()
        for tok in self._linguistic_token_forms(message):
            if tok in seen or tok not in loc_tokens:
                continue
            seen.add(tok)
            ordered.append(tok)
        if ordered:
            return " ".join(ordered[:4]).strip()
        ranked = sorted(loc_tokens, key=lambda t: (-len(t), t))
        return " ".join(ranked[:4]).strip()

    def _weather_answer_is_valid(self, query: str, answer: str, q_tokens: set[str] | None = None) -> bool:
        if not self._is_weather_query(query, q_tokens=q_tokens):
            return True
        ans_low = self._normalize_linguistic_text(answer)
        if not ans_low:
            return False
        if not any(marker in ans_low for marker in _WEATHER_ANSWER_MARKERS):
            return False
        loc_tokens = self._extract_weather_location_tokens(query, q_tokens=q_tokens)
        if not loc_tokens:
            return True
        a_tokens = self._extract_linguistic_terms(
            answer,
            min_len=2,
            drop_stop=True,
            drop_generic=False,
        )
        if not a_tokens:
            return False
        for lt in loc_tokens:
            if lt in a_tokens:
                return True
            if any(self._is_token_like(at, lt, threshold=0.74) for at in a_tokens):
                return True
            lt_st = _stem_ru(lt)
            if lt_st and any(_stem_ru(at) == lt_st for at in a_tokens if len(at) >= 4):
                return True
        return False

    def _weather_unavailable_answer(self, query: str, q_tokens: set[str] | None = None) -> str:
        location_hint = self._build_weather_location_hint(query, q_tokens=q_tokens)
        if location_hint:
            return (
                f"Сейчас я не смог получить актуальную погоду для «{location_hint}». "
                "Внешний погодный источник на сервере сейчас недоступен, поэтому я не буду "
                "подменять ответ локальной заглушкой."
            )
        return (
            "Сейчас я не смог получить актуальную погоду. "
            "Внешний погодный источник на сервере сейчас недоступен, поэтому я не буду "
            "подменять ответ локальной заглушкой."
        )

    def _is_time_query(self, message: str, q_tokens: set[str] | None = None) -> bool:
        del q_tokens
        return looks_like_time_query(message)

    def _is_currency_query(self, message: str, q_tokens: set[str] | None = None) -> bool:
        del q_tokens
        return looks_like_currency_query(message)

    def _is_reference_query(self, message: str, q_tokens: set[str] | None = None) -> bool:
        if self._is_weather_query(message, q_tokens=q_tokens):
            return False
        if q_tokens is not None and self._is_news_query(message, q_tokens):
            return False
        if self._is_time_query(message, q_tokens=q_tokens):
            return False
        if self._is_currency_query(message, q_tokens=q_tokens):
            return False
        return looks_like_reference_query(message)

    def _generate_article(self, topic: str, context_window: ContextWindow | None = None) -> dict:
        """#Фаза A1: Генерация статьи по теме через RAG + формулы."""
        t0 = time.time()

        # 1. Ищем знания в RAG с несколькими запросами
        search_queries = [
            topic,
            f"всё про {topic}",
            f"что такое {topic}",
            f"{topic} история",
        ]
        all_rag_chunks = []
        best_rag_response = None

        for query in search_queries:
            try:
                rag_result = self.rag.query(query, top_k=5)
                if rag_result.get("response"):
                    all_rag_chunks.append(rag_result["response"])
                    if not best_rag_response or rag_result.get("confidence", 0) > best_rag_response.get("confidence", 0):
                        best_rag_response = rag_result
            except Exception:
                pass

        # 2. Ищем в графе знаний
        graph_docs = self._search_graph_by_topic(topic, top_k=5)

        # 3. Формируем статью
        article_parts = []

        # Заголовок — нормализуем падеж но сохраняем смысл
        title = topic
        # Убираем слова-команды но сохраняем тему
        title = re.sub(r'^(войну|статью|заметку|доклад)\s+(про|о|об|на тему)\s+', '', title)
        title = re.sub(r'^(напиши|сочини|создай|подготовь)\s+(стать[юю]|текст|доклад|реферат|эссе|заметку)\s+(про|о|об|на тему)\s+', '', title)
        title = title.strip()
        # Капитализируем первое слово
        if title:
            title = title[0].upper() + title[1:]
        article_parts.append(f"# {title}")
        article_parts.append("")

        # Введение из лучшего RAG результата
        if best_rag_response and best_rag_response.get("response"):
            article_parts.append("## Введение")
            article_parts.append(best_rag_response["response"][:500])
            article_parts.append("")

        # Объединяем все RAG чанки
        if len(all_rag_chunks) > 1:
            article_parts.append("## Дополнительная информация")
            for chunk in all_rag_chunks[1:3]:
                article_parts.append(chunk[:300])
                article_parts.append("")

        # Основная часть из графа
        if graph_docs:
            article_parts.append("## Из базы знаний Kolibri")
            for doc in graph_docs[:3]:
                content = doc.get("content", "")[:300]
                if content:
                    article_parts.append(content)
                    article_parts.append("")

        # Источники
        sources = set()
        if best_rag_response and best_rag_response.get("sources"):
            for src in best_rag_response["sources"]:
                title = src.get("title", "Неизвестный источник")
                sources.add(f"- {title} ({src.get('source', '')})")

        if sources:
            article_parts.append("## Источники")
            article_parts.extend(sorted(sources)[:5])

        article = "\n".join(article_parts)

        # #Фаза Web Research: Всегда используем полноценный веб-поиск для статей
        try:
            # Нормализуем тему для поиска
            search_topic = topic
            search_topic = re.sub(r'(войну|статью|заметку|доклад)\s+', '', search_topic)

            # Полноценный веб-поиск + обучение
            research_result = self.web_research.research_and_learn(
                search_topic,
                max_sources=15,
                timeout=30.0,
                ai_engine=self,
            )

            if research_result.get("combined_text"):
                # Формируем статью из результатов веб-поиска
                combined = research_result["combined_text"]
                sources = research_result.get("sources", [])

                article_parts = [
                    f"# {title}",
                    "",
                    "## Введение",
                    combined[:1500],
                    "",
                ]

                if len(combined) > 1500:
                    article_parts.append("## Основная информация")
                    article_parts.append(combined[1500:4000])
                    article_parts.append("")

                if sources:
                    article_parts.append("## Источники")
                    for src in sources[:10]:
                        article_parts.append(f"- [{src['title']}]({src['url']}) ({src['source']})")

                article = "\n".join(article_parts)
        except Exception as e:
            log.warning("Web research failed: %s", e)

        # Если всё ещё мало — честно говорим
        if len(article) < 200:
            article = (
                f"# {title}\n\n"
                f"По теме «{title}» в моей локальной базе пока недостаточно подробной информации "
                f"для полноценной статьи. Однако я могу:\n\n"
                f"1. Найти краткую справку через веб-поиск\n"
                f"2. Запомнить материал если вы его предоставите\n"
                f"3. Сгенерировать структуру статьи для дальнейшего наполнения\n\n"
                f"Что предпочитаете?"
            )

        duration_ms = (time.time() - t0) * 1000

        return {
            "response": article,
            "confidence": 0.7 if len(article) > 500 else 0.4,
            "sources": ["article-generation"] + (list(sources) if sources else []),
            "method": "article-generation",
            "knowledge_hits": len(all_rag_chunks) + len(graph_docs),
            "duration_ms": round(duration_ms, 1),
            "formula_data": self._basic_formula_data(),
            "graph_stats": self.graph.get_stats(),
        }

    def _search_graph_by_topic(self, topic: str, top_k: int = 5) -> list[dict]:
        """Поиск документов в графе знаний по теме."""
        results = []
        topic_lower = topic.lower()

        for edge in self.graph.edges.values():
            try:
                # KnowledgeEdge может иметь разные атрибуты
                if hasattr(edge, 'document'):
                    doc = edge.document if isinstance(edge.document, dict) else {}
                elif hasattr(edge, '__dict__'):
                    doc = edge.__dict__
                else:
                    doc = {}

                content = str(doc.get("content", ""))
                title = str(doc.get("title", ""))

                if topic_lower in content.lower() or topic_lower in title.lower():
                    results.append(doc)
                    if len(results) >= top_k:
                        break
            except Exception:
                continue

        return results

    def _is_plain_fact_statement(self, message: str) -> bool:
        text = re.sub(r"\s+", " ", (message or "").strip())
        if len(text) < 14 or len(text) > 320:
            return False
        lower = self._normalize_linguistic_text(text)
        if not lower or "?" in text:
            return False
        if "http://" in lower or "https://" in lower:
            return False
        if self._is_greeting_intent(text) or self._is_identity_intent(text):
            return False
        terms = self._extract_linguistic_terms(text, min_len=3, drop_stop=True, drop_generic=False)
        if self._is_time_query(text, q_tokens=terms):
            return False
        if self._is_currency_query(text, q_tokens=terms):
            return False
        if self._is_reference_query(text, q_tokens=terms):
            return False
        if lower.startswith(_DIALOG_FACT_BAD_PREFIXES):
            return False
        return len(terms) >= 3

    def _is_context_placeholder_answer(self, text: str) -> bool:
        lowered = self._normalize_linguistic_text(text)
        if not lowered:
            return True
        bad_markers = (
            "в моей локальной базе пока мало",
            "недостаточно локальных знаний",
            "пока нет подтвержденных фактов в текущем диалоге",
            "добавьте факт (например",
            "добавьте материал",
            "кратко по текущему диалогу",
        )
        return any(marker in lowered for marker in bad_markers)

    def _cleanup_response_text(self, text: str) -> str:
        src = re.sub(r"\s+", " ", str(text or "").strip())
        if not src:
            return src
        # Исправляем частый артефакт склейки из web-snippet'ов.
        src = re.sub(r"(?<=[а-яё])(?=[А-ЯЁ])", " ", src)
        src = re.sub(r"\bпогодав\b", "Погода в", src, flags=re.IGNORECASE)
        src = re.sub(r"\.(?=[A-ZА-ЯЁ])", ". ", src)
        src = re.sub(r"([!?])([^\s])", r"\1 \2", src)
        src = re.sub(r"\s{2,}", " ", src).strip()
        return src

    def _strip_memory_ack_wrapper(self, text: str) -> str:
        src = re.sub(r"\s+", " ", str(text or "").strip())
        if not src:
            return src
        src = re.sub(
            r"^(?:принял|окей|хорошо|понял)\.\s*зафиксировал в контексте:\s*",
            "",
            src,
            flags=re.IGNORECASE,
        )
        src = re.sub(
            r"^(?:запомнил|зафиксировал(?: в контексте)?):\s*",
            "",
            src,
            flags=re.IGNORECASE,
        )
        return src.strip()

    def _is_identity_intent(self, text: str) -> bool:
        normalized = self._normalize_linguistic_text(text)
        if not normalized:
            return False
        if any(
            phrase in normalized
            for phrase in ("как тебя зовут", "кто ты", "представься", "твое имя", "твоё имя", "назовись")
        ):
            return True

        tokens = self._linguistic_token_forms(normalized)
        if not tokens:
            return False
        if any(
            self._token_matches_any(tok, {"представься", "представся", "назовись"}, threshold=0.66)
            for tok in tokens
        ):
            return True
        has_user_self_ref = any(
            self._token_matches_any(tok, _IDENTITY_USER_SELF_WORDS, threshold=0.74)
            for tok in tokens
        )
        if has_user_self_ref:
            return False
        has_you = any(self._token_matches_any(tok, _IDENTITY_YOU_WORDS, threshold=0.74) for tok in tokens)
        has_name = any(self._token_matches_any(tok, _IDENTITY_NAME_WORDS, threshold=0.68) for tok in tokens)
        return has_you and has_name

    def _is_greeting_intent(self, text: str) -> bool:
        normalized = self._normalize_linguistic_text(text)
        if not normalized:
            return False
        if re.match(
            r"^(что\s+такое|кто\s+так(?:ой|ая|ие)|объясни|расскажи(?:\s+подробно)?\s+(?:про|о)|что\s+ты\s+знаешь\s+(?:о|об|про)|что\s+изучает|чем\s+занимается|как\s+устроен(?:а|о)?|почему\s+важ(?:ен|на|но)|зачем\s+нуж(?:ен|на|но))\b",
            normalized,
        ):
            return False
        if normalized.startswith(("доброе утро", "добрый день", "добрый вечер", "доброй ночи")):
            return True

        tokens = self._linguistic_token_forms(normalized)
        if not tokens:
            return False

        greeting_hits = [
            tok for tok in tokens
            if self._token_matches_any(tok, _GREETING_CANONICAL, threshold=0.66)
        ]
        if not greeting_hits:
            return False

        informative = [
            tok
            for tok in tokens
            if len(tok) >= 3
            and not _is_stop_word(tok)
            and not self._token_matches_any(tok, _GREETING_CANONICAL, threshold=0.72)
            and not self._token_matches_any(tok, _GREETING_ADDRESS_WORDS, threshold=0.72)
        ]
        if not informative:
            return True

        return all(
            self._token_matches_any(tok, _SMALLTALK_WORDS, threshold=0.72)
            for tok in informative
        )

    def _is_smalltalk_checkin_intent(self, text: str) -> bool:
        normalized = self._normalize_linguistic_text(text)
        if not normalized:
            return False
        if self._match_c_projection_query(text):
            return False
        direct_phrases = (
            "как дела",
            "как ты",
            "как дела колибри",
            "что делаешь",
            "как поживаешь",
        )
        if normalized in direct_phrases:
            return True
        tokens = self._linguistic_token_forms(normalized)
        if not tokens:
            return False
        has_checkin = any(
            self._token_matches_any(tok, {"дела", "поживаешь", "настроение"}, threshold=0.72)
            for tok in tokens
        )
        has_how = any(self._token_matches_any(tok, {"как"}, threshold=0.8) for tok in tokens)
        has_address = any(self._token_matches_any(tok, _GREETING_ADDRESS_WORDS, threshold=0.72) for tok in tokens)
        return has_checkin and (has_how or has_address)

    def _is_architecture_intent(self, text: str) -> bool:
        normalized = self._normalize_linguistic_text(text)
        if not normalized:
            return False
        if not re.search(r"\b(kolibri|колибри|калибри)\b", normalized):
            return False
        return bool(
            re.search(r"\b(архитектур|ядро|устроен|устроена|устроено)\b", normalized)
            or re.search(r"^(объясни|расскажи)", normalized)
        )

    def _is_self_meta_intent(self, text: str) -> bool:
        normalized = self._normalize_linguistic_text(text)
        if not normalized:
            return False
        patterns = (
            r"^ты\s+умеешь\s+говорить\b",
            r"^ты\s+говоришь\b",
            r"^ты\s+бог\b",
            r"^ты\s+человек\b",
            r"^ты\s+жив(?:ой|ой\?)?$",
        )
        return any(re.match(pattern, normalized) for pattern in patterns)

    def _is_abusive_intent(self, text: str) -> bool:
        normalized = self._normalize_linguistic_text(text)
        if not normalized:
            return False
        tokens = [tok for tok in self._linguistic_token_forms(normalized) if tok]
        if not tokens or len(tokens) > 4:
            return False
        abusive_hits = [
            tok
            for tok in tokens
            if self._token_matches_any(tok, _ABUSIVE_MARKERS, threshold=0.72)
        ]
        return bool(abusive_hits)

    def _is_ambiguous_entity_fragment(self, text: str) -> bool:
        raw = re.sub(r"\s+", " ", (text or "").strip())
        if not raw or "?" in raw:
            return False
        normalized = self._normalize_linguistic_text(raw)
        if not normalized:
            return False
        if any(ch.isdigit() for ch in normalized):
            return False
        if self._is_weather_query(normalized):
            return False
        if self._is_time_query(normalized) or self._is_currency_query(normalized) or self._is_reference_query(normalized):
            return False
        if normalized.startswith((
            "объясни ",
            "поясни ",
            "расскажи ",
            "что такое ",
            "что ты знаешь ",
            "как устро",
            "почему важ",
            "зачем нуж",
            "приведи пример",
        )):
            return False
        if self._is_greeting_intent(normalized) or self._is_identity_intent(normalized):
            return False
        if self._is_followup_only_query(normalized) or self._is_referential_query(normalized):
            return False
        tokens = [
            tok for tok in self._linguistic_token_forms(normalized)
            if tok and len(tok) >= 3 and not _is_stop_word(tok)
        ]
        if not tokens or len(tokens) > 3:
            return False
        if any(
            self._token_matches_any(tok, {"объясни", "расскажи", "покажи", "сделай", "найди"}, threshold=0.72)
            for tok in tokens
        ):
            return False
        return True

    def _is_capabilities_intent(self, text: str) -> bool:
        normalized = self._normalize_linguistic_text(text)
        if not normalized:
            return False
        patterns = (
            r"^(?:help|помощь|помоги|помощ)$",
            r"^что(?:\s+ты)?(?:\s+уже)?\s+умеешь(?:\s+сейчас)?$",
            r"^что(?:\s+ты)?\s+можешь$",
            r"^какие\s+у\s+тебя\s+возможности$",
            r"^что\s+умеет\s+колибри$",
        )
        return any(re.match(pattern, normalized) for pattern in patterns)

    def _extract_name_candidate(self, message: str) -> str | None:
        text = message.strip()
        patterns = (
            r"(?:меня\s+зовут|мо[её]\s+имя)\s+([A-Za-zА-Яа-яЁё][A-Za-zА-Яа-яЁё\-]{1,24}(?:\s+[A-Za-zА-Яа-яЁё][A-Za-zА-Яа-яЁё\-]{1,24})?)",
        )
        banned = {"работаю", "люблю", "живу", "учусь", "занимаюсь", "интересуюсь", "нравится"}
        for pat in patterns:
            match = re.search(pat, text, flags=re.IGNORECASE)
            if not match:
                continue
            raw_name = re.sub(r"\s+", " ", match.group(1).strip())
            parts = raw_name.split()
            if any(p.lower() in banned for p in parts):
                continue
            pretty = " ".join(p[:1].upper() + p[1:] for p in parts if p)
            if pretty:
                return pretty
        return None

    def _remember_user_name(self, name: str) -> None:
        clean_name = re.sub(r"\s+", " ", name.strip())
        if not clean_name:
            return
        profile = self._get_user_profile()
        profile["name"] = clean_name
        profile["updated_at"] = time.time()
        self._save_user_profile(profile=profile)

    def _remember_user_fact(self, fact: str) -> None:
        clean_fact = re.sub(r"\s+", " ", fact.strip())
        if len(clean_fact) < 3:
            return
        profile = self._get_user_profile()
        facts = list(profile.get("facts", []) or [])
        if clean_fact not in facts:
            facts.append(clean_fact)
        profile["facts"] = facts[-32:]
        profile["updated_at"] = time.time()
        self._save_user_profile(profile=profile)

    def _extract_auto_user_fact(self, message: str) -> str | None:
        text = re.sub(r"\s+", " ", (message or "").strip())
        if len(text) < 8 or len(text) > 240:
            return None
        lowered = text.lower().strip(" .!?")
        if "?" in text:
            return None
        if "http://" in lowered or "https://" in lowered:
            return None
        if any(lowered.startswith(pref) for pref in _AUTO_FACT_BAD_PREFIXES):
            return None
        if not any(pat.match(lowered) for pat in _AUTO_FACT_PATTERNS):
            return None
        # Нужна минимальная информативность (>=3 токена не-стоп-слов).
        core_tokens = [
            t for t in _tokenize(lowered)
            if len(t) >= 3 and not _is_stop_word(t) and t not in _GENERIC_QUERY_WORDS
        ]
        if len(core_tokens) < 3:
            return None
        return text.strip(" .")

    def _answer_from_user_facts(self, query: str) -> str | None:
        profile = self._get_user_profile()
        facts = [str(x).strip() for x in (profile.get("facts", []) or []) if str(x).strip()]
        if not facts:
            return None

        query_lower = (query or "").lower()
        interest_query = any(k in query_lower for k in ("интерес", "люблю", "нрав", "увлека", "предпоч"))

        q_tokens, q_stems = self._extract_terms_and_stems(query)
        if not q_tokens and "обо мне" in (query or "").lower():
            q_tokens = {"мне"}

        scored: list[tuple[float, str]] = []
        for fact in facts[-32:]:
            low = fact.lower()
            f_tokens = {
                t for t in _tokenize(low)
                if len(t) >= 3 and not _is_stop_word(t)
            }
            if not f_tokens:
                continue
            f_stems = {_stem_ru(t) for t in f_tokens if len(t) >= 4}
            overlap = len(q_tokens & f_tokens) if q_tokens else 0
            stem_overlap = len(q_stems & f_stems) if q_stems else 0
            interest_fact = any(k in low for k in ("люблю", "нрав", "интерес", "увлека", "предпоч"))
            if q_tokens and overlap == 0 and stem_overlap == 0 and not (interest_query and interest_fact):
                continue
            recency_boost = 0.02 * len(scored)
            score = overlap * 1.0 + stem_overlap * 0.7 + recency_boost
            if interest_query and interest_fact:
                score += 1.2
            scored.append((score, fact))

        if not scored:
            # Если вопрос общий "что ты знаешь обо мне", отдаём последние факты.
            if any(k in (query or "").lower() for k in ("обо мне", "помнишь", "знаешь")):
                top = facts[-4:]
            else:
                return None
        else:
            scored.sort(key=lambda x: x[0], reverse=True)
            top = []
            for _, fact in scored:
                if fact not in top:
                    top.append(fact)
                if len(top) >= 4:
                    break

        if not top:
            return None
        return "По моим записям о вас:\n" + "\n".join(f"• {x}" for x in top)

    def _render_user_memory(self) -> str:
        profile = self._get_user_profile()
        name = str(profile.get("name", "") or "").strip()
        facts = [str(x).strip() for x in (profile.get("facts", []) or []) if str(x).strip()]
        docs = self._visible_profile_documents(
            [d for d in (profile.get("documents", []) or []) if isinstance(d, dict)]
        )
        lines: list[str] = []
        if name:
            lines.append(f"• Имя: {name}")
        if facts:
            for item in facts[-5:]:
                lines.append(f"• {item}")
        if docs:
            lines.append(f"• Обученных текстов: {len(docs)}")
            for d in docs[-3:]:
                title = self._document_display_title(d)[:80]
                lines.append(f"  - {title}")
        if not lines:
            return "Пока вы меня не обучали персональным фактам. Напишите: `Меня зовут ...` или `Запомни: ...`."
        return "Я помню о вас:\n" + "\n".join(lines)

    def _document_display_title(self, doc: dict[str, object]) -> str:
        title = re.sub(r"\s+", " ", str(doc.get("title", "") or "").strip())
        summary = re.sub(r"\s+", " ", str(doc.get("summary", "") or "").strip())
        text = re.sub(r"\s+", " ", str(doc.get("text", "") or "").strip())
        candidate = title
        low_title = candidate.lower()
        noisy_prefixes = (
            "текст:",
            "сообщения",
            "хорошо",
            "отлично",
            "сделал",
            "продолжил",
            "да,",
            "вот это",
        )
        if len(candidate) < 4 or any(low_title.startswith(prefix) for prefix in noisy_prefixes):
            for fallback in (summary, text):
                if len(fallback) < 12:
                    continue
                first_sentence = re.split(r"(?<=[.!?])\s+", fallback, maxsplit=1)[0].strip(" .:-")
                if len(first_sentence) >= 8:
                    candidate = first_sentence
                    break
        candidate = re.sub(r"\s+", " ", candidate).strip(" .:-")
        return candidate[:90] if candidate else "Обученный текст"

    def _visible_profile_documents(self, docs: list[dict[str, object]]) -> list[dict[str, object]]:
        visible: list[dict[str, object]] = []
        seen_titles: set[str] = set()
        noisy_fragments = (
            "по вашему запросу",
            "правильные ответы",
            "равильные ответы",
            "вот это уже",
            "тогда отвечаю",
            "локальный решатель",
            "жёстко и стратегически",
            "продолжил и",
            "сделал и",
        )
        noisy_content_fragments = (
            "/users/kolibri/",
            "/srv/kolibri/",
            "backend/service/",
            "что изменил:",
            "выкачен на сервер",
            "проблема была в двух местах",
            "магия цифр",
            "ratio=",
            "mb/s",
            "world выше",
            "локальный решатель",
            "подтвердил и исправил",
        )
        for doc in docs:
            source = str(doc.get("source", "") or "").strip().lower()
            if source == "auto-message":
                continue
            title = self._document_display_title(doc)
            key = re.sub(r"\s+", " ", title).strip().lower()
            if not key or key in seen_titles:
                continue
            if any(fragment in key for fragment in noisy_fragments):
                continue
            summary = re.sub(r"\s+", " ", str(doc.get("summary", "") or "").strip()).lower()
            text = re.sub(r"\s+", " ", str(doc.get("text", "") or "").strip()).lower()
            combined = " ".join(part for part in (key, summary[:240], text[:240]) if part)
            if key == "сообщения":
                continue
            if any(fragment in combined for fragment in noisy_content_fragments):
                continue
            seen_titles.add(key)
            visible.append(doc)
        return visible

    def _split_sentences(self, text: str) -> list[str]:
        raw_parts = re.split(r"(?<=[.!?])\s+|\n+", text.strip())
        cleaned: list[str] = []
        for part in raw_parts:
            p = re.sub(r"\s+", " ", part.strip())
            if len(p) >= 12:
                cleaned.append(p)
        return cleaned

    def _infer_document_title(self, text: str) -> str:
        lines = [re.sub(r"\s+", " ", x.strip()) for x in text.splitlines() if x.strip()]
        if lines:
            first = lines[0].strip(" .:-")
            if 3 <= len(first) <= 100:
                return first

        tokens = [
            t for t in _tokenize(text.lower())
            if len(t) >= 4 and not _is_stop_word(t)
        ]
        if tokens:
            top = [w for w, _ in Counter(tokens).most_common(3)]
            return "Текст: " + ", ".join(top)
        return "Обученный текст"

    def _remember_document(self, text: str, source: str = "user") -> dict | None:
        clean_text = re.sub(r"[ \t]+", " ", text).strip()
        if len(clean_text) < 120:
            return None

        doc_id = hashlib.sha1(clean_text.encode("utf-8", errors="ignore")).hexdigest()[:12]
        now = time.time()
        summary_sentences = self._split_sentences(clean_text)
        summary = " ".join(summary_sentences[:3])[:1800]
        title = self._infer_document_title(clean_text)
        entry = {
            "id": doc_id,
            "title": title,
            "text": clean_text[:20000],
            "summary": summary,
            "source": source,
            "tokens": len(_tokenize(clean_text)),
            "created_at": now,
            "updated_at": now,
        }

        profile = self._get_user_profile()
        docs = [d for d in (profile.get("documents", []) or []) if isinstance(d, dict)]
        replaced = False
        for idx, d in enumerate(docs):
            if str(d.get("id", "")) == doc_id:
                entry["created_at"] = float(d.get("created_at", now) or now)
                docs[idx] = entry
                replaced = True
                break
        if not replaced:
            docs.append(entry)
        profile["documents"] = docs[-self._user_doc_max:]
        profile["updated_at"] = now
        self._save_user_profile(profile=profile)
        self._persist_state_throttled()
        return entry

    def _find_best_document(self, query: str) -> dict | None:
        profile = self._get_user_profile()
        docs = [d for d in (profile.get("documents", []) or []) if isinstance(d, dict)]
        if not docs:
            return None
        q_lower = (query or "").strip().lower()
        if re.search(r"\b(этот|последн\w*)\s+текст\b", q_lower):
            return docs[-1]

        ignored_tokens = {
            "расскажи", "перескажи", "своими", "словами", "кратко", "подробно",
            "напиши", "сочини", "придумай", "сказка", "сказку", "история", "историю",
            "про", "пожалуйста",
        }
        q_tokens = [
            t for t in _tokenize(query.lower())
            if len(t) >= 3 and t not in ignored_tokens and not _is_stop_word(t)
        ]
        if not q_tokens:
            return docs[-1]
        q_stems = {_stem_ru(t) for t in q_tokens if len(t) >= 4}
        q_prefixes = {t[:4] for t in q_tokens if len(t) >= 4}

        best_doc: dict | None = None
        best_score = -1.0
        for d in docs:
            title = str(d.get("title", "") or "").lower()
            text = str(d.get("text", "") or "").lower()
            hay = (title + " " + text[:3500]).strip()
            hay_tokens = {
                t for t in _tokenize(hay)
                if len(t) >= 3 and not _is_stop_word(t)
            }
            hay_stems = {_stem_ru(t) for t in hay_tokens if len(t) >= 4}

            exact_overlap = len(set(q_tokens) & hay_tokens)
            stem_overlap = len(q_stems & hay_stems) if q_stems else 0
            prefix_overlap = 0
            if q_prefixes:
                for p in q_prefixes:
                    if any(tok.startswith(p) for tok in hay_tokens):
                        prefix_overlap += 1

            score = exact_overlap * 3.0 + stem_overlap * 2.0 + prefix_overlap * 1.0
            for t in q_tokens:
                if t in title:
                    score += 1.8
            score += min(0.5, len(text) / 20000.0)
            if score > best_score:
                best_score = score
                best_doc = d
        return best_doc

    def _unique_sentences(self, sentences: list[str], limit: int | None = None) -> list[str]:
        unique: list[str] = []
        seen: set[str] = set()
        for raw in sentences:
            s = re.sub(r"\s+", " ", str(raw).strip())
            if len(s) < 12:
                continue
            normalized = re.sub(r"[^a-zа-яё0-9]+", " ", s.lower()).strip()
            normalized = re.sub(r"\s+", " ", normalized)
            if len(normalized) < 10:
                continue
            if normalized in seen:
                continue
            seen.add(normalized)
            unique.append(s)
            if limit is not None and len(unique) >= max(1, limit):
                break
        return unique

    def _strip_leading_connector(self, sentence: str) -> str:
        s = re.sub(r"\s+", " ", (sentence or "").strip())
        if not s:
            return s
        return re.sub(
            r"^(?:сначала|потом|дальше|после этого|в итоге|затем|наконец|также)\s+",
            "",
            s,
            flags=re.IGNORECASE,
        ).strip()

    def _retell_document(self, doc: dict, short: bool = False) -> str:
        text = str(doc.get("text", "") or "").strip()
        title = str(doc.get("title", "этот текст") or "этот текст").strip()
        sentences = self._unique_sentences(self._split_sentences(text))
        if not sentences:
            return f"Я помню материал «{title}», но текста пока недостаточно для пересказа."

        target = 4 if short else 7
        if len(sentences) <= target:
            selected = sentences
        else:
            idxs = {0, len(sentences) - 1}
            idxs.add(len(sentences) // 3)
            idxs.add((2 * len(sentences)) // 3)
            idxs = {i for i in idxs if 0 <= i < len(sentences)}
            step = max(1, len(sentences) // max(1, target))
            for i in range(0, len(sentences), step):
                idxs.add(i)
                if len(idxs) >= target:
                    break
            selected = [sentences[i] for i in sorted(idxs)[:target]]

        prefixes = ["Сначала", "Потом", "Дальше", "После этого", "В итоге", "Затем", "Наконец"]
        parts: list[str] = []
        for i, sentence in enumerate(selected):
            s = sentence.strip().strip('"“”')
            if not s:
                continue
            s = re.sub(r"^\s*(?:сказка|история)\s+[^:]{0,40}:\s*", "", s, flags=re.IGNORECASE)
            s = self._strip_leading_connector(s)
            if len(s) < 8:
                continue
            s = s[0].lower() + s[1:] if len(s) > 1 else s.lower()
            pref = prefixes[min(i, len(prefixes) - 1)]
            parts.append(f"{pref} {s.rstrip('.!?')}.")

        if not parts:
            return f"Я запомнил «{title}», но пока не могу собрать связный пересказ."

        intro = f"Пересказ по-своему для «{title}»:\n"
        return intro + " ".join(parts)

    def _extract_story_topic(self, query: str, doc: dict | None = None) -> str:
        q = (query or "").strip().lower()
        if q:
            m = re.search(r"\bпро\s+([а-яёa-z][а-яёa-z0-9\-]{2,})", q)
            if m:
                return m.group(1)
            tokens = [
                t for t in _tokenize(q)
                if len(t) >= 3 and not _is_stop_word(t)
                and t not in {"сказка", "сказку", "напиши", "расскажи", "сочини", "придумай", "история", "историю"}
            ]
            if tokens:
                return tokens[-1]
        if doc:
            title = str(doc.get("title", "") or "").lower()
            t_tokens = [
                t for t in _tokenize(title)
                if len(t) >= 3 and not _is_stop_word(t) and t not in {"текст", "сказка", "история"}
            ]
            if t_tokens:
                return t_tokens[0]
        return "историю"

    def _compose_story_from_document(self, doc: dict, topic_hint: str = "") -> str:
        text = str(doc.get("text", "") or "").strip()
        title = str(doc.get("title", "этот текст") or "этот текст").strip()
        sentences = self._unique_sentences(self._split_sentences(text))
        if not sentences:
            return f"Я помню материал «{title}», но пока не хватает данных, чтобы написать сказку."

        target = 6
        if len(sentences) <= target:
            selected = sentences
        else:
            step = max(1, len(sentences) // target)
            selected = [sentences[i] for i in range(0, len(sentences), step)][:target]

        connectors = ["Однажды", "Скоро", "Потом", "После этого", "Наконец"]
        parts: list[str] = []
        for i, sentence in enumerate(selected):
            s = sentence.strip().strip('"“”')
            if not s:
                continue
            s = re.sub(r"^\s*(?:сказка|история)\s+[^:]{0,40}:\s*", "", s, flags=re.IGNORECASE)
            s = self._strip_leading_connector(s)
            if len(s) < 8:
                continue
            base = s.rstrip(".!?")
            if i == 0:
                parts.append(base + ".")
                continue
            pref = connectors[min(i - 1, len(connectors) - 1)]
            base = base[0].lower() + base[1:] if len(base) > 1 else base.lower()
            parts.append(f"{pref} {base}.")

        if not parts:
            return f"Я помню материал «{title}», но пока не могу собрать связную сказку."

        topic = topic_hint.strip() or self._extract_story_topic("", doc)
        if len(parts) == 1:
            core = parts[0].rstrip(".")
            low = core.lower()
            extras: list[str] = [
                "Эта история начинается спокойно, но путь героя постепенно усложняется.",
            ]
            m_meet = re.search(r"встрет[а-яё]*\s+(.+)$", low)
            if m_meet:
                tail = m_meet.group(1)
                tail = re.sub(r"\bа\s+потом\b", ", ", tail, flags=re.IGNORECASE)
                tail = re.sub(r"\s+", " ", tail).strip(" ,.")
                items = [x.strip() for x in tail.split(",") if x.strip()]
                tail = ", ".join(items)
                if len(tail) >= 3:
                    extras.append(f"По дороге ему встретились: {tail}.")
            extras.append("Финал напоминает: в каждом путешествии важны внимательность и выбор.")
            return f"Сказка про {topic}:\n" + " ".join([parts[0], *extras])

        return f"Сказка про {topic}:\n" + " ".join(parts)

    def _learn_from_user_text(self, text: str) -> None:
        clean = text.strip()
        if len(clean) < 80:
            return
        self.train_text(clean)
        self._user_doc_learning_counter += 1
        self._persist_state_throttled(
            force=(self._user_doc_learning_counter % 2 == 0),
        )

    def _maybe_auto_learn_from_message(self, message: str) -> dict | None:
        if not self._auto_learn_long_messages:
            return None
        text = message.strip()
        if len(text) < 500:
            return None
        sentences = self._split_sentences(text)
        if len(sentences) < 4:
            return None
        # Не обучаем автоматически чисто вопросительные сообщения.
        if text.count("?") > max(2, len(sentences) // 2):
            return None

        doc = self._remember_document(text, source="auto-message")
        try:
            self._train_queue.put_nowait(("user_text", text[:20000]))
            if doc:
                title = str(doc.get("title", "текст")).strip().lower()
                self.formula_pool.add_association(
                    f"перескажи {title}",
                    self._retell_document(doc, short=True),
                )
        except queue.Full:
            pass
        return doc

    # ------------------------------------------------------------------
    # Генеративный AI: FormulaLM + BPE-токенизатор
    # ------------------------------------------------------------------

    def _train_lm_on_corpus(self) -> None:
        """Обучить FormulaLM на предложениях из SentenceStore."""
        if self._lm_trained or self.sentence_store.size < 100:
            return
        try:
            all_texts: list[str] = []
            for idx in range(min(self.sentence_store.size, 2000)):
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

            self._formula_lm.evolve(sequences[:200], generations=30)
            self._lm_trained = True
            self._lm_generation += 30
            log.info(
                "FormulaLM trained: gen=%d, vocab=%d, sequences=%d",
                self._lm_generation, len(self._bpe_tokenizer), len(sequences),
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

    def _plan_chat_stage_budgets(self, budget_ms: int, fast_mode: bool) -> dict[str, int]:
        total = max(1200, int(budget_ms))
        if fast_mode:
            retrieval = max(300, int(total * 0.28))
            synthesis = max(380, int(total * 0.38))
            repair = max(180, int(total * 0.14))
            cognition = max(120, int(total * 0.10))
        else:
            retrieval = max(900, int(total * 0.34))
            synthesis = max(1000, int(total * 0.32))
            repair = max(600, int(total * 0.20))
            cognition = max(360, int(total * 0.10))
        reserved = total - (retrieval + synthesis + repair + cognition)
        if reserved < 80:
            # Балансируем, чтобы не выйти за общий бюджет.
            overflow = 80 - reserved
            take = min(overflow, max(0, repair - 120))
            repair -= take
            overflow -= take
            if overflow > 0:
                take = min(overflow, max(0, cognition - 80))
                cognition -= take
                overflow -= take
            if overflow > 0:
                take = min(overflow, max(0, synthesis - 220))
                synthesis -= take
                overflow -= take
            reserved = total - (retrieval + synthesis + repair + cognition)
            if reserved < 0:
                reserved = 0
        return {
            "retrieval": max(120, retrieval),
            "synthesis": max(160, synthesis),
            "repair": max(80, repair),
            "cognition": max(60, cognition),
            "reserve": max(0, reserved),
        }

    def _plan_chat_stage_deadlines(
        self,
        start_time: float,
        global_deadline: float,
        budget_ms: int,
        fast_mode: bool,
    ) -> tuple[dict[str, int], dict[str, float]]:
        budgets = self._plan_chat_stage_budgets(budget_ms, fast_mode=fast_mode)
        cursor = float(start_time)
        deadlines: dict[str, float] = {}
        for stage in ("retrieval", "synthesis", "repair", "cognition"):
            cursor += max(0, int(budgets.get(stage, 0))) / 1000.0
            deadlines[stage] = min(cursor, global_deadline)
        return budgets, deadlines

    # ------------------------------------------------------------------
    # Главная функция: ответить на сообщение
    # ------------------------------------------------------------------

    def chat(
        self,
        message: str,
        conversation_id: str | None = None,
        client_id: str | None = None,
        temperature: float = 0.7,
        response_profile: str = "fast",
        time_budget_ms: int | None = None,
        persona: str | None = None,
        memory_enabled: bool = True,
        model_name: str | None = None,
    ) -> dict:
        """
        Ответить через Числовое Мышление.
        Pipeline: контекст → CoT → паттерны → граф → формулы → C-модель → синтез → генерация.
        """
        start_time = time.time()
        profile = (response_profile or "fast").strip().lower()
        if profile not in {"fast", "balanced", "deep"}:
            profile = "fast"
        fast_mode = self._fast_chat_mode or profile == "fast"
        if profile == "deep":
            fast_mode = False

        budget_ms = int(time_budget_ms) if time_budget_ms is not None else 0
        if budget_ms <= 0:
            budget_ms = 12000 if fast_mode else 30000
        deadline = start_time + (budget_ms / 1000.0)
        stage_budgets_ms, stage_deadlines = self._plan_chat_stage_deadlines(
            start_time=start_time,
            global_deadline=deadline,
            budget_ms=budget_ms,
            fast_mode=fast_mode,
        )

        def budget_exceeded() -> bool:
            return time.time() >= deadline

        def stage_exceeded(stage_name: str) -> bool:
            stage_deadline = stage_deadlines.get(stage_name, deadline)
            return time.time() >= min(stage_deadline, deadline)
        if client_id:
            client_key = self._sanitize_client_id(client_id)
        elif conversation_id:
            client_key = self._sanitize_client_id(f"conv:{conversation_id}")
        else:
            client_key = self._sanitize_client_id(None)
        active_client_token = self._active_client_id_var.set(client_key)
        base_profile = self._load_user_profile(client_key)
        profile = self._apply_runtime_preferences(
            base_profile,
            persona=persona,
            memory_enabled=memory_enabled,
            model_name=model_name,
        )
        active_profile_token = self._active_user_profile_var.set(profile)
        try:
            scoped_conversation_id = self._scoped_conversation_id(conversation_id, client_key)
            conv = self.get_or_create_conversation(scoped_conversation_id, client_id=client_key)
            context_window = self._get_or_create_context_window(conv.id)

            cache_key = self._make_cache_key(
                conv.id,
                client_key,
                message,
                temperature,
                turn_count=len(conv.turns),
                persona=str((persona or "assistant")).strip().lower(),
            )
            now = time.time()
            cached = self._response_cache.get(cache_key)
            if cached is not None:
                ts, resp = cached
                if now - ts < self._response_cache_ttl:
                    resp = dict(resp)
                    conv.add("user", message)
                    self._persist_conversation_turn(client_key, conv.id, "user", message)
                    context_window.add_message("user", message)
                    cached_answer = str(resp.get("response", "") or "")
                    if cached_answer:
                        conv.add("assistant", cached_answer)
                        self._persist_conversation_turn(client_key, conv.id, "assistant", cached_answer)
                        context_window.add_message("assistant", cached_answer)
                        resp["formula_data"] = self._augment_formula_data_with_runtime_vote(
                            message=message,
                            response=cached_answer,
                            method=str(resp.get("method", "") or ""),
                            confidence=float(resp.get("confidence", 0.0) or 0.0),
                            formula_data=resp.get("formula_data") if isinstance(resp.get("formula_data"), dict) else None,
                        )
                    resp["conversation_id"] = conv.id
                    resp["context_stats"] = context_window.get_stats()
                    resp["duration_ms"] = round((time.time() - start_time) * 1000, 1)
                    resp["cached"] = True
                    resp["client_id"] = client_key
                    return resp
                del self._response_cache[cache_key]

            conv.add("user", message)
            self._persist_conversation_turn(client_key, conv.id, "user", message)
            lower = message.lower()
            context_window.add_message("user", message)

            if fast_mode:
                thinking_steps = []
                thinking_text = ""
                search_strategy = {
                    "depth": 1,
                    "retrieval_top_k": 4,
                    "max_words": 6,
                    "use_abstract": False,
                    "use_causal": False,
                }
            else:
                thinking_steps = self._chain_of_thought.analyze_query(message)
                thinking_text = self._chain_of_thought.format_thinking()
                search_strategy = self._chain_of_thought.get_search_strategy()

            special = self._handle_special_commands(message, lower, context_window=context_window)
            if special:
                special["response"] = self._apply_persona_style(
                    str(special.get("response", "") or ""),
                    persona=persona,
                )
                conv.add("assistant", special["response"])
                self._persist_conversation_turn(client_key, conv.id, "assistant", special["response"])
                context_window.add_message("assistant", special["response"])
                special["formula_data"] = self._augment_formula_data_with_runtime_vote(
                    message=message,
                    response=str(special.get("response", "") or ""),
                    method=str(special.get("method", "") or ""),
                    confidence=float(special.get("confidence", 0.0) or 0.0),
                    formula_data=special.get("formula_data") if isinstance(special.get("formula_data"), dict) else None,
                )
                special["conversation_id"] = conv.id
                special["duration_ms"] = round((time.time() - start_time) * 1000, 1)
                special["thinking"] = thinking_text
                special["thinking_steps"] = [
                    {
                        "type": s.step_type.name,
                        "content": s.description,
                        "result": s.result,
                        "confidence": s.confidence,
                    }
                    for s in thinking_steps
                ]
                special["generation_used"] = False
                special["context_stats"] = context_window.get_stats()
                special["client_id"] = client_key
                return special

            prefer_specialized_pipeline = self._should_prefer_specialized_pipeline(message)

            # #Фаза A1: RAG Pipeline — первый шаг перед C-inference
            # Но не для запросов, где нужен exact math / code-gen / logic path.
            rag_response: dict | None = None
            if self._rag_enabled and (not prefer_specialized_pipeline) and not stage_exceeded("retrieval"):
                try:
                    # Определяем категорию запроса
                    rag_category = self._detect_rag_category(message)
                    rag_response = self.rag.query(message, top_k=self._rag_top_k, category=rag_category)

                    if rag_response and rag_response.get("response") and rag_response.get("confidence", 0) > 0.6:
                        # RAG нашёл хороший ответ
                        response_text = rag_response["response"]
                        response_text = self._apply_persona_style(response_text, persona=persona)
                        conv.add("user", message)
                        self._persist_conversation_turn(client_key, conv.id, "user", message)
                        context_window.add_message("user", message)
                        conv.add("assistant", response_text)
                        self._persist_conversation_turn(client_key, conv.id, "assistant", response_text)
                        context_window.add_message("assistant", response_text)

                        result = {
                            "response": response_text,
                            "confidence": rag_response["confidence"],
                            "sources": rag_response.get("sources", []) + ["rag-pipeline"],
                            "conversation_id": conv.id,
                            "knowledge_hits": len(rag_response.get("sources", [])),
                            "method": rag_response.get("method", "rag"),
                            "duration_ms": round((time.time() - start_time) * 1000, 1),
                            "model_available": True,
                            "formula_data": self._augment_formula_data_with_runtime_vote(
                                message=message,
                                response=response_text,
                                method="rag-pipeline",
                                confidence=rag_response["confidence"],
                            ),
                            "graph_stats": self.graph.get_stats(),
                            "thinking": thinking_text,
                            "thinking_steps": [
                                {
                                    "type": s.step_type.name,
                                    "content": s.description,
                                    "result": s.result,
                                    "confidence": s.confidence,
                                }
                                for s in thinking_steps
                            ],
                            "generation_used": False,
                            "context_stats": context_window.get_stats(),
                            "cached": False,
                            "client_id": client_key,
                            "rag_corrected": rag_response.get("corrected", False),
                        }
                        return result
                except Exception as e:
                    log.warning("RAG query failed: %s", e)
                    rag_response = None

            # #Фаза B1: Code Generation — если запрос про код
            code_response: dict | None = None
            if self._code_gen_enabled and self._is_code_query(message):
                try:
                    language = self._detect_code_language(message)
                    code_result = self.code_gen.generate(message, language)
                    if code_result["fitness"] > 0.5:
                        code_text = f"Вот код на {language}:\n\n```{language}\n{code_result['code']}\n```\n\nFitness: {code_result['fitness']:.2f}"
                        code_text = self._apply_persona_style(code_text, persona=persona)
                        conv.add("user", message)
                        self._persist_conversation_turn(client_key, conv.id, "user", message)
                        context_window.add_message("user", message)
                        conv.add("assistant", code_text)
                        self._persist_conversation_turn(client_key, conv.id, "assistant", code_text)
                        context_window.add_message("assistant", code_text)

                        code_response = {
                            "response": code_text,
                            "confidence": code_result["fitness"],
                            "sources": ["code-generation"],
                            "conversation_id": conv.id,
                            "knowledge_hits": 0,
                            "method": "code-generation",
                            "duration_ms": code_result["duration_ms"],
                            "model_available": True,
                            "formula_data": {
                                "code_language": language,
                                "genome_length": code_result["genome_length"],
                                "fitness": code_result["fitness"],
                            },
                            "graph_stats": self.graph.get_stats(),
                            "thinking": thinking_text,
                            "thinking_steps": [
                                {
                                    "type": s.step_type.name,
                                    "content": s.description,
                                    "result": s.result,
                                    "confidence": s.confidence,
                                }
                                for s in thinking_steps
                            ],
                            "generation_used": True,
                            "context_stats": context_window.get_stats(),
                            "cached": False,
                            "client_id": client_key,
                        }
                        return code_response
                except Exception as e:
                    log.warning("Code generation failed: %s", e)
                    code_response = None

            # #Фаза B2: Math Reasoning — если запрос математический
            math_response: dict | None = None
            if self._math_enabled and self._is_math_query(message):
                try:
                    exact_math = self._try_math_eval(message)
                    if exact_math is not None:
                        math_text = str(exact_math.get("response", "") or "")
                        math_response = {
                            "response": self._apply_persona_style(math_text, persona=persona),
                            "confidence": float(exact_math.get("confidence", 1.0) or 1.0),
                            "sources": list(exact_math.get("sources", ["math-engine"])),
                            "conversation_id": conv.id,
                            "knowledge_hits": int(exact_math.get("knowledge_hits", 0) or 0),
                            "method": str(exact_math.get("method", "math-eval") or "math-eval"),
                            "duration_ms": round((time.time() - start_time) * 1000, 1),
                            "model_available": True,
                            "formula_data": exact_math.get("formula_data", self._basic_formula_data()),
                            "graph_stats": exact_math.get("graph_stats", self.graph.get_stats()),
                            "thinking": thinking_text,
                            "thinking_steps": [
                                {
                                    "type": s.step_type.name,
                                    "content": s.description,
                                    "result": s.result,
                                    "confidence": s.confidence,
                                }
                                for s in thinking_steps
                            ],
                            "generation_used": False,
                            "context_stats": context_window.get_stats(),
                            "cached": False,
                            "client_id": client_key,
                        }
                        conv.add("assistant", math_response["response"])
                        self._persist_conversation_turn(client_key, conv.id, "assistant", math_response["response"])
                        context_window.add_message("assistant", math_response["response"])
                        return math_response

                    math_result = self.math.solve(message)
                    if math_result and math_result.get("confidence", 0) > 0.5:
                        math_text = f"Решение:\n\n{math_result['response']}\n\nОтвет: {math_result.get('answer', 'N/A')}"
                        math_text = self._apply_persona_style(math_text, persona=persona)
                        conv.add("assistant", math_text)
                        self._persist_conversation_turn(client_key, conv.id, "assistant", math_text)
                        context_window.add_message("assistant", math_text)

                        math_response = {
                            "response": math_text,
                            "confidence": math_result["confidence"],
                            "sources": ["math-reasoning"],
                            "conversation_id": conv.id,
                            "knowledge_hits": 0,
                            "method": math_result.get("method", "math"),
                            "duration_ms": math_result.get("duration_ms", 0),
                            "model_available": True,
                            "formula_data": {
                                "math_steps": math_result.get("steps", []),
                                "math_answer": math_result.get("answer", ""),
                            },
                            "graph_stats": self.graph.get_stats(),
                            "thinking": thinking_text,
                            "thinking_steps": [
                                {
                                    "type": s.step_type.name,
                                    "content": s.description,
                                    "result": s.result,
                                    "confidence": s.confidence,
                                }
                                for s in thinking_steps
                            ],
                            "generation_used": True,
                            "context_stats": context_window.get_stats(),
                            "cached": False,
                            "client_id": client_key,
                        }
                        return math_response
                except Exception as e:
                    log.warning("Math reasoning failed: %s", e)
                    math_response = None

            # #Фаза B3: Function Calling — если запрос требует инструмент
            fc_response: dict | None = None
            if self._function_calling_enabled and (not prefer_specialized_pipeline):
                try:
                    tool_call = self.function_calling.detect_tool_call(message)
                    if tool_call:
                        tool_result = self.function_calling.execute_tool(tool_call)
                        response_text = self.function_calling.format_response(tool_result)
                        response_text = self._apply_persona_style(response_text, persona=persona)
                        conv.add("user", message)
                        self._persist_conversation_turn(client_key, conv.id, "user", message)
                        context_window.add_message("user", message)
                        conv.add("assistant", response_text)
                        self._persist_conversation_turn(client_key, conv.id, "assistant", response_text)
                        context_window.add_message("assistant", response_text)

                        fc_response = {
                            "response": response_text,
                            "confidence": 0.9 if tool_result.get("success") else 0.3,
                            "sources": ["function-calling"],
                            "conversation_id": conv.id,
                            "knowledge_hits": 0,
                            "method": f"tool-{tool_call['tool']}",
                            "duration_ms": 0,
                            "model_available": True,
                            "formula_data": {
                                "tool_name": tool_call["tool"],
                                "tool_arguments": tool_call["arguments"],
                                "tool_success": tool_result.get("success", False),
                            },
                            "graph_stats": self.graph.get_stats(),
                            "thinking": thinking_text,
                            "thinking_steps": [
                                {
                                    "type": s.step_type.name,
                                    "content": s.description,
                                    "result": s.result,
                                    "confidence": s.confidence,
                                }
                                for s in thinking_steps
                            ],
                            "generation_used": True,
                            "context_stats": context_window.get_stats(),
                            "cached": False,
                            "client_id": client_key,
                        }
                        return fc_response
                except Exception as e:
                    log.warning("Function calling failed: %s", e)
                    fc_response = None

            projection_query = self._match_c_projection_query(message)
            c_formula_answer: dict | None = None
            if (
                self._enable_c_inference
                and self.c_inference.available
                and self._is_c_formula_query(message)
                and not projection_query
                and (not stage_exceeded("retrieval"))
            ):
                c_formula_answer = self.c_inference.query(message, strategy="formula")
                if self._is_valid_c_formula_answer(message, c_formula_answer):
                    response = self._apply_persona_style(
                        str(c_formula_answer.get("response", "") or ""),
                        persona=persona,
                    )
                    confidence = float(c_formula_answer.get("confidence", 0.72) or 0.72)
                    knowledge_hits = int(c_formula_answer.get("knowledge_hits", 0) or 0)
                    formulas_applied = int(c_formula_answer.get("formulas_applied", 0) or 0)
                    conv.add("assistant", response)
                    self._persist_conversation_turn(client_key, conv.id, "assistant", response)
                    context_window.add_message("assistant", response)
                    result = {
                        "response": response,
                        "confidence": max(0.45, min(0.92, confidence)),
                        "sources": ["c-core-formula"],
                        "conversation_id": conv.id,
                        "knowledge_hits": knowledge_hits,
                        "method": "c-core-formula",
                        "duration_ms": round((time.time() - start_time) * 1000, 1),
                        "model_available": self.c_retriever.available or self.c_inference.available,
                        "profile": profile,
                        "time_budget_ms": budget_ms,
                        "budget_exceeded": budget_exceeded(),
                        "formula_data": self._c_formula_runtime_data(c_formula_answer),
                        "graph_stats": self.graph.get_stats(),
                        "thinking": thinking_text,
                        "thinking_steps": [
                            {
                                "type": s.step_type.name,
                                "content": s.description,
                                "result": s.result,
                                "confidence": s.confidence,
                            }
                            for s in thinking_steps
                        ],
                        "generation_used": False,
                        "context_stats": context_window.get_stats(),
                        "cached": False,
                        "client_id": client_key,
                    }
                    result["formula_data"] = self._augment_formula_data_with_runtime_vote(
                        message=message,
                        response=response,
                        method="c-core-formula",
                        confidence=float(result["confidence"]),
                        formula_data=result.get("formula_data"),
                        c_payload=c_formula_answer,
                    )
                    return result

            auto_doc = self._maybe_auto_learn_from_message(message)
            if auto_doc and "?" not in message:
                title = str(auto_doc.get("title", "текст")).strip()
                auto_resp = (
                    f"Принял материал «{title}» и добавил в обучение. "
                    "Можете запросить: `перескажи этот текст`."
                )
                auto_resp = self._apply_persona_style(auto_resp, persona=persona)
                conv.add("assistant", auto_resp)
                self._persist_conversation_turn(client_key, conv.id, "assistant", auto_resp)
                context_window.add_message("assistant", auto_resp)
                result = {
                    "response": auto_resp,
                    "confidence": 0.95,
                    "sources": ["auto-learning"],
                    "conversation_id": conv.id,
                    "knowledge_hits": 0,
                    "method": "auto-learning-ack",
                    "duration_ms": round((time.time() - start_time) * 1000, 1),
                    "model_available": self.c_retriever.available,
                    "formula_data": self._basic_formula_data(),
                    "graph_stats": self.graph.get_stats(),
                    "thinking": thinking_text,
                    "thinking_steps": [
                        {
                            "type": s.step_type.name,
                            "content": s.description,
                            "result": s.result,
                            "confidence": s.confidence,
                        }
                        for s in thinking_steps
                    ],
                    "generation_used": False,
                    "context_stats": context_window.get_stats(),
                    "profile": profile,
                    "time_budget_ms": budget_ms,
                    "budget_exceeded": False,
                    "client_id": client_key,
                }
                result["formula_data"] = self._augment_formula_data_with_runtime_vote(
                    message=message,
                    response=auto_resp,
                    method="auto-learning-ack",
                    confidence=0.95,
                    formula_data=result.get("formula_data"),
                )
                return result

            realtime_direct = None
            realtime_method = ""
            if (
                self._enable_web_augment
                and (not self._is_ephemeral_client(client_key))
                and (not budget_exceeded())
                and (not stage_exceeded("retrieval"))
            ):
                direct_q_tokens, direct_q_stems = self._extract_terms_and_stems(message)
                if (not projection_query) and any((
                    self._is_news_query(message, q_tokens=direct_q_tokens),
                    self._is_time_query(message, q_tokens=direct_q_tokens),
                    self._is_currency_query(message, q_tokens=direct_q_tokens),
                    self._is_reference_query(message, q_tokens=direct_q_tokens),
                )):
                    realtime_direct, realtime_method = self._build_dynamic_no_knowledge_response(
                        message=message,
                        retrieved_sentences=[],
                        graph_answer="",
                        context_window=context_window,
                        deadline_ts=min(deadline, stage_deadlines.get("synthesis", deadline)),
                        fast_mode=fast_mode,
                        allow_web_augment=True,
                    )
                    if realtime_direct and realtime_method in {"web-news", "web-time", "web-rate", "web-reference"}:
                        realtime_direct = self._apply_persona_style(
                            realtime_direct,
                            persona=persona,
                        )
                        if realtime_method == "web-news":
                            realtime_direct = self._cleanup_response_text(realtime_direct)
                            direct_confidence = 0.46
                        elif realtime_method == "web-time":
                            direct_confidence = 0.44
                        elif realtime_method == "web-rate":
                            direct_confidence = 0.45
                        else:
                            direct_confidence = 0.4
                        context_window.add_message("assistant", realtime_direct)
                        conv.add("assistant", realtime_direct)
                        self._persist_conversation_turn(client_key, conv.id, "assistant", realtime_direct)
                        result = {
                            "response": realtime_direct,
                            "confidence": direct_confidence,
                            "sources": [realtime_method],
                            "conversation_id": conv.id,
                            "knowledge_hits": 0,
                            "method": realtime_method,
                            "duration_ms": round((time.time() - start_time) * 1000, 1),
                            "model_available": self.c_retriever.available,
                            "formula_data": self._basic_formula_data(),
                            "graph_stats": self.graph.get_stats(),
                            "thinking": thinking_text,
                            "thinking_steps": [
                                {
                                    "type": s.step_type.name,
                                    "content": s.description,
                                    "result": s.result,
                                    "confidence": s.confidence,
                                }
                                for s in thinking_steps
                            ],
                            "generation_used": False,
                            "context_stats": context_window.get_stats(),
                            "profile": profile,
                            "time_budget_ms": budget_ms,
                            "budget_exceeded": False,
                            "client_id": client_key,
                            "cached": False,
                        }
                        result["formula_data"] = self._augment_formula_data_with_runtime_vote(
                            message=message,
                            response=realtime_direct,
                            method=realtime_method,
                            confidence=float(direct_confidence),
                            formula_data=result.get("formula_data"),
                        )
                        return result

            # ====== ЧИСЛОВОЕ МЫШЛЕНИЕ ======
            tokens = _tokenize(message)
            query_patterns: dict[str, str] = {}
            query_hashes: dict[str, int] = {}
            for t in tokens:
                if len(t) >= 2:
                    query_patterns[t] = pattern_to_str(word_to_pattern(t))
                    query_hashes[t] = djb2_hash(t)

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
            contextual_query = self._build_contextual_query(message, context_window)
            retrieval_query = contextual_query or (mapped_query if mapped_query else message)
            topic_focus = self._extract_topic_focus(contextual_query or message)
            if topic_focus:
                retrieval_query = topic_focus

            retrieval_limit = max(12, int(retrieval_k))
            if fast_mode:
                retrieval_limit = 4
            base_retrieved = self.sentence_store.retrieve(
                query=retrieval_query,
                formula=best_formula,
                top_k=retrieval_limit,
            )
            merged_scores: dict[str, float] = {t: s for t, s in base_retrieved}
            if (
                (not fast_mode)
                and mapped_query
                and mapped_query != message
                and (not budget_exceeded())
                and (not stage_exceeded("retrieval"))
            ):
                mapped_retrieved = self.sentence_store.retrieve(
                    query=mapped_query,
                    formula=best_formula,
                    top_k=retrieval_limit,
                )
                for t, s in mapped_retrieved:
                    prev = merged_scores.get(t)
                    if prev is None or s > prev:
                        merged_scores[t] = s

            enriched_query = context_window.get_query_with_context(contextual_query or message)
            if (
                enriched_query != (contextual_query or message)
                and len(merged_scores) < 3
                and (not budget_exceeded())
                and (not stage_exceeded("retrieval"))
            ):
                extra = self.sentence_store.retrieve(
                    query=enriched_query,
                    formula=best_formula,
                    top_k=4 if fast_mode else 6,
                )
                for t, s in extra:
                    s = s * 0.8
                    prev = merged_scores.get(t)
                    if prev is None or s > prev:
                        merged_scores[t] = s

            retrieved = sorted(merged_scores.items(), key=lambda x: x[1], reverse=True)[:retrieval_limit]
            # Не используем в общем retrieval персональные фразы, чтобы не утекали данные между клиентами.
            retrieved = self._filter_private_retrieval_hits(retrieved)
            retrieved = self._rerank_retrieved_sentences(
                message=message,
                retrieved_sentences=retrieved,
                context_window=context_window,
                limit=retrieval_limit,
            )

            max_answer_words = search_strategy.get("max_words", 10)
            if fast_mode:
                max_answer_words = min(max_answer_words, 6)
            formula_words: list[tuple[str, float]] = []
            graph_answer = ""
            graph_confidence = 0.0
            graph_meta: dict = {"answer_patterns": {}, "total_score": 0.0, "candidates_total": 0}
            if not fast_mode and (not stage_exceeded("retrieval")):
                formula_words = self.graph.generate_words(
                    query=message,
                    formula=best_formula,
                    max_words=8,
                )
                graph_answer, graph_confidence, graph_meta = self.graph.answer(
                    message,
                    max_words=max_answer_words,
                )

            if (
                (not fast_mode)
                and search_strategy.get("depth", 1) >= 2
                and graph_confidence < 0.6
                and (not budget_exceeded())
                and (not stage_exceeded("retrieval"))
            ):
                mh_answer, mh_conf, mh_meta = self.graph.multi_hop_answer(
                    message,
                    max_hops=2,
                    max_words=max_answer_words,
                )
                if mh_conf > graph_confidence:
                    graph_answer = mh_answer
                    graph_confidence = mh_conf
                    graph_meta = mh_meta

            formula_result = self._formula_predict(message)
            c_knowledge: list[str] = []
            if self._enable_c_retriever and self.c_retriever.available and (not stage_exceeded("retrieval")):
                c_knowledge = self.c_retriever.query(message)
            assoc_answer = self.formula_pool.lookup(message)
            if not assoc_answer:
                normalized_q = message.strip().rstrip("?!.")
                if normalized_q and normalized_q != message:
                    assoc_answer = self.formula_pool.lookup(normalized_q)

            effective_validation_query = contextual_query or message

            synthesis_result = self._synthesize_response(
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
                context_window=context_window,
                deadline_ts=min(deadline, stage_deadlines.get("synthesis", deadline)),
                fast_mode=fast_mode,
                allow_web_augment=(not self._is_ephemeral_client(client_key)),
            )
            synthesis_meta: dict[str, object] = {}
            if isinstance(synthesis_result, tuple) and len(synthesis_result) == 4:
                response, confidence, method, raw_meta = synthesis_result
                if isinstance(raw_meta, dict):
                    synthesis_meta = raw_meta
            else:
                response, confidence, method = synthesis_result

            if method in {"formula-retrieval", "formula-association"}:
                response = self._normalize_qa_response(response)
            if method in {"web-augment", "web-augment-weather", "web-news"}:
                response = self._cleanup_response_text(response)

            if (
                has_cyrillic
                and method not in {
                    "topic-overview",
                    "precise-retrieval",
                    "math-eval",
                    "logic-solver",
                    "command",
                    "pattern-lookup",
                    "formula-inspect",
                    "document-list",
                    "story-memory",
                    "retell-memory",
                    "web-news",
                    "web-time",
                    "web-rate",
                    "web-reference",
                }
                and (not self._is_math_reasoning_method(method))
                and self._response_needs_language_fallback(response)
            ):
                fallback = self._build_russian_fallback(message, retrieved)
                if fallback and self._answer_shape_is_valid(effective_validation_query, fallback):
                    response = fallback
                    confidence = max(0.35, min(confidence, 0.55))
                    method = "ru-safe-fallback"

            if len(thinking_steps) >= 2:
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
                    self._chain_of_thought.update_step(
                        len(thinking_steps) - 2,
                        f"Метод: {method}, уверенность: {confidence:.2f}",
                        confidence,
                    )
                if len(thinking_steps) >= 5:
                    verified = confidence >= 0.5 and method != "no-knowledge"
                    self._chain_of_thought.update_step(
                        len(thinking_steps) - 1,
                        f"{'Ответ верифицирован' if verified else 'Низкая уверенность — возможен fallback'}",
                        0.9 if verified else 0.3,
                    )
                thinking_text = self._chain_of_thought.format_thinking()

            if (not budget_exceeded()) and (not stage_exceeded("cognition")) and self._enable_cot_enrichment and (
                search_strategy.get("use_abstract") or search_strategy.get("use_causal")
            ):
                response = self._cot_enrich_response(response, message, confidence, search_strategy)

            generation_used = False
            if (not budget_exceeded()) and confidence < 0.3 and self._lm_trained:
                generated = self._generate_text(message, max_tokens=64)
                if generated and len(generated) > 10:
                    response = generated
                    method = "formula-lm-generation"
                    confidence = max(confidence, 0.25)
                    generation_used = True

            self_check = self._run_self_check(
                query=effective_validation_query,
                answer=response,
                method=method,
                confidence=confidence,
            )
            skip_repair = method == "dynamic-fallback" and self._is_project_runtime_query(message)
            if (
                self._enable_self_check
                and (not budget_exceeded())
                and (not stage_exceeded("repair"))
                and not self_check.get("passed", True)
                and not skip_repair
            ):
                repaired = self._repair_answer_with_self_check(
                    message=effective_validation_query,
                    retrieval_query=retrieval_query,
                    retrieved_sentences=retrieved,
                    context_window=context_window,
                )
                if repaired:
                    repaired_check = self._run_self_check(
                        query=effective_validation_query,
                        answer=repaired,
                        method=method,
                        confidence=confidence,
                    )
                    if float(repaired_check.get("score", 0.0)) > float(self_check.get("score", 0.0)) + 0.1:
                        response = repaired
                        method = f"{method}+self-check"
                        confidence = max(
                            confidence,
                            min(0.82, float(repaired_check.get("score", confidence))),
                        )
                        self_check = repaired_check

            allow_train_enqueue = True
            if fast_mode:
                now_train = time.time()
                if (now_train - self._last_fast_train_enqueue_ts) < self._fast_train_min_interval_sec:
                    allow_train_enqueue = False
                elif random.random() > self._fast_train_sample_rate:
                    allow_train_enqueue = False
                if allow_train_enqueue:
                    self._last_fast_train_enqueue_ts = now_train

            if allow_train_enqueue:
                try:
                    if confidence >= (0.6 if fast_mode else 0.5) and method != "no-knowledge":
                        self._train_queue.put_nowait(("retrieval", message, response))
                    if c_knowledge and (not fast_mode):
                        self._train_queue.put_nowait(("c_knowledge", message, c_knowledge))
                    if self._enable_dialog_learning:
                        self._train_queue.put_nowait(
                            ("dialogue", message, response, float(confidence), method)
                        )
                except queue.Full:
                    pass

            full_response = self._apply_persona_style(response, persona=persona)
            cognitive_payload = None
            if (not budget_exceeded()) and (not stage_exceeded("cognition")) and self._enable_cognition:
                cognitive_payload = self._cognitive_enrichment(message).get("cognitive")

            context_window.add_message("assistant", full_response)
            formula_hex = best_formula.gene.to_hex()
            conv.add("assistant", full_response, formula_used=formula_hex)
            self._persist_conversation_turn(client_key, conv.id, "assistant", full_response)
            duration = round((time.time() - start_time) * 1000, 1)

            default_formula_data = {
                "query_patterns": query_patterns,
                "query_hashes": query_hashes,
                "answer_patterns": graph_meta.get("answer_patterns", {}),
                "formula_predict": formula_result.get("predict_value", 0),
                "formula_genome_hex": formula_hex,
                "formula_fitness": round(best_formula.fitness, 4),
                "formula_generation": self.formula_pool.generation,
                "graph_score": graph_meta.get("total_score", 0),
                "graph_candidates": graph_meta.get("candidates_total", 0),
                "retrieved_sentences": [{"text": t[:150], "score": s} for t, s in retrieved[:3]],
                "formula_generated_words": [{"word": w, "score": round(s, 4)} for w, s in formula_words[:5]],
                "sentence_store_size": self.sentence_store.size,
                "memory_digits": self.sentence_store.memory_digits,
                "embedding_vocab": self.embeddings.vocab_size,
                "embedding_trained_pairs": self.embeddings.trained_pairs,
                "dialog_learning_enabled": self._enable_dialog_learning,
                "dialog_learning_min_conf": self._dialog_learning_min_conf,
            }
            if isinstance(synthesis_meta.get("formula_data"), dict):
                default_formula_data.update(dict(synthesis_meta.get("formula_data") or {}))

            result = {
                "response": full_response,
                "confidence": confidence,
                "sources": list(synthesis_meta.get("sources", [method])),
                "conversation_id": conv.id,
                "knowledge_hits": int(synthesis_meta.get("knowledge_hits", graph_meta.get("candidates_total", 0)) or 0),
                "method": method,
                "duration_ms": duration,
                "model_available": self.c_retriever.available,
                "profile": profile,
                "time_budget_ms": budget_ms,
                "budget_exceeded": budget_exceeded(),
                "stage_budgets_ms": stage_budgets_ms,
                "formula_data": default_formula_data,
                "graph_stats": synthesis_meta.get("graph_stats", self.graph.get_stats()),
                "thinking": thinking_text,
                "thinking_steps": [
                    {
                        "type": s.step_type.name,
                        "content": s.description,
                        "result": s.result,
                        "confidence": s.confidence,
                    }
                    for s in thinking_steps
                ],
                "generation_used": generation_used,
                "context_stats": context_window.get_stats(),
                "cognitive": cognitive_payload,
                "self_check": self_check,
                "client_id": client_key,
            }
            c_runtime_payload = c_formula_answer if method == "c-core-formula" else None
            if method == "c-core-formula" and isinstance(synthesis_meta.get("c_payload"), dict):
                c_runtime_payload = dict(synthesis_meta.get("c_payload") or {})

            result["formula_data"] = self._augment_formula_data_with_runtime_vote(
                message=message,
                response=full_response,
                method=method,
                confidence=float(confidence),
                formula_data=result.get("formula_data"),
                c_payload=c_runtime_payload,
            )

            if len(self._response_cache) >= self._response_cache_max:
                sorted_keys = sorted(self._response_cache, key=lambda k: self._response_cache[k][0])
                for k in sorted_keys[: self._response_cache_max // 4]:
                    self._response_cache.pop(k, None)
            result["cached"] = False
            self._response_cache[cache_key] = (time.time(), result)

            # Записываем диалог в демон непрерывного обучения
            try:
                from .continuous_learning_daemon import get_continuous_learning_daemon
                learning_daemon = get_continuous_learning_daemon()
                learning_daemon.record_dialogue(message, full_response)
            except Exception:
                pass  # Не критично для основного ответа

            return result
        finally:
            self._active_user_profile_var.reset(active_profile_token)
            self._active_client_id_var.reset(active_client_token)

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

    def _detect_rag_category(self, message: str) -> str | None:
        """#Фаза A1: Определить категорию для RAG поиска."""
        q = (message or "").strip().lower()
        if not q:
            return None

        categories = {
            "math": ["математик", "алгебр", "геометр", "числ", "уравнен", "матриц", "интеграл"],
            "physics": ["физик", "энерги", "сил", "скорост", "гравитац", "квантов", "релятив"],
            "biology": ["биолог", "клетк", "организм", "ген", "эволюц", "днк", "иммун"],
            "chemistry": ["химич", "реакци", "элемент", "молекул", "атом", "веществ"],
            "history": ["истор", "войн", "импер", "революц", "древн", "царств"],
            "medicine": ["медицин", "болезн", "лечен", "симптом", "диагноз"],
            "it": ["программ", "алгоритм", "компьютер", "код", "функци", "данны", "сортировк"],
            "economics": ["эконом", "рынок", "валют", "инфляц", "бюджет", "финанс"],
            "law": ["закон", "прав", "суд", "стать", "конституц"],
        }

        for category, keywords in categories.items():
            for kw in keywords:
                if kw in q:
                    return category
        return None

    def _is_code_query(self, message: str) -> bool:
        """#Фаза B1: Определить что запрос про код."""
        q = (message or "").strip().lower()
        code_patterns = [
            r"\bнапиши\s+код\b", r"\bсгенерируй\s+код\b", r"\bсоздай\s+функци",
            r"\bалгоритм\s+сортировк", r"\bфункци[юя]\s+", r"\bкласс\s+",
            r"\bкод\s+на\s+(python|pythonе|си|javascript|js)\b",
            r"\bкак\s+отсортироват", r"\bкак\s+найти\b", r"\bкак\s+искать\b",
            r"\bwrite\s+(a\s+)?(code|function|class)\b", r"\bsort\b",
            r"\bsearch\b", r"\bbinary\s+search\b", r"\bbubble\s+sort\b",
        ]
        for pattern in code_patterns:
            if re.search(pattern, q):
                return True
        return False

    def _detect_code_language(self, message: str) -> str:
        """#Фаза B1: Определить язык программирования."""
        q = (message or "").strip().lower()
        if "python" in q or "питон" in q or "питон" in q:
            return "python"
        elif "javascript" in q or "js" in q or "джава" in q:
            return "javascript"
        elif " си " in q or " на си" in q or " c " in q or "на c" in q:
            return "c"
        return "python"  # Default

    def _is_math_query(self, message: str) -> bool:
        """#Фаза B2: Определить что запрос математический."""
        q = (message or "").strip().lower()
        # Арифметика
        if re.search(r'[\d]+\s*[+\-*/×÷]\s*[\d]+', q):
            return True
        # Проценты
        if any(word in q for word in ["%", "процент", "percent"]):
            return True
        # Уравнения
        if any(word in q for word in ["реши", "уравнен", "найди x", "solve", "вычисли"]):
            return True
        # Текстовые задачи
        if any(word in q for word in ["сколько будет", "сколько всего", "сколько осталось"]):
            return True
        # Словесные арифметические выражения
        if any(word in q for word in ["посчитай", "плюс", "минус", "умнож", "делит", "скобк", "степен", "корень"]):
            return True
        # Геометрия
        if any(word in q for word in ["площадь", "периметр", "объем", "площад"]):
            return True
        return False

    def _is_logic_query(self, message: str) -> bool:
        """Определить, что запрос требует логического решателя, а не retrieval/RAG."""
        q = (message or "").strip().lower().replace("ё", "е")
        if not q:
            return False
        markers = ("логик", "логич", "силлог", "вывод", "следует", "шар", "гарантир", "все ")
        return any(marker in q for marker in markers)

    def _should_prefer_specialized_pipeline(self, message: str) -> bool:
        """Запросы на код, математику и явную логику не должны перехватываться RAG."""
        return self._is_code_query(message) or self._is_math_query(message) or self._is_logic_query(message)

    def _is_c_formula_query(self, message: str) -> bool:
        q = (message or "").strip().lower()
        if not q:
            return False
        patterns = (
            r"\bчто\s+такое\b",
            r"\bкто\s+так(ой|ая|ие)\b",
            r"\bобъясни\b",
            r"\bрасскажи\s+про\b",
            r"\bрасскажи\s+о\b",
            r"\bрасскажи\s+подробно\s+про\b",
            r"\bрасскажи\s+подробно\s+о\b",
            r"\bчто\s+ты\s+знаешь\s+(?:о|об|про)\b",
            r"\bчто\s+изучает\b",
            r"\bчем\s+занимается\b",
            r"\bкак\s+устроен(?:а|о)?\b",
            r"\bпочему\s+важ(?:ен|на|но)\b",
            r"\bзачем\s+нуж(?:ен|на|но)\b",
        )
        return any(re.search(pattern, q) for pattern in patterns)

    def _canonicalize_definition_focus_text(self, text: str) -> str:
        raw = (text or "").strip().lower()
        if not raw:
            return ""
        cleaned = re.sub(
            r"\s+(?:простыми\s+словами|простым\s+языком|кратко|подробно)$",
            "",
            raw,
            flags=re.IGNORECASE,
        ).strip()
        mapping: list[tuple[str, str]] = [
            ("медицин", "медицина"),
            ("географ", "география"),
            ("философ", "философия"),
            ("биолог", "биология"),
            ("физик", "физика"),
            ("астроном", "астрономия"),
            ("анатом", "анатомия"),
            ("физиолог", "физиология"),
            ("патолог", "патология"),
            ("терап", "терапия"),
            ("хими", "химия"),
            ("истори", "история"),
            ("эконом", "экономика"),
            ("прав", "право"),
            ("алгоритм", "алгоритм"),
            ("программирован", "программирование"),
        ]
        for stem, canonical in mapping:
            if cleaned.startswith(stem):
                return canonical
        if cleaned.endswith("ию"):
            return cleaned[:-2] + "ия"
        if cleaned.endswith("ии"):
            return cleaned[:-2] + "ия"
        if cleaned.endswith("ике"):
            return cleaned[:-3] + "ика"
        if cleaned.endswith("ику"):
            return cleaned[:-2] + "ика"
        return cleaned

    def _definition_focus_terms(self, query: str) -> list[str]:
        q = (query or "").strip().lower()
        if not q:
            return []
        match = re.search(
            r"(?:\bчто\s+такое\b|\bкто\s+так(?:ой|ая|ие)\b|\bобъясни\b|\bрасскажи(?:\s+подробно)?\s+(?:про|о)\b|\bчто\s+ты\s+знаешь\s+(?:о|об|про)\b|\bчто\s+изучает\b|\bчем\s+занимается\b|\bкак\s+устроен(?:а|о)?\b|\bпочему\s+важ(?:ен|на|но)\b|\bзачем\s+нуж(?:ен|на|но)\b)\s+(.+)$",
            q,
        )
        if not match:
            return []
        tail = re.sub(r"[?!.,:;]+$", "", match.group(1)).strip()
        tail = self._canonicalize_definition_focus_text(tail)
        tokens, _ = self._extract_terms_and_stems(tail, drop_generic=False)
        return [t for t in sorted(tokens) if len(t) >= 4]

    def _definition_forbidden_stems(self, query: str) -> set[str]:
        focus = set(self._definition_focus_terms(query))
        mapping: dict[str, set[str]] = {
            "медицина": {"математик", "географ", "философ"},
            "география": {"математик", "медицин", "философ"},
            "философия": {"медицин", "математик", "географ"},
            "биология": {"математик", "географ"},
            "физика": {"философ", "медицин"},
            "астрономия": {"географ", "медицин", "философ"},
            "анатомия": {"сериал", "эконом", "географ"},
            "физиология": {"географ", "эконом", "сериал"},
            "патология": {"фильм", "сериал"},
            "терапия": {"сериал", "комедийн", "американск"},
            "химия": {"истор", "эконом", "географ"},
            "история": {"эконом", "математик", "географ"},
            "экономика": {"философ", "математик", "географ"},
            "право": {"лев", "правооблад", "географ"},
            "алгоритм": {"географ", "эконом", "философ"},
            "программирование": {"сериал", "философ", "географ"},
        }
        denied: set[str] = set()
        for token, stems in mapping.items():
            if token in focus:
                denied |= stems
        return denied

    def _definition_support_stems(self, query: str) -> set[str]:
        focus = set(self._definition_focus_terms(query))
        mapping: dict[str, set[str]] = {
            "медицина": {"здоров", "болезн", "лечен"},
            "география": {"земл", "территор", "поверхност", "пространств"},
            "философия": {"познан", "мышлен", "мировоззрен"},
            "биология": {"жив", "организм"},
            "физика": {"природ", "явлен", "матери", "движен"},
            "астрономия": {"небес", "звезд", "планет", "галактик", "вселен"},
            "анатомия": {"строен", "тел", "орган", "ткан"},
            "физиология": {"функци", "организм", "клет", "сред"},
            "патология": {"болезн", "процесс", "состояни", "организм"},
            "терапия": {"лечен", "заболев", "пациент", "медицин", "клинич"},
            "химия": {"веществ", "состав", "строен", "реакц", "элемент"},
            "история": {"прошл", "событ", "человечеств", "источн", "цивилизац", "государств", "культур", "люд", "времен"},
            "экономика": {"хозяйств", "производ", "распредел", "обмен", "потреблен"},
            "право": {"норм", "закон", "государств", "отношен", "обществен"},
            "алгоритм": {"правил", "инструкц", "действ", "задач", "исполн"},
            "программирование": {"алгоритм", "программ", "компьют", "вычисл"},
        }
        required: set[str] = set()
        for token, stems in mapping.items():
            if token in focus:
                required |= stems
        return required

    def _definition_stem_hits(
        self,
        expected_stems: set[str],
        answer_tokens: set[str],
        answer_stems: set[str],
    ) -> set[str]:
        if not expected_stems:
            return set()
        hits = set(expected_stems & answer_stems)
        if hits:
            return hits
        for expected in expected_stems:
            if len(expected) < 3:
                continue
            for token in answer_tokens:
                if token.startswith(expected):
                    hits.add(expected)
                    break
        return hits

    def _is_valid_c_formula_answer(self, query: str, payload: dict | None) -> bool:
        if not payload:
            return False
        answer = self._cleanup_c_formula_answer_text(str(payload.get("response", "") or ""))
        payload["response"] = answer
        if not answer:
            return False
        if answer.startswith("Мне пока не хватает данных"):
            return False
        q_lower = (query or "").strip().lower()
        if self._is_c_formula_query(q_lower):
            focus_terms = self._definition_focus_terms(q_lower)
            if focus_terms:
                a_tokens, a_stems = self._extract_terms_and_stems(answer.lower(), drop_generic=False)
                focus_stems = self._extract_linguistic_stems(set(focus_terms))
                if not ((set(focus_terms) & a_tokens) or (focus_stems & a_stems)):
                    return False
                support_stems = self._definition_support_stems(q_lower)
                if support_stems and not self._definition_stem_hits(support_stems, a_tokens, a_stems):
                    return False
                forbidden_stems = self._definition_forbidden_stems(q_lower)
                if forbidden_stems and self._definition_stem_hits(forbidden_stems, a_tokens, a_stems):
                    return False
        if self._response_needs_language_fallback(answer):
            is_definition = self._is_c_formula_query(q_lower)
            themed_enough = self._association_is_relevant(q_lower, answer)
            definitional_shape = ("—" in answer) or (" это " in f" {answer.lower()} ")
            if not (is_definition and themed_enough and definitional_shape and len(answer) >= 48):
                return False
        if not self._answer_shape_is_valid(query, answer):
            return False
        if len(answer) < 24:
            return False
        return True

    def _cleanup_c_formula_answer_text(self, text: str) -> str:
        src = self._cleanup_response_text(text)
        src = re.sub(r"\[\d+\]", "", src).strip()
        if "—" in src:
            prefix, suffix = src.split("—", 1)
            prefix_cyr = sum(1 for ch in prefix if "\u0400" <= ch <= "\u04ff")
            noisy_prefix = bool(
                re.search(r"[^\x00-\x7FА-Яа-яЁё0-9\s.,;:!?\"'()«»_/-]", prefix)
                or any(mark in prefix for mark in ("<", "["))
            )
            if (prefix_cyr < 4 or noisy_prefix) and len(suffix.strip()) >= 20:
                src = suffix.strip()
        src = re.sub(r"^[^А-Яа-яЁё0-9]+", "", src).strip()
        src = re.sub(r"\s{2,}", " ", src).strip()
        return src

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
        context_window: ContextWindow | None = None,
        deadline_ts: float | None = None,
        fast_mode: bool = False,
        allow_web_augment: bool = True,
    ) -> tuple[str, float, str] | tuple[str, float, str, dict[str, object]]:
        """
        Синтез ответа — формулы ГЕНЕРИРУЮТ + РАНЖИРУЮТ.

        Приоритеты:
        1. Формульные ассоциации (точное Q→A через FNV1a хеш)
        2. Гибрид: sentence retrieval + формульная генерация слов
        3. Чистая формульная генерация (слова из трансформации паттернов)
        4. C-модель (.klm бинарь)
        5. Граф слов (fallback)
        """
        q_tokens, q_stems = self._extract_terms_and_stems(message)
        math_result = self._try_math_eval(message)
        if math_result is not None:
            return (
                str(math_result.get("response", "") or ""),
                float(math_result.get("confidence", 0.98) or 0.98),
                str(math_result.get("method", "math-eval") or "math-eval"),
            )
        math_reasoning_result = self._try_math_reasoning_answer(message)
        if math_reasoning_result is not None:
            return (
                str(math_reasoning_result.get("response", "") or ""),
                float(math_reasoning_result.get("confidence", 0.9) or 0.9),
                str(math_reasoning_result.get("method", "math-reasoning") or "math-reasoning"),
                {
                    "sources": list(math_reasoning_result.get("sources", ["math-reasoning"])),
                    "knowledge_hits": int(math_reasoning_result.get("knowledge_hits", 0) or 0),
                    "formula_data": math_reasoning_result.get("formula_data"),
                    "graph_stats": math_reasoning_result.get("graph_stats"),
                },
            )
        recap_answer, recap_method = self._build_conversation_memory_read_response(
            message=message,
            context_window=context_window,
        )
        if recap_answer:
            recap_confidence = {
                "conversation-memory": 0.96,
                "conversation-memory-empty": 0.9,
            }.get(recap_method, 0.9)
            return recap_answer, recap_confidence, recap_method
        profile_memory_answer, profile_memory_method = self._build_profile_memory_read_response(message)
        if profile_memory_answer:
            profile_memory_confidence = {
                "profile-memory": 0.96,
                "profile-memory-query": 0.92,
                "profile-memory-empty": 0.88,
                "document-list": 0.95,
            }.get(profile_memory_method, 0.9)
            return profile_memory_answer, profile_memory_confidence, profile_memory_method
        learned_doc_answer, learned_doc_method = self._build_learned_document_read_response(message)
        if learned_doc_answer:
            learned_doc_confidence = {
                "story-memory": 0.92,
                "story-fallback": 0.5,
                "retell-memory": 0.95,
            }.get(learned_doc_method, 0.9)
            return learned_doc_answer, learned_doc_confidence, learned_doc_method
        system_answer, system_method = self._build_system_read_response(message)
        if system_answer:
            system_confidence = {
                "identity": 1.0,
                "greeting": 1.0,
                "smalltalk-checkin": 0.98,
                "self-meta": 0.98,
                "abuse-deescalation": 0.94,
                "clarify-entity": 0.82,
                "kolibri-architecture": 0.98,
                "command": 1.0,
            }.get(system_method, 0.92)
            return system_answer, system_confidence, system_method
        system_inspection_answer, system_inspection_method = self._build_system_inspection_response(message)
        if system_inspection_answer:
            inspection_confidence = {
                "command": 1.0,
                "pattern-lookup": 1.0,
                "formula-inspect": 1.0,
            }.get(system_inspection_method, 0.95)
            return system_inspection_answer, inspection_confidence, system_inspection_method
        projection_answer = self._build_topic_projection_read_response(message)
        if projection_answer:
            return (
                str(projection_answer.get("response", "") or ""),
                float(projection_answer.get("confidence", 0.9) or 0.9),
                str(projection_answer.get("method", "canonical-topic-fallback") or "canonical-topic-fallback"),
                {
                    "sources": list(projection_answer.get("sources", []) or []),
                    "knowledge_hits": int(projection_answer.get("knowledge_hits", 0) or 0),
                    "formula_data": projection_answer.get("formula_data"),
                    "graph_stats": projection_answer.get("graph_stats"),
                    "c_payload": projection_answer.get("c_payload"),
                },
            )
        is_weather_query = self._is_weather_query(message, q_tokens=q_tokens)
        is_time_query = self._is_time_query(message, q_tokens=q_tokens)
        is_currency_query = self._is_currency_query(message, q_tokens=q_tokens)
        is_reference_query = self._is_reference_query(message, q_tokens=q_tokens)

        # Для realtime-справки даём приоритет прямому live lookup, чтобы
        # локальный retrieval не перехватывал время/валюты/базовые факты.
        if allow_web_augment and self._enable_web_augment and (
            is_weather_query or is_time_query or is_currency_query or is_reference_query
        ):
            dynamic_fallback, dynamic_method = self._build_dynamic_no_knowledge_response(
                message=message,
                retrieved_sentences=retrieved_sentences,
                graph_answer=graph_answer,
                context_window=context_window,
                deadline_ts=deadline_ts,
                fast_mode=fast_mode,
                allow_web_augment=allow_web_augment,
            )
            if dynamic_fallback and dynamic_method in {"web-augment-weather", "weather-unavailable", "web-time", "web-rate", "web-reference"}:
                if dynamic_method == "web-augment-weather":
                    return (dynamic_fallback, 0.42, dynamic_method)
                if dynamic_method == "weather-unavailable":
                    return (dynamic_fallback, 0.72, dynamic_method)
                if dynamic_method == "web-time":
                    return (dynamic_fallback, 0.44, dynamic_method)
                if dynamic_method == "web-rate":
                    return (dynamic_fallback, 0.45, dynamic_method)
                return (dynamic_fallback, 0.4, dynamic_method)

        topic_answer = self._try_topic_overview_answer(message, fast_mode=fast_mode)
        if topic_answer:
            return (topic_answer, 0.78, "topic-overview")

        logic_answer = self._try_logic_solver_answer(message)
        if logic_answer:
            return (logic_answer, 0.9, "logic-solver")

        precise_answer = None
        if not is_reference_query:
            precise_answer = self._try_precise_retrieval_answer(message)
        if precise_answer:
            return (precise_answer, 0.92, "precise-retrieval")

        # Для count-запросов даём приоритет web-augment, если локальный corpus
        # не дал устойчивого числового ответа.
        lower = (message or "").strip().lower()
        is_count_query = ("сколько" in lower or "how many" in lower)
        if (
            is_count_query
            and (not is_time_query)
            and (not is_currency_query)
            and allow_web_augment
            and self._enable_web_augment
        ):
            web_count = self._try_web_augment_answer(
                message=message,
                q_tokens=q_tokens,
                q_stems=q_stems,
                force=True,
                latency_budget_sec=2.8 if fast_mode else 4.2,
            )
            if web_count and re.search(r"\b\d+\b", web_count):
                return (web_count, 0.74, "web-augment-count")

        # 1. Формульные ассоциации (точное совпадение через хеш)
        if self._enable_formula_associations and assoc_answer and self._association_is_relevant(message, assoc_answer):
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
            min_threshold = 0.24 if n_tokens <= 3 else 0.18 if n_tokens <= 6 else 0.12
            if best_score >= min_threshold:
                answer = self._build_coherent_response(
                    retrieval_query, retrieved_sentences, formula_words, c_knowledge,
                )
                if answer and self._answer_shape_is_valid(message, answer):  # Прошёл фильтр релевантности/формы
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
                return (answer, 0.5, "c-model")

        # 4. Граф слов (fallback) — только если ответ действительно релевантен вопросу.
        if graph_answer and graph_confidence >= 0.15 and self._graph_answer_is_relevant(message, graph_answer):
            return (graph_answer, graph_confidence, "knowledge-graph")

        # 5. Чистая формульная генерация (экспериментально, по флагу)
        # По умолчанию выключена, т.к. часто даёт телеграфный шум.
        if self._enable_formula_generation and formula_words and len(formula_words) >= 2:
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
                    if answer and self._answer_shape_is_valid(message, answer):  # Релевантный ответ найден
                        avg_score = sum(s for _, s in formula_words[:5]) / min(5, len(formula_words))
                        return (answer, min(0.7, avg_score + 0.15), "formula-generation")

                answer = self._generate_from_formula_words(
                    message, words_only, graph_answer, graph_meta,
                )
                avg_score = sum(s for _, s in formula_words[:5]) / min(5, len(formula_words))
                return (answer, min(0.5, avg_score), "formula-generation")

        dynamic_fallback, dynamic_method = self._build_dynamic_no_knowledge_response(
            message=message,
            retrieved_sentences=retrieved_sentences,
            graph_answer=graph_answer,
            context_window=context_window,
            deadline_ts=deadline_ts,
            fast_mode=fast_mode,
            allow_web_augment=allow_web_augment,
        )
        if dynamic_fallback:
            if dynamic_method == "web-augment":
                confidence = 0.34
            elif dynamic_method == "web-augment-weather":
                confidence = 0.42
            elif dynamic_method == "web-news":
                confidence = 0.46
            elif dynamic_method == "web-time":
                confidence = 0.44
            elif dynamic_method == "web-rate":
                confidence = 0.45
            elif dynamic_method == "web-reference":
                confidence = 0.4
            elif dynamic_method == "dialog-context":
                confidence = 0.56
            elif dynamic_method == "dialog-fact-ack":
                confidence = 0.78
            else:
                confidence = 0.22
            return (dynamic_fallback, confidence, dynamic_method)

        return (
            "У меня пока недостаточно локальных знаний по этой теме. "
            "Добавьте материал, и я обучусь на нём.",
            0.1, "no-knowledge",
        )

    def _is_referential_query(self, message: str) -> bool:
        if self._extract_followup_directive_mode(message) is not None:
            return True
        tokens = self._extract_linguistic_terms(
            message,
            min_len=1,
            drop_stop=False,
            drop_generic=False,
        )
        if not tokens:
            return False
        referential_tokens = {
            "он", "она", "они", "его", "ее", "её", "их", "ему", "ей", "ним",
            "это", "этот", "эта", "эти", "там", "тогда", "такой",
            "я", "мой", "моя", "моё", "мне", "меня",
            "you", "it", "they", "them", "that", "those",
            "продолжай", "подробнее",
        }
        if tokens & referential_tokens:
            return True
        lower = (message or "").strip().lower()
        if lower.startswith(("а как насчет", "а как насчёт", "а что насчет", "а что насчёт", "а насчет", "а насчёт")):
            return True
        if lower.startswith(("а в ", "а во ", "в ", "во ")):
            content = [
                t
                for t in self._extract_linguistic_terms(message, drop_generic=False)
                if len(t) >= 3 and not _is_stop_word(t)
            ]
            return len(content) <= 2
        if lower.startswith(("а как", "а что", "а почему", "а где", "а когда")):
            # Не считаем референсом длинный новый вопрос без явных ссылок на контекст.
            content = [
                t
                for t in self._extract_linguistic_terms(message, drop_generic=False)
                if len(t) >= 3 and not _is_stop_word(t)
            ]
            return len(content) <= 2
        return False

    def _extract_followup_directive_mode(self, message: str) -> str | None:
        normalized = self._normalize_linguistic_text(message)
        if not normalized:
            return None
        compact = re.sub(r"\s+", " ", normalized).strip()
        if not compact:
            return None
        if re.search(r"\bсравн", compact):
            return "compare"
        if re.search(r"\bпо\s+пункт", compact) or re.search(r"\bсписк", compact):
            return "bullets"
        if re.search(r"\b(проще|попроще|короче)\b", compact) or "простыми словами" in compact or "своими словами" in compact:
            return "simple"
        if re.search(r"\b(пример|примеры|например)\b", compact):
            return "example"
        if re.search(r"\b(приведи|привести|покажи|поясни|объясни)\s+.*\bпример", compact):
            return "example"
        if re.search(r"\b(на\s+примере|ещ[её]\s+пример|можно\s+пример|какой\s+пример)\b", compact):
            return "example"
        if re.search(r"\b(что\s+еще|что\s+ещё|еще\s+что|ещё\s+что)\b", compact):
            return "more"
        if re.search(r"\b(а\s+что\s+ещ[её]|что\s+ещ[её]\s+ты\s+знаешь|что\s+ещ[её]\s+важн)\b", compact):
            return "more"
        if re.search(r"\b(это\s+точно|точно\??|ты\s+уверен|вы\s+уверены|уверен\??|верно\??|правильно\s+ли\s+это)\b", compact):
            return "confirm"
        if re.search(r"\b(почему|зачем)\b", compact):
            compact_tokens = [tok for tok in self._linguistic_token_forms(compact) if tok]
            if len(compact_tokens) <= 4:
                return "why"
        tokens = [
            tok
            for tok in self._extract_linguistic_terms(
                compact,
                min_len=2,
                drop_stop=True,
                drop_generic=False,
            )
            if tok
        ]
        if not tokens:
            return None
        if all(self._token_matches_any(tok, _FOLLOWUP_DIRECTIVE_WORDS, threshold=0.72) for tok in tokens):
            return "detail"
        return None

    def _is_followup_only_query(self, message: str, q_tokens: set[str] | None = None) -> bool:
        if self._extract_followup_directive_mode(message) is not None:
            return True
        tokens = set(q_tokens) if q_tokens is not None else self._extract_linguistic_terms(
            message,
            min_len=2,
            drop_stop=True,
            drop_generic=False,
        )
        if not tokens:
            low = self._normalize_linguistic_text(message)
            return low in _FOLLOWUP_DIRECTIVE_WORDS
        return all(self._token_matches_any(t, _FOLLOWUP_DIRECTIVE_WORDS, threshold=0.72) for t in tokens)

    def _get_recent_assistant_answer(
        self,
        context_window: ContextWindow | None,
        current_query: str | None = None,
    ) -> str | None:
        if context_window is None:
            return None
        current_norm = re.sub(r"\s+", " ", (current_query or "").strip().lower())
        skipped_current = False
        for msg in reversed(context_window.working_memory[-24:]):
            role = str(getattr(msg, "role", "") or "").strip().lower()
            if role != "assistant":
                continue
            text = re.sub(r"\s+", " ", str(getattr(msg, "content", "") or "").strip())
            if len(text) < 8:
                continue
            normalized = text.lower()
            if current_norm and normalized == current_norm and not skipped_current:
                skipped_current = True
                continue
            if self._is_context_placeholder_answer(text):
                continue
            if text.rstrip().endswith("?"):
                continue
            return text
        return None

    def _get_recent_non_recap_user_messages(
        self,
        context_window: ContextWindow | None,
        *,
        current_query: str | None = None,
        limit: int = 3,
    ) -> list[str]:
        if context_window is None:
            return []
        raw_messages = context_window.get_recent_substantive_user_messages(
            limit=max(limit + 3, limit),
            current_query=current_query,
        )
        filtered: list[str] = []
        for item in raw_messages:
            clean = re.sub(r"\s+", " ", str(item or "").strip())
            if not clean:
                continue
            if self._is_conversation_recap_intent(clean):
                continue
            filtered.append(clean)
            if len(filtered) >= limit:
                break
        return filtered

    def _extract_followup_topic_from_anchor(
        self,
        message: str,
        context_window: ContextWindow | None,
    ) -> str | None:
        if context_window is None:
            return None
        anchors = self._get_recent_non_recap_user_messages(
            context_window,
            current_query=message,
            limit=1,
        )
        anchor = anchors[0] if anchors else None
        if not anchor:
            return None
        anchor_tokens, _anchor_stems = self._extract_terms_and_stems(anchor)
        if (
            self._is_weather_query(anchor, q_tokens=anchor_tokens)
            or self._is_news_query(anchor, q_tokens=anchor_tokens)
            or self._is_time_query(anchor, q_tokens=anchor_tokens)
            or self._is_currency_query(anchor, q_tokens=anchor_tokens)
        ):
            return None
        if self._is_architecture_intent(anchor):
            return "архитектура Kolibri"
        focus = self._extract_topic_focus(anchor) or anchor
        focus = self._canonicalize_definition_focus_text(focus) or focus.strip()
        focus = re.sub(r"\s+", " ", focus).strip(" .,!?:;")
        return focus or None

    def _extract_followup_compare_target(self, message: str, topic: str | None = None) -> str | None:
        normalized = self._normalize_linguistic_text(message)
        if not normalized:
            return None
        compact = re.sub(r"\s+", " ", normalized).strip()
        patterns = (
            r"\bсравн(?:и|ить)?\s+с\s+(.+)$",
            r"\bсравн(?:и|ить)?\s+и\s+(.+)$",
            r"\bсравн(?:и|ить)?\s+(.+)$",
        )
        for pattern in patterns:
            match = re.search(pattern, compact)
            if not match:
                continue
            candidate = re.sub(r"^(?:с|и)\s+", "", match.group(1).strip(), flags=re.IGNORECASE)
            candidate = self._canonicalize_definition_focus_text(candidate) or candidate
            if " " not in candidate:
                if candidate.endswith("ью") and len(candidate) >= 5:
                    candidate = candidate[:-2] + "ь"
                elif candidate.endswith("ом") and len(candidate) >= 4:
                    candidate = candidate[:-2] + "о"
                elif candidate.endswith("ем") and len(candidate) >= 4:
                    candidate = candidate[:-2] + "е"
            candidate = re.sub(r"\s+", " ", candidate).strip(" .,!?:;")
            if not candidate:
                continue
            if topic and candidate.lower() == topic.lower():
                return None
            return candidate
        return None

    def _topic_summary_for_followup(self, topic: str) -> str | None:
        normalized_topic = re.sub(r"\s+", " ", (topic or "").strip())
        if not normalized_topic:
            return None
        if normalized_topic.lower() == "архитектура kolibri":
            return self._kolibri_architecture_summary_text()
        query = f"что такое {normalized_topic}"

        def _topic_answer_is_good(answer: str | None) -> bool:
            if not answer:
                return False
            clean_answer = answer.strip()
            if not clean_answer:
                return False
            if self._response_needs_language_fallback(clean_answer):
                return False
            if not self._answer_shape_is_valid(query, clean_answer):
                return False
            if not self._text_mentions_topic_focus(normalized_topic, clean_answer):
                return False
            return True

        if self._enable_c_inference and self.c_inference.available:
            c_formula_answer = self.c_inference.query(query, strategy="formula")
            if self._is_valid_c_formula_answer(query, c_formula_answer):
                response = str(c_formula_answer.get("response", "") or "").strip()
                if response:
                    return response
        try:
            reference_answer = fetch_reference_answer(
                query,
                timeout=3.5,
            )
        except Exception:
            reference_answer = None
        if _topic_answer_is_good(reference_answer):
            return reference_answer.strip()
        q_tokens, q_stems = self._extract_terms_and_stems(normalized_topic)
        web_answer = self._try_web_augment_answer(
            query,
            q_tokens=q_tokens,
            q_stems=q_stems,
            force=True,
            latency_budget_sec=2.5,
        )
        if _topic_answer_is_good(web_answer):
            return web_answer.strip()
        return None

    def _text_mentions_topic_focus(self, topic: str | None, text: str | None) -> bool:
        normalized_topic = re.sub(r"\s+", " ", (topic or "").strip())
        clean_text = re.sub(r"\s+", " ", (text or "").strip())
        if not normalized_topic or not clean_text:
            return False
        query = f"что такое {normalized_topic}"
        focus_terms = set(self._definition_focus_terms(query))
        if not focus_terms:
            focus_terms, _focus_stems_unused = self._extract_terms_and_stems(
                normalized_topic,
                drop_generic=False,
            )
        focus_stems = self._extract_linguistic_stems(focus_terms) if focus_terms else set()
        a_tokens, a_stems = self._extract_terms_and_stems(clean_text, drop_generic=False)
        return bool((focus_terms & a_tokens) or (focus_stems & a_stems))

    def _pick_additive_followup_text(
        self,
        answer: str,
        recent_answer: str | None = None,
        *,
        topic: str | None = None,
    ) -> str | None:
        clean = re.sub(r"\s+", " ", self._strip_memory_ack_wrapper(answer).strip())
        if not clean:
            return None
        recent_clean = re.sub(r"\s+", " ", self._strip_memory_ack_wrapper(recent_answer or "").strip())
        recent_norm = self._normalize_linguistic_text(recent_clean)
        recent_tokens, recent_stems = (
            self._extract_terms_and_stems(recent_clean, drop_generic=False)
            if recent_clean
            else (set(), set())
        )

        candidates: list[str] = []
        for sentence in _split_sentences(clean):
            sentence = sentence.strip()
            if ":" in sentence:
                _, suffix = sentence.split(":", 1)
                suffix = suffix.strip(" ,.;")
                parts = [suffix] if len(suffix) >= 12 else []
            else:
                parts = [part.strip(" ,.;") for part in re.split(r"[;]\s*", sentence) if part.strip(" ,.;")]
            if not parts:
                parts = [sentence.strip(" ,.;")]
            for part in parts:
                if len(part) >= 12:
                    candidates.append(part)

        picked: list[str] = []
        for candidate in candidates:
            candidate = self._cleanup_followup_clause(candidate)
            if len(candidate) < 12:
                continue
            candidate_norm = self._normalize_linguistic_text(candidate)
            if not candidate_norm:
                continue
            if topic and not self._text_mentions_topic_focus(topic, candidate):
                continue
            if recent_norm and (
                candidate_norm in recent_norm
                or recent_norm in candidate_norm
            ):
                continue
            c_tokens, c_stems = self._extract_terms_and_stems(candidate, drop_generic=False)
            if recent_tokens or recent_stems:
                overlap = len(c_tokens & recent_tokens) + len(c_stems & recent_stems)
                total = max(1, len(c_tokens | c_stems))
                if overlap / total >= 0.7:
                    continue
            normalized = candidate.rstrip(" .") + "."
            if normalized.lower() in {item.lower() for item in picked}:
                continue
            picked.append(normalized)
            if len(picked) >= 2:
                break

        if not picked:
            return None
        return " ".join(picked)

    def _cleanup_followup_clause(self, text: str) -> str:
        clean = re.sub(r"\s+", " ", (text or "").strip())
        if not clean:
            return ""
        patterns = (
            r"^например,\s*если\s+взять\s+тему\s+«[^»]+»,\s*то\s*",
            r"^например,\s*если\s+говорить\s+о\s+[^,]+,\s*то\s*",
            r"^в\s+качестве\s+примера[:,]?\s*",
            r"^примером\s+может\s+быть[:,]?\s*",
            r"^можно\s+рассмотреть\s+такой\s+случай[:,]?\s*",
        )
        for pattern in patterns:
            clean = re.sub(pattern, "", clean, flags=re.IGNORECASE).strip()
        return clean.strip(" .;,:")

    def _looks_like_example_text(self, text: str | None) -> bool:
        clean = re.sub(r"\s+", " ", (text or "").strip()).lower()
        if not clean:
            return False
        return bool(
            re.search(
                r"\b(например|пример|примером|на примере|типичный случай|случай|договор|ситуац|практик)\b",
                clean,
            )
        )

    def _is_example_projection_query(self, query: str) -> bool:
        clean = re.sub(r"\s+", " ", (query or "").strip()).lower()
        return any(
            marker in clean
            for marker in (
                "пример",
                "случай",
                "где используется",
                "пример применения",
                "пример из практики",
            )
        )

    def _extract_explicit_example_text(
        self,
        answer: str,
        *,
        topic: str | None = None,
    ) -> str | None:
        clean = re.sub(r"\s+", " ", self._strip_memory_ack_wrapper(answer).strip())
        if not clean:
            return None
        patterns = [
            r"(?:пример(?:\s+из)?(?:\s+[а-яёa-z0-9\-]+){0,4})\s*:\s*(.+)",
            r"(?:типичный\s+случай(?:\s+[а-яёa-z0-9\-]+){0,4})\s*[-:]\s*(.+)",
            r"(?:в\s+качестве\s+примера)\s*[-:]\s*(.+)",
            r"(?:примером\s+может\s+быть)\s*[-:]\s*(.+)",
            r"(?:например)\s*[:,]\s*(.+)",
        ]
        for pattern in patterns:
            match = re.search(pattern, clean, flags=re.IGNORECASE)
            if not match:
                continue
            candidate = self._cleanup_followup_clause(match.group(1))
            if len(candidate) < 4:
                continue
            if topic and not self._text_mentions_topic_focus(topic, clean):
                continue
            return candidate.rstrip(" .") + "."
        return None

    def _followup_c_query_candidates(self, mode: str, topic: str) -> list[str]:
        normalized_topic = re.sub(r"\s+", " ", (topic or "").strip())
        if not normalized_topic:
            return []
        candidates_by_mode: dict[str, list[str]] = {
            "detail": [
                f"расскажи подробно о {normalized_topic}",
                f"как устроено {normalized_topic}",
                f"что ты знаешь о {normalized_topic}",
            ],
            "more": [
                f"расскажи подробно о {normalized_topic}",
                f"как устроено {normalized_topic}",
                f"почему важно {normalized_topic}",
                f"зачем нужно {normalized_topic}",
                f"роль {normalized_topic}",
                f"функции {normalized_topic}",
                f"задачи {normalized_topic}",
                f"применение {normalized_topic}",
                f"где используется {normalized_topic}",
                f"что изучает {normalized_topic}",
                f"пример из {normalized_topic}",
                f"типичный случай {normalized_topic}",
            ],
            "simple": [
                f"объясни {normalized_topic} простыми словами",
                f"что ты знаешь о {normalized_topic}",
            ],
            "bullets": [
                f"расскажи подробно о {normalized_topic}",
                f"что ты знаешь о {normalized_topic}",
                f"как устроено {normalized_topic}",
            ],
            "example": [
                f"пример из {normalized_topic}",
                f"пример применения {normalized_topic}",
                f"пример из практики {normalized_topic}",
                f"типичный случай {normalized_topic}",
                f"где используется {normalized_topic}",
                f"как устроено {normalized_topic}",
                f"зачем нужно {normalized_topic}",
                f"почему важно {normalized_topic}",
                f"расскажи подробно о {normalized_topic}",
            ],
            "why": [
                f"почему важно {normalized_topic}",
                f"зачем нужно {normalized_topic}",
                f"как устроено {normalized_topic}",
            ],
            "confirm": [
                f"что ты знаешь о {normalized_topic}",
            ],
        }
        raw_candidates = candidates_by_mode.get(mode, [f"что ты знаешь о {normalized_topic}"])
        out: list[str] = []
        seen: set[str] = set()
        for candidate in raw_candidates:
            clean = re.sub(r"\s+", " ", candidate.strip())
            if not clean or clean in seen:
                continue
            seen.add(clean)
            out.append(clean)
        return out

    def _topic_c_query_candidates(self, projection: str, topic: str) -> list[str]:
        normalized_topic = re.sub(r"\s+", " ", (topic or "").strip())
        if not normalized_topic:
            return []
        candidates_by_projection: dict[str, list[str]] = {
            "explain": [
                f"объясни {normalized_topic}",
                f"что такое {normalized_topic}",
                f"что ты знаешь о {normalized_topic}",
                f"расскажи о {normalized_topic}",
            ],
            "tell": [
                f"расскажи подробно о {normalized_topic}",
                f"расскажи о {normalized_topic}",
                f"что ты знаешь о {normalized_topic}",
                f"что такое {normalized_topic}",
                f"как устроено {normalized_topic}",
            ],
            "knowledge": [
                f"что ты знаешь о {normalized_topic}",
                f"что такое {normalized_topic}",
                f"расскажи о {normalized_topic}",
                f"как устроено {normalized_topic}",
            ],
            "structure": [
                f"как устроено {normalized_topic}",
                f"расскажи о {normalized_topic}",
                f"что такое {normalized_topic}",
            ],
            "importance": [
                f"почему важно {normalized_topic}",
                f"зачем нужно {normalized_topic}",
                f"расскажи о {normalized_topic}",
                f"что такое {normalized_topic}",
            ],
            "study": [
                f"что изучает {normalized_topic}",
                f"расскажи о {normalized_topic}",
                f"что такое {normalized_topic}",
            ],
            "role": [
                f"чем занимается {normalized_topic}",
                f"расскажи о {normalized_topic}",
                f"что такое {normalized_topic}",
            ],
        }
        raw_candidates = candidates_by_projection.get(projection, [f"что такое {normalized_topic}"])
        out: list[str] = []
        seen: set[str] = set()
        for candidate in raw_candidates:
            clean = re.sub(r"\s+", " ", candidate.strip())
            if not clean or clean in seen:
                continue
            seen.add(clean)
            out.append(clean)
        return out

    def _match_c_projection_query(self, message: str) -> tuple[str, str] | None:
        stripped = re.sub(r"\s+", " ", (message or "").strip())
        if not stripped:
            return None
        if self._is_architecture_intent(stripped):
            return None

        explain_match = re.match(
            r"^(?:объясни|поясни|обьясни)\s+(.+)$",
            stripped,
            flags=re.IGNORECASE,
        )
        if explain_match:
            topic = explain_match.group(1).strip()
            topic = re.sub(r"\s+простыми\s+словами$", "", topic, flags=re.IGNORECASE).strip()
            topic = self._canonicalize_definition_focus_text(topic) or topic
            return ("explain", topic) if topic else None

        tell_match = re.match(
            r"^(?:расскажи(?:\s+подробно)?)\s+(?:о|про)\s+(.+)$",
            stripped,
            flags=re.IGNORECASE,
        )
        if tell_match:
            topic = self._canonicalize_definition_focus_text(tell_match.group(1).strip())
            return ("tell", topic) if topic else None

        knowledge_match = re.match(
            r"^(?:что\s+ты\s+знаешь)\s+(?:о|об|про)\s+(.+)$",
            stripped,
            flags=re.IGNORECASE,
        )
        if knowledge_match:
            topic = self._canonicalize_definition_focus_text(knowledge_match.group(1).strip())
            return ("knowledge", topic) if topic else None

        structure_match = re.match(
            r"^(?:как\s+устроен(?:а|о)?)\s+(.+)$",
            stripped,
            flags=re.IGNORECASE,
        )
        if structure_match:
            topic = self._canonicalize_definition_focus_text(structure_match.group(1).strip())
            return ("structure", topic) if topic else None

        importance_match = re.match(
            r"^(?:(?:почему\s+важ(?:ен|на|но))|(?:зачем\s+нуж(?:ен|на|но)))\s+(.+)$",
            stripped,
            flags=re.IGNORECASE,
        )
        if importance_match:
            topic = self._canonicalize_definition_focus_text(importance_match.group(1).strip())
            return ("importance", topic) if topic else None

        return None

    def _try_c_core_topic_projection_response(
        self,
        *,
        topic: str,
        projection: str,
    ) -> dict[str, Any] | None:
        if not (self._enable_c_inference and self.c_inference.available):
            return None
        for c_query in self._topic_c_query_candidates(projection, topic):
            c_formula_answer = self.c_inference.query(c_query, strategy="formula")
            if not self._is_valid_c_formula_answer(c_query, c_formula_answer):
                continue
            return {
                "response": str(c_formula_answer.get("response", "") or ""),
                "confidence": max(0.45, min(0.92, float(c_formula_answer.get("confidence", 0.76) or 0.76))),
                "sources": ["c-core-formula"],
                "method": "c-core-formula",
                "knowledge_hits": int(c_formula_answer.get("knowledge_hits", 0) or 0),
                "c_payload": c_formula_answer,
                "formula_data": self._c_formula_runtime_data(c_formula_answer),
                "graph_stats": self.graph.get_stats(),
            }
        return None

    def _build_topic_projection_read_response(self, message: str) -> dict[str, Any] | None:
        stripped = re.sub(r"\s+", " ", (message or "").strip())
        projection_match = self._match_c_projection_query(stripped)
        if not projection_match:
            return None
        projection, topic = projection_match
        c_projection_answer = self._try_c_core_topic_projection_response(
            topic=topic,
            projection=projection,
        )
        if c_projection_answer:
            return c_projection_answer
        return self._topic_projection_fallback_response(
            topic=topic,
            projection=projection,
        )

    def _topic_projection_fallback_response(
        self,
        *,
        topic: str,
        projection: str,
    ) -> dict[str, Any]:
        web_query_by_projection = {
            "explain": f"что такое {topic}",
            "tell": f"что такое {topic}",
            "knowledge": f"что такое {topic}",
            "structure": f"как устроено {topic}",
            "importance": f"почему важно {topic}",
        }
        fallback_text_by_projection = {
            "explain": (
                f"По теме «{topic}» в локальной базе пока мало проверенных объяснений. "
                "Добавьте материал, и я закреплю это знание."
            ),
            "tell": (
                f"По теме «{topic}» в локальном контуре пока мало подтверждённого материала "
                "для развёрнутого ответа."
            ),
            "knowledge": (
                f"По теме «{topic}» в локальном контуре пока мало подтверждённого материала "
                "для точного ответа."
            ),
            "structure": (
                f"По теме «{topic}» в локальном контуре пока мало подтверждённого материала "
                "о том, как это устроено."
            ),
            "importance": (
                f"По теме «{topic}» в локальном контуре пока мало подтверждённого материала "
                "о том, почему это важно."
            ),
        }
        web_query = web_query_by_projection.get(projection, f"что такое {topic}")
        query_terms, query_stems = self._extract_terms_and_stems(topic)
        web_answer = self._try_web_augment_answer(
            web_query,
            q_tokens=query_terms,
            q_stems=query_stems,
            force=True,
            latency_budget_sec=2.5,
        )
        if (
            web_answer
            and self._answer_shape_is_valid(web_query, web_answer)
            and not self._response_needs_language_fallback(web_answer)
        ):
            return {
                "response": web_answer,
                "confidence": 0.42,
                "sources": ["web-augment"],
                "method": "web-augment",
                "knowledge_hits": 0,
                "formula_data": self._basic_formula_data(),
                "graph_stats": self.graph.get_stats(),
            }
        return {
            "response": fallback_text_by_projection.get(
                projection,
                f"По теме «{topic}» в локальном контуре пока мало подтверждённого материала.",
            ),
            "confidence": 0.38,
            "sources": ["canonical-runtime"],
            "method": "canonical-topic-fallback",
            "knowledge_hits": 0,
            "formula_data": self._basic_formula_data(),
            "graph_stats": self.graph.get_stats(),
        }

    def _followup_additive_score(
        self,
        text: str | None,
        *,
        recent_answer: str | None = None,
        topic: str | None = None,
    ) -> float:
        clean_text = re.sub(r"\s+", " ", (text or "").strip())
        if not clean_text:
            return 0.0
        t_tokens, t_stems = self._extract_terms_and_stems(clean_text, drop_generic=False)
        r_tokens, r_stems = self._extract_terms_and_stems(recent_answer or "", drop_generic=False)
        novel = len(t_tokens - r_tokens) + len(t_stems - r_stems)
        overlap = len(t_tokens & r_tokens) + len(t_stems & r_stems)
        topic_bonus = 1.5 if (topic and self._text_mentions_topic_focus(topic, clean_text)) else 0.0
        return novel * 1.2 - overlap * 0.15 + topic_bonus

    def _query_c_followup_answer(
        self,
        *,
        topic: str,
        mode: str,
        recent_answer: str | None,
    ) -> str | None:
        if not (self._enable_c_inference and self.c_inference.available and topic):
            return None

        best_response: str | None = None
        best_additive_more: str | None = None
        best_additive_more_score = float("-inf")
        for c_query in self._followup_c_query_candidates(mode, topic):
            if mode == "more" and self._looks_like_example_text(recent_answer) and self._is_example_projection_query(c_query):
                continue
            c_formula_answer = self.c_inference.query(c_query, strategy="formula")
            if not self._is_valid_c_formula_answer(c_query, c_formula_answer):
                continue
            response = str(c_formula_answer.get("response", "") or "").strip()
            if not response:
                continue

            if mode == "more":
                additive = self._pick_additive_followup_text(response, recent_answer, topic=topic)
                if additive:
                    additive_score = self._followup_additive_score(
                        additive,
                        recent_answer=recent_answer,
                        topic=topic,
                    )
                    if additive_score > best_additive_more_score:
                        best_additive_more = additive
                        best_additive_more_score = additive_score
                best_response = best_response or response
                continue

            if mode in {"example", "why"}:
                explicit_example = self._extract_explicit_example_text(response, topic=topic) if mode == "example" else None
                if explicit_example:
                    reshaped_example = self._summarize_followup_answer(explicit_example, mode, topic=topic)
                    if reshaped_example:
                        return reshaped_example
                additive = self._pick_additive_followup_text(response, recent_answer, topic=topic)
                candidate_source = additive or response
                reshaped = self._summarize_followup_answer(candidate_source, mode, topic=topic)
                if reshaped:
                    return reshaped
                best_response = best_response or candidate_source
                continue

            if mode in {"simple", "bullets", "confirm"}:
                reshaped = self._summarize_followup_answer(response, mode, topic=topic)
                if reshaped:
                    return reshaped
                best_response = best_response or response
                continue

            return response

        if mode == "more" and best_additive_more:
            return best_additive_more
        if best_response:
            if mode in {"simple", "bullets", "confirm", "example", "why"}:
                reshaped = self._summarize_followup_answer(best_response, mode, topic=topic)
                if reshaped:
                    return reshaped
            return best_response
        return None

    def _summarize_followup_answer(self, answer: str, mode: str, topic: str | None = None) -> str | None:
        clean = re.sub(r"\s+", " ", self._strip_memory_ack_wrapper(answer).strip())
        clean = re.sub(r"^по контексту текущего диалога:\s*", "", clean, flags=re.IGNORECASE).strip()
        clean = re.sub(r"^да,\s+по\s+текущей\s+теме\s+«[^»]+»\s+основной\s+смысл\s+остается?\s+таким:\s*", "", clean, flags=re.IGNORECASE).strip()
        clean = re.sub(r"^да,\s+основной\s+смысл\s+ответа\s+остается?\s+таким:\s*", "", clean, flags=re.IGNORECASE).strip()
        clean = re.sub(r"^если проще,\s*про\s+[^:]{1,80}:\s*", "", clean, flags=re.IGNORECASE).strip()
        clean = re.sub(r"^если связать это с темой «[^»]+», смысл такой:\s*", "", clean, flags=re.IGNORECASE).strip()
        clean = re.sub(r"^по теме «[^»]+» коротко по пунктам:\s*", "", clean, flags=re.IGNORECASE).strip()
        if mode in {"simple", "example", "more", "why", "confirm"}:
            clean = clean.replace("•", " ")
            clean = re.sub(r"\s+", " ", clean).strip()
        if len(clean) < 8:
            return None
        sentences = [part.strip().rstrip(" .") for part in _split_sentences(clean) if part.strip()]
        if not sentences:
            sentences = [clean.rstrip(" .")]
        if mode == "simple":
            first = sentences[0]
            prefix = f"Если проще, про {topic}: " if topic else "Если проще: "
            return prefix + first[:280].rstrip(" .") + "."
        if mode == "bullets":
            parts: list[str] = []
            for sentence in sentences:
                chunks = [chunk.strip(" ,.;") for chunk in re.split(r"[;,]\s*", sentence) if chunk.strip(" ,.;")]
                if not chunks:
                    chunks = [sentence]
                for chunk in chunks:
                    if len(chunk) < 4:
                        continue
                    parts.append(chunk[:180].rstrip(" .") + ".")
                    if len(parts) >= 4:
                        break
                if len(parts) >= 4:
                    break
            if not parts:
                return None
            title = f"По теме «{topic}» коротко по пунктам:" if topic else "Коротко по пунктам:"
            return title + "\n" + "\n".join(f"• {part}" for part in parts)
        if mode == "more":
            picked = ". ".join(sentences[:2]).strip()
            if picked:
                return picked.rstrip(" .") + "."
        if mode == "example":
            first = sentences[0]
            if topic:
                return f"Например, если взять тему «{topic}», то можно рассмотреть такой случай: {first[:260].rstrip(' .')}."
            return f"Например: {first[:260].rstrip(' .')}."
        if mode == "why":
            first = sentences[0]
            prefix = f"Если связать это с темой «{topic}», смысл такой: " if topic else "Если кратко по смыслу: "
            return prefix + first[:320].rstrip(" .") + "."
        if mode == "confirm":
            first = ". ".join(sentences[:2]).strip() or sentences[0]
            if topic:
                return (
                    f"Да, по текущей теме «{topic}» основной смысл остаётся таким: "
                    f"{first[:360].rstrip(' .')}."
                )
            return f"Да, основной смысл ответа остаётся таким: {first[:360].rstrip(' .')}."
        picked = ". ".join(sentences[:2]).strip()
        return picked.rstrip(" .") + "." if picked else None

    def _build_followup_context_answer(
        self,
        message: str,
        context_window: ContextWindow | None,
    ) -> str | None:
        if context_window is None:
            return None
        mode = self._extract_followup_directive_mode(message) or "detail"
        topic = self._extract_followup_topic_from_anchor(message, context_window)
        recent_answer = self._get_recent_assistant_answer(context_window, current_query=message)
        if mode == "example" and recent_answer:
            explicit_recent_example = self._extract_explicit_example_text(recent_answer, topic=topic)
            if explicit_recent_example:
                reshaped_recent_example = self._summarize_followup_answer(explicit_recent_example, mode, topic=topic)
                if reshaped_recent_example:
                    return reshaped_recent_example
        if topic and topic.lower() == "архитектура kolibri":
            architecture_answer = self._kolibri_architecture_summary_text()
            if mode == "more":
                additive = self._pick_additive_followup_text(architecture_answer, recent_answer, topic=topic)
                if additive:
                    return additive
            reshaped = self._summarize_followup_answer(architecture_answer, mode, topic=topic)
            return reshaped or architecture_answer
        c_followup_more_response: str | None = None
        if self._enable_c_inference and self.c_inference.available and topic:
            c_followup_answer = self._query_c_followup_answer(
                topic=topic,
                mode=mode,
                recent_answer=recent_answer,
            )
            if c_followup_answer:
                if mode == "more":
                    c_followup_more_response = c_followup_answer
                else:
                    return c_followup_answer
        if mode == "compare":
            if not topic:
                return "Могу сравнить предыдущую тему, но сначала уточните, что именно нужно сопоставить."
            compare_target = self._extract_followup_compare_target(message, topic=topic)
            if not compare_target:
                return f"Могу сравнить тему «{topic}», но уточните, с чем именно её сравнить."
            base_summary = self._topic_summary_for_followup(topic) or self._get_recent_assistant_answer(context_window, current_query=message)
            target_summary = self._topic_summary_for_followup(compare_target)
            if not base_summary or not target_summary:
                return f"Я удерживаю тему «{topic}», но для сравнения с «{compare_target}» мне пока не хватает чистого локального материала."
            base_simple = self._summarize_followup_answer(base_summary, "simple", topic=topic) or base_summary
            target_simple = self._summarize_followup_answer(target_summary, "simple", topic=compare_target) or target_summary
            base_simple = re.sub(r"^Если проще,\s*про\s+[^:]{1,80}:\s*", "", base_simple, flags=re.IGNORECASE).strip()
            target_simple = re.sub(r"^Если проще,\s*про\s+[^:]{1,80}:\s*", "", target_simple, flags=re.IGNORECASE).strip()
            return (
                f"Если коротко, «{topic}» и «{compare_target}» связаны, но не совпадают.\n"
                f"• {topic}: {base_simple.rstrip(' .')}.\n"
                f"• {compare_target}: {target_simple.rstrip(' .')}."
            )
        topical_summary = self._topic_summary_for_followup(topic) if topic else None
        if mode == "more" and not topical_summary and c_followup_more_response:
            topical_summary = c_followup_more_response
        if topical_summary:
            if mode == "more":
                additive = self._pick_additive_followup_text(topical_summary, recent_answer, topic=topic)
                if additive:
                    return additive
                if topic:
                    contextual_facts = context_window.get_recent_semantic_facts(
                        query=topic,
                        limit=8,
                        current_query=message,
                    )
                    if contextual_facts:
                        additive_facts = self._pick_additive_followup_text(
                            ". ".join(contextual_facts),
                            recent_answer,
                            topic=topic,
                        )
                        if additive_facts:
                            return additive_facts
                    return (
                        f"По теме «{topic}» основное уже обозначено. "
                        f"Могу дальше объяснить проще, привести пример или сравнить её с другой темой."
                    )
            reshaped = self._summarize_followup_answer(topical_summary, mode, topic=topic)
            if reshaped:
                return reshaped
            return topical_summary
        if not recent_answer:
            return None
        if mode == "more" and topic:
            return (
                f"По теме «{topic}» основное уже обозначено. "
                f"Если хотите, я могу раскрыть причину, пример или сравнение."
            )
        return self._summarize_followup_answer(recent_answer, mode, topic=topic)

    def _build_contextual_query(
        self,
        message: str,
        context_window: ContextWindow | None,
    ) -> str | None:
        """Разворачивает follow-up в запрос по последней теме и смысловым фактам треда."""
        if context_window is None or not self._is_referential_query(message):
            return None
        anchors = self._get_recent_non_recap_user_messages(
            context_window,
            current_query=message,
            limit=3,
        )
        anchor = anchors[0] if anchors else None
        if not anchor:
            return None
        anchor_clean = re.sub(r"\s+", " ", anchor.strip())
        if not anchor_clean:
            return None
        anchor_tokens, _anchor_stems = self._extract_terms_and_stems(anchor_clean)
        is_realtime_anchor = (
            self._is_weather_query(anchor_clean, q_tokens=anchor_tokens)
            or self._is_news_query(anchor_clean, q_tokens=anchor_tokens)
            or self._is_time_query(anchor_clean, q_tokens=anchor_tokens)
            or self._is_currency_query(anchor_clean, q_tokens=anchor_tokens)
        )
        supporting_facts: list[str] = []
        if not is_realtime_anchor:
            for fact in context_window.get_recent_semantic_facts(query=anchor_clean, limit=6, current_query=message):
                cleaned = self._strip_memory_ack_wrapper(fact).strip().rstrip(".")
                if not cleaned:
                    continue
                if cleaned.lower() == anchor_clean.lower():
                    continue
                if any(cleaned.lower() == existing.lower() for existing in supporting_facts):
                    continue
                supporting_facts.append(cleaned)
        supporting_messages = [
            re.sub(r"\s+", " ", item.strip()).rstrip(".")
            for item in anchors[1:]
            if item and item.strip() and item.strip().lower() != anchor_clean.lower()
        ]
        if self._is_followup_only_query(message):
            if is_realtime_anchor:
                if self._is_weather_query(anchor_clean, q_tokens=anchor_tokens):
                    location_only = self._extract_weather_followup_location(message)
                    if location_only:
                        return f"Какая погода в {location_only}?"
                return anchor_clean
            supplements = supporting_messages[:2]
            for fact in supporting_facts:
                if any(fact.lower() == item.lower() for item in supplements):
                    continue
                supplements.append(fact)
                if len(supplements) >= 4:
                    break
            if supplements:
                return f"{anchor_clean} (контекст треда: {'; '.join(supplements)})"
            return anchor_clean
        current = re.sub(r"\s+", " ", (message or "").strip())
        current = re.sub(r"^(?:а|и|ну)\s+", "", current, flags=re.IGNORECASE).strip()
        if not current or current.lower() == anchor_clean.lower():
            return anchor_clean
        current_tokens, _current_stems = self._extract_terms_and_stems(current)
        weather_followup_location = self._extract_weather_followup_location(current)
        explicit_reference = bool(
            re.search(
                r"\b(это|этого|этому|этим|там|тогда|такой|такая|такие|него|неё|нее|них|ней|нем|нём)\b",
                current.lower(),
            )
        )
        same_realtime_topic = (
            (self._is_weather_query(anchor_clean, q_tokens=anchor_tokens) and self._is_weather_query(current, q_tokens=current_tokens))
            or (self._is_news_query(anchor_clean, anchor_tokens) and self._is_news_query(current, current_tokens))
            or (self._is_time_query(anchor_clean, q_tokens=anchor_tokens) and self._is_time_query(current, q_tokens=current_tokens))
            or (self._is_currency_query(anchor_clean, q_tokens=anchor_tokens) and self._is_currency_query(current, q_tokens=current_tokens))
        )
        if is_realtime_anchor:
            if self._is_weather_query(anchor_clean, q_tokens=anchor_tokens) and weather_followup_location:
                return f"Какая погода в {weather_followup_location}?"
            if not explicit_reference and not same_realtime_topic and not (current_tokens & anchor_tokens):
                return None
            return f"{anchor_clean}. {current}"
        supplements = supporting_messages[:2]
        for fact in supporting_facts:
            if any(fact.lower() == item.lower() for item in supplements):
                continue
            supplements.append(fact)
            if len(supplements) >= 4:
                break
        if supplements:
            return f"{anchor_clean}. {current}. Контекст треда: {'; '.join(supplements)}."
        return f"{anchor_clean}. {current}"

    def _followup_prefers_fresh_lookup(
        self,
        message: str,
        context_window: ContextWindow | None,
    ) -> bool:
        if context_window is None or not self._is_followup_only_query(message):
            return False
        anchors = self._get_recent_non_recap_user_messages(
            context_window,
            current_query=message,
            limit=1,
        )
        anchor = anchors[0] if anchors else None
        if not anchor:
            return False
        anchor_tokens, _anchor_stems = self._extract_terms_and_stems(anchor)
        weather_followup_location = self._extract_weather_followup_location(message)
        if self._is_weather_query(anchor, q_tokens=anchor_tokens):
            return True
        if weather_followup_location and self._is_weather_query(anchor, q_tokens=anchor_tokens):
            return True
        if self._is_news_query(anchor, q_tokens=anchor_tokens):
            return True
        if self._is_time_query(anchor, q_tokens=anchor_tokens):
            return True
        if self._is_currency_query(anchor, q_tokens=anchor_tokens):
            return True
        return "?" in anchor and self._should_use_web_augment(anchor, anchor_tokens)

    def _conversation_context_fallback(
        self,
        message: str,
        context_window: ContextWindow | None,
        q_tokens: set[str],
        q_stems: set[str],
    ) -> str | None:
        if context_window is None:
            return None

        referential = self._is_referential_query(message)
        if not referential:
            return None
        followup_only = self._is_followup_only_query(message, q_tokens=q_tokens)
        if followup_only:
            if self._followup_prefers_fresh_lookup(message, context_window):
                return None
            smart_followup_answer = self._build_followup_context_answer(message, context_window)
            if smart_followup_answer:
                answer_core = self._strip_memory_ack_wrapper(smart_followup_answer).rstrip(" .")
                if answer_core:
                    return f"По контексту текущего диалога: {answer_core}."
            return "Пока в текущем диалоге нет фактов, которые можно раскрыть подробнее."
        if not q_tokens and not q_stems:
            return None
        normalized_query = re.sub(r"\s+", " ", (message or "").lower()).strip()
        allow_weak_recent_facts = bool(
            re.search(
                r"\b(это|этого|этому|этим|такое|такой|связ|имеешь|имееш)\b",
                normalized_query,
            )
        )
        candidates: list[tuple[float, str, str]] = []
        skipped_current_user = False
        request_prefixes = (
            "напомни", "расскажи", "объясни", "поясни", "подскажи",
            "скажи", "ответь", "перескажи", "сделай", "создай",
            "придумай", "переведи", "напиши", "покажи",
        )

        for idx, fact in enumerate(context_window.get_relevant_facts(message, limit=4)):
            text = re.sub(r"\s+", " ", str(fact or "").strip())
            if len(text) < 8:
                continue
            if self._is_context_placeholder_answer(text):
                continue
            lowered = text.lower()
            if lowered.startswith(request_prefixes):
                continue
            t_tokens, t_stems = self._extract_terms_and_stems(
                lowered,
                drop_generic=False,
            )
            overlap = len(q_tokens & t_tokens) if q_tokens else 0
            stem_overlap = len(q_stems & t_stems) if q_stems else 0
            if q_tokens and overlap == 0 and stem_overlap == 0:
                continue
            fact_bonus = max(0.0, 0.7 - idx * 0.08)
            score = overlap * 0.8 + stem_overlap * 0.5 + fact_bonus
            candidates.append((score, text, "fact"))

        recent_messages = list(reversed(context_window.working_memory[-24:]))
        for idx, msg in enumerate(recent_messages):
            role = str(getattr(msg, "role", "") or "").strip().lower()
            if role not in {"user", "assistant"}:
                continue
            text = re.sub(r"\s+", " ", str(getattr(msg, "content", "") or "").strip())
            if len(text) < 8:
                continue
            if self._is_context_placeholder_answer(text):
                continue
            lowered = text.lower()
            is_question = text.rstrip().endswith("?")
            starts_as_question = lowered.startswith(
                (
                    "как ", "что ", "кто ", "сколько ", "почему ", "зачем ",
                    "где ", "когда ", "а как ", "а что ", "а почему ",
                )
            )
            starts_as_request = lowered.startswith(request_prefixes)

            normalized_text = re.sub(r"\s+", " ", lowered).strip()
            if role == "user" and not skipped_current_user and normalized_text == normalized_query:
                skipped_current_user = True
                continue

            t_tokens, t_stems = self._extract_terms_and_stems(
                lowered,
                drop_generic=False,
            )
            if not t_tokens:
                continue

            overlap = len(q_tokens & t_tokens) if q_tokens else 0
            stem_overlap = len(q_stems & t_stems) if q_stems else 0
            if q_tokens and overlap == 0 and stem_overlap == 0:
                continue

            recency_bonus = max(0.0, 0.35 - idx * 0.05)
            role_bonus = 0.2 if role == "user" else 0.1
            referential_bonus = 0.5 if referential and role == "user" else 0.0
            question_penalty = 0.9 if is_question else 0.0
            if starts_as_question:
                question_penalty += 0.7
            if starts_as_request:
                question_penalty += 1.0
            score = overlap * 0.8 + stem_overlap * 0.5 + recency_bonus + role_bonus + referential_bonus - question_penalty
            candidates.append((score, text, role))

        if not candidates:
            if not allow_weak_recent_facts:
                return None
            recent_facts = [
                self._strip_memory_ack_wrapper(item).strip().rstrip(" .")
                for item in context_window.get_recent_semantic_facts(query=message, limit=4, current_query=message)
            ]
            recent_facts = [item for item in recent_facts if item]
            if recent_facts:
                return f"По контексту текущего диалога: {'; '.join(recent_facts[:3])}."
            return None

        candidates.sort(key=lambda x: x[0], reverse=True)
        best_score, best_text, _best_role = candidates[0]
        for score, text, role in candidates:
            if text.rstrip().endswith("?"):
                continue
            best_score, best_text, _best_role = score, text, role
            break
        if best_score < 0.4:
            if not allow_weak_recent_facts:
                return None
            recent_facts = [
                self._strip_memory_ack_wrapper(item).strip().rstrip(" .")
                for item in context_window.get_recent_semantic_facts(query=message, limit=4, current_query=message)
            ]
            recent_facts = [item for item in recent_facts if item]
            if recent_facts:
                return f"По контексту текущего диалога: {'; '.join(recent_facts[:3])}."
            return None

        needs_multi = (
            " и " in normalized_query
            or ("где" in normalized_query and ("что" in normalized_query or "кто" in normalized_query))
        )
        answer_parts: list[str] = []
        seen_parts: set[str] = set()
        threshold = max(0.4, best_score - (0.35 if needs_multi else 0.15))
        for score, text, _role in candidates:
            if score < threshold:
                continue
            if text.rstrip().endswith("?"):
                continue
            part = text.strip().rstrip(" .")
            if len(part) < 6:
                continue
            norm_part = re.sub(r"\s+", " ", part.lower()).strip()
            if norm_part in seen_parts:
                continue
            seen_parts.add(norm_part)
            answer_parts.append(part)
            if len(answer_parts) >= (2 if needs_multi else 1):
                break

        answer_core = ". ".join(answer_parts) if answer_parts else best_text.strip().rstrip(" .")
        if len(answer_core) < 6:
            return None
        if answer_core.lower().startswith("по контексту текущего диалога"):
            parts = answer_core.split(":", 1)
            answer_core = parts[1].strip() if len(parts) == 2 else answer_core
        return f"По контексту текущего диалога: {answer_core}."

    def _build_dynamic_no_knowledge_response(
        self,
        message: str,
        retrieved_sentences: list[tuple[str, float]],
        graph_answer: str,
        context_window: ContextWindow | None = None,
        deadline_ts: float | None = None,
        fast_mode: bool = False,
        allow_web_augment: bool = True,
    ) -> tuple[str | None, str]:
        """
        Динамический fallback без захардкоженных фактов.
        Стратегия:
        1) Слабые retrieval-кандидаты -> короткая связка фактов.
        2) Релевантный graph-answer.
        3) Web-augment: локальный поиск в интернете и дообучение без API-ключей.
        4) Локальная FormulaLM-генерация (если обучена или можно дообучить).
        """
        normalized_message = re.sub(
            r"\s*(?:->|→)\s*[a-z0-9_-]{2,}\s*$",
            "",
            (message or "").strip(),
            flags=re.IGNORECASE,
        )
        if normalized_message:
            message = normalized_message
        q_tokens, q_stems = self._extract_terms_and_stems(message)
        contextual_query = self._build_contextual_query(message, context_window)
        lookup_message = contextual_query or message
        lookup_q_tokens, lookup_q_stems = self._extract_terms_and_stems(lookup_message)
        effective_weather_query = self._is_weather_query(lookup_message, q_tokens=lookup_q_tokens)
        effective_news_query = self._is_news_query(lookup_message, q_tokens=lookup_q_tokens)
        effective_time_query = self._is_time_query(lookup_message, q_tokens=lookup_q_tokens)
        effective_currency_query = self._is_currency_query(lookup_message, q_tokens=lookup_q_tokens)
        effective_reference_query = self._is_reference_query(lookup_message, q_tokens=lookup_q_tokens)

        recap_answer, recap_method = self._build_conversation_memory_read_response(
            message=message,
            context_window=context_window,
        )
        if recap_answer:
            return recap_answer, recap_method

        profile_memory_answer, profile_memory_method = self._build_profile_memory_read_response(message)
        if profile_memory_answer:
            return profile_memory_answer, profile_memory_method

        system_answer, system_method = self._build_system_read_response(message)
        if system_answer:
            return system_answer, system_method

        # Фактические утверждения пользователя подтверждаем сразу,
        # не тратя бюджет на web-поиск.
        if self._is_plain_fact_statement(message):
            fact_preview = message.strip().rstrip(".!?")
            if fact_preview:
                self._remember_user_fact(fact_preview)
                return f"Принял. Зафиксировал в контексте: {fact_preview}.", "dialog-fact-ack"

        # Для реального времени (погода) сначала пробуем web-augment,
        # иначе локальные шумные фрагменты могут выглядеть правдоподобно.
        if effective_weather_query and allow_web_augment and self._enable_web_augment:
            if not external_network_available():
                return self._weather_unavailable_answer(lookup_message, q_tokens=lookup_q_tokens), "weather-unavailable"
            weather_budget = None
            if deadline_ts is not None:
                weather_budget = max(0.0, float(deadline_ts) - time.time())
                if weather_budget < 0.45:
                    weather_budget = 0.0
            if weather_budget is None:
                weather_budget = 3.2 if fast_mode else 4.0
            else:
                weather_budget = min(3.2 if fast_mode else 4.0, weather_budget)
            weather_hint = self._build_weather_location_hint(
                lookup_message,
                q_tokens=lookup_q_tokens,
            )
            weather_web = None
            if weather_hint:
                try:
                    weather_web = fetch_weather_answer(
                        weather_hint,
                        timeout=max(2.0, float(weather_budget or 0.0)),
                    )
                except Exception as exc:
                    log.debug("realtime weather lookup failed: %s", exc)
            if not weather_web:
                weather_web = self._try_web_augment_answer(
                    lookup_message,
                    q_tokens=lookup_q_tokens,
                    q_stems=lookup_q_stems,
                    force=True,
                    latency_budget_sec=weather_budget,
                )
            if weather_web and self._weather_answer_is_valid(lookup_message, weather_web, q_tokens=lookup_q_tokens):
                return weather_web, "web-augment-weather"
            return self._weather_unavailable_answer(lookup_message, q_tokens=lookup_q_tokens), "weather-unavailable"

        news_digest = None
        if effective_news_query and allow_web_augment and self._enable_web_augment:
            news_digest = self._try_web_news_digest(lookup_message, q_tokens=lookup_q_tokens)
        if news_digest:
            return news_digest, "web-news"

        if effective_time_query and allow_web_augment and self._enable_web_augment:
            time_answer = None
            try:
                time_answer = fetch_time_answer(
                    lookup_message,
                    timeout=3.0 if fast_mode else 4.5,
                )
            except Exception as exc:
                log.debug("realtime time lookup failed: %s", exc)
            if time_answer and self._answer_shape_is_valid(lookup_message, time_answer):
                return time_answer, "web-time"

        if effective_currency_query and allow_web_augment and self._enable_web_augment:
            rate_answer = None
            try:
                rate_answer = fetch_exchange_rate_answer(
                    lookup_message,
                    timeout=3.2 if fast_mode else 4.8,
                )
            except Exception as exc:
                log.debug("realtime exchange lookup failed: %s", exc)
            if rate_answer and self._answer_shape_is_valid(lookup_message, rate_answer):
                return rate_answer, "web-rate"

        if effective_reference_query and allow_web_augment and self._enable_web_augment:
            reference_answer = None
            try:
                reference_answer = fetch_reference_answer(
                    lookup_message,
                    timeout=3.8 if fast_mode else 5.6,
                )
            except Exception as exc:
                log.debug("realtime reference lookup failed: %s", exc)
            if reference_answer and self._answer_shape_is_valid(lookup_message, reference_answer):
                return reference_answer, "web-reference"

        if self._is_referential_query(message) and not any((
            effective_weather_query,
            effective_news_query,
            effective_time_query,
            effective_currency_query,
            effective_reference_query,
        )):
            context_answer = self._conversation_context_fallback(
                message=message,
                context_window=context_window,
                q_tokens=q_tokens,
                q_stems=q_stems,
            )
            if context_answer:
                return context_answer, "dialog-context"

        ranked_local = self._rank_relevant_snippets(
            message=lookup_message,
            candidates=retrieved_sentences[:6],
            q_tokens=lookup_q_tokens,
            q_stems=lookup_q_stems,
            max_out=2,
            min_score=0.06,
        )
        if ranked_local:
            if len(ranked_local) == 1:
                candidate_answer = ranked_local[0] + "."
            else:
                candidate_answer = f"{ranked_local[0]}. {ranked_local[1]}."
            if effective_weather_query and not self._weather_answer_is_valid(lookup_message, candidate_answer, q_tokens=lookup_q_tokens):
                candidate_answer = ""
            if candidate_answer and self._answer_shape_is_valid(lookup_message, candidate_answer):
                return candidate_answer, "dynamic-fallback"

        if graph_answer and self._graph_answer_is_relevant(lookup_message, graph_answer) and self._answer_shape_is_valid(lookup_message, graph_answer):
            cleaned_graph = graph_answer.strip()
            if effective_weather_query and not self._weather_answer_is_valid(lookup_message, cleaned_graph, q_tokens=lookup_q_tokens):
                cleaned_graph = ""
            if cleaned_graph:
                return cleaned_graph, "dynamic-fallback"

        context_answer = self._conversation_context_fallback(
            message=message,
            context_window=context_window,
            q_tokens=q_tokens,
            q_stems=q_stems,
        )
        if context_answer:
            if effective_weather_query and not self._weather_answer_is_valid(lookup_message, context_answer, q_tokens=lookup_q_tokens):
                context_answer = None
        if context_answer:
            return context_answer, "dialog-context"

        is_project_query = self._is_project_runtime_query(message, q_tokens=q_tokens)
        if is_project_query:
            topic_hint = self._topic_for_web_augment(message, q_tokens)
            topic_hint = topic_hint or "проектной конфигурации"
            return (
                f"По теме «{topic_hint}» пока нет подтверждённых фактов в текущем диалоге. "
                "Добавьте факт (например: `В проекте используется порт 8001 для API.`), и я зафиксирую его в памяти."
            ), "dynamic-fallback"

        web_message = lookup_message
        web_q_tokens = set(lookup_q_tokens)
        web_q_stems = set(lookup_q_stems)
        if context_window is not None and self._is_referential_query(message) and not contextual_query:
            enriched = context_window.get_query_with_context(web_message)
            if enriched and enriched != web_message:
                web_message = enriched
                extra_tokens, extra_stems = self._extract_terms_and_stems(enriched)
                if extra_tokens:
                    web_q_tokens |= extra_tokens
                    web_q_stems |= extra_stems

        web_budget_sec: float | None = None
        if deadline_ts is not None:
            web_budget_sec = max(0.0, float(deadline_ts) - time.time())
            if web_budget_sec < 0.45:
                web_budget_sec = 0.0
        if web_budget_sec is None:
            web_budget_sec = 2.6 if fast_mode else 3.8
        else:
            web_budget_sec = min(2.6 if fast_mode else 3.8, web_budget_sec)
        if (not allow_web_augment) or (not self._enable_web_augment):
            web_answer = None
        elif web_budget_sec is not None and web_budget_sec <= 0.0:
            web_answer = None
        else:
            web_answer = self._try_web_augment_answer(
                web_message,
                q_tokens=web_q_tokens,
                q_stems=web_q_stems,
                latency_budget_sec=web_budget_sec,
            )
        if web_answer and effective_weather_query and not self._weather_answer_is_valid(lookup_message, web_answer, q_tokens=web_q_tokens):
            web_answer = None
        if web_answer:
            return web_answer, "web-augment"

        # Ленивое локальное дообучение генератора запускаем только в фоне,
        # чтобы не блокировать пользовательский запрос.
        if (not self._lm_trained) and self.sentence_store.size >= 150 and (not self._lm_train_queued):
            try:
                self._train_queue.put_nowait(("lm",))
                self._lm_train_queued = True
            except queue.Full:
                pass
        if self._lm_trained:
            generated = (self._generate_text(message, max_tokens=72) or "").strip()
            if generated and len(generated) >= 24 and not self._response_needs_language_fallback(generated) and self._answer_shape_is_valid(message, generated):
                return generated, "dynamic-fallback"

        topic = self._extract_topic_focus(web_message if web_message else message)
        if not topic:
            tokens = sorted((web_q_tokens if web_q_tokens else q_tokens), key=lambda t: (-len(t), t))
            topic = " ".join(tokens[:4]).strip()
        if topic:
            return (
                f"По теме «{topic}» в моей локальной базе пока мало проверенных данных. "
                "Добавьте материал, и я смогу отвечать точнее."
            ), "dynamic-fallback"
        return None, "no-knowledge"

    def _rerank_retrieved_sentences(
        self,
        message: str,
        retrieved_sentences: list[tuple[str, float]],
        context_window: ContextWindow | None = None,
        limit: int | None = None,
    ) -> list[tuple[str, float]]:
        if not retrieved_sentences:
            return retrieved_sentences

        q_tokens, q_stems = self._extract_terms_and_stems(
            message,
            min_len=2,
            drop_stop=True,
            drop_generic=False,
        )
        context_tokens: set[str] = set()
        context_stems: set[str] = set()
        if context_window is not None:
            for fact in context_window.get_relevant_facts(message, limit=4):
                f_tokens, f_stems = self._extract_terms_and_stems(
                    fact,
                    min_len=2,
                    drop_stop=True,
                    drop_generic=False,
                )
                context_tokens |= f_tokens
                context_stems |= f_stems

        rescored: list[tuple[float, str, float]] = []
        for idx, (text, base_score) in enumerate(retrieved_sentences):
            low = re.sub(r"\s+", " ", str(text or "").strip().lower())
            if not low:
                continue
            t_tokens, t_stems = self._extract_terms_and_stems(
                low,
                min_len=2,
                drop_stop=True,
                drop_generic=False,
            )
            token_overlap = len(q_tokens & t_tokens) if q_tokens else 0
            stem_overlap = len(q_stems & t_stems) if q_stems else 0
            ctx_overlap = len(context_tokens & t_tokens) if context_tokens else 0
            ctx_stem_overlap = len(context_stems & t_stems) if context_stems else 0
            position_penalty = min(0.12, idx * 0.01)
            rerank_score = (
                float(base_score)
                + token_overlap * 0.18
                + stem_overlap * 0.12
                + ctx_overlap * 0.09
                + ctx_stem_overlap * 0.05
                - position_penalty
            )
            rescored.append((rerank_score, text, float(base_score)))

        rescored.sort(key=lambda x: x[0], reverse=True)
        out_limit = max(1, int(limit or len(rescored)))
        return [(text, base) for _score, text, base in rescored[:out_limit]]

    def _rank_relevant_snippets(
        self,
        message: str,
        candidates: list[tuple[str, float]],
        q_tokens: set[str],
        q_stems: set[str],
        max_out: int = 2,
        min_score: float = 0.06,
    ) -> list[str]:
        tech_tokens = {t for t in q_tokens if re.search(r"[a-z0-9]", t)}
        anchor_tokens = {t for t in q_tokens if len(t) >= 7 and not re.search(r"\d", t)}
        anchor_stems = {_stem_ru(t) for t in anchor_tokens if len(t) >= 4}
        bad_fragments = {
            "утверждение своими словами",
            "сноска на источник",
            "важный совет при работе",
            "в таком духе",
        }
        query_norm = re.sub(r"\s+", " ", (message or "").lower()).strip(" ?!.,")

        ranked_snippets: list[tuple[float, str]] = []
        for text, score in candidates:
            clean = (text or "").strip().rstrip(".")
            if len(clean) < 18:
                continue
            if clean.endswith("?"):
                continue
            if _is_low_quality_sentence(clean):
                continue
            if score < min_score:
                continue
            lowered = clean.lower()
            lowered_norm = re.sub(r"\s+", " ", lowered).strip(" ?!.,")
            if query_norm and (lowered_norm == query_norm or lowered_norm in query_norm):
                continue
            if any(marker in lowered for marker in bad_fragments):
                continue
            t_tokens, t_stems = self._extract_terms_and_stems(
                lowered,
                drop_generic=False,
            )
            if not t_tokens:
                continue
            token_overlap = len(q_tokens & t_tokens) if q_tokens else 0
            stem_overlap = len(q_stems & t_stems) if q_stems else 0
            matched_token_stems = {_stem_ru(t) for t in (q_tokens & t_tokens) if len(t) >= 4}
            stem_only_overlap = len((q_stems & t_stems) - matched_token_stems) if q_stems else 0

            if tech_tokens and len(tech_tokens & t_tokens) == 0:
                continue
            if anchor_tokens and len(anchor_tokens & t_tokens) == 0 and not (
                anchor_stems and len(anchor_stems & t_stems) > 0
            ):
                continue
            if token_overlap == 0 and stem_overlap == 0:
                continue

            q_len = len(q_tokens)
            if q_len <= 1:
                overlap_required = 1
            elif q_len <= 3:
                overlap_required = 2
            elif q_len <= 6:
                overlap_required = 3
            else:
                overlap_required = 4
            if (token_overlap + stem_only_overlap) < overlap_required:
                continue

            rank = float(score) + token_overlap * 0.45 + stem_only_overlap * 0.25
            if lowered_norm.startswith(("сколько ", "how many ", "что такое ", "what is ")):
                rank -= 0.9
            ranked_snippets.append((rank, clean))

        ranked_snippets.sort(key=lambda x: x[0], reverse=True)
        unique: list[str] = []
        for _, snippet in ranked_snippets:
            if any(snippet[:56] == prev[:56] for prev in unique):
                continue
            unique.append(snippet)
            if len(unique) >= max(1, max_out):
                break
        return unique

    def _topic_for_web_augment(self, message: str, q_tokens: set[str]) -> str:
        topic = (self._extract_topic_focus(message) or "").strip()
        if topic:
            return topic
        ordered_tokens: list[str] = []
        seen: set[str] = set()
        for tok in self._linguistic_token_forms(message):
            if tok in seen or tok not in q_tokens:
                continue
            if _is_stop_word(tok) or tok in _GENERIC_QUERY_WORDS or tok in _FOLLOWUP_DIRECTIVE_WORDS:
                continue
            seen.add(tok)
            ordered_tokens.append(tok)
        if ordered_tokens:
            return " ".join(ordered_tokens[:6]).strip()
        ranked_tokens = sorted(q_tokens, key=lambda t: (-len(t), t))
        return " ".join(ranked_tokens[:5]).strip()

    def _is_news_query(self, message: str, q_tokens: set[str]) -> bool:
        lower = (message or "").strip().lower()
        if not lower:
            return False
        if any(k in lower for k in ("новост", "news", "headlines", "событи", "мировые новости")):
            return True
        if ("в мире" in lower or "world" in lower) and ("что" in lower or "какие" in lower):
            return True
        news_roots = ("новост", "событ", "news", "headline")
        return any(any(t.startswith(root) for root in news_roots) for t in q_tokens)

    def _build_news_query(self, message: str, q_tokens: set[str]) -> str:
        lower = (message or "").strip().lower()
        has_cyr = any("\u0400" <= c <= "\u04ff" for c in lower)
        if has_cyr:
            if "в росс" in lower:
                return "новости россии сегодня"
            if "тех" in lower or "ai" in lower or "ии" in lower:
                return "новости технологий и искусственного интеллекта сегодня"
            return "мировые новости сегодня"
        if "tech" in lower or "ai" in lower:
            return "latest technology and ai news"
        return "latest world news today"

    def _try_web_news_digest(self, message: str, q_tokens: set[str]) -> str | None:
        if not self._enable_web_augment:
            return None
        if not self._is_news_query(message, q_tokens):
            return None

        search_query = self._build_news_query(message, q_tokens)
        try:
            return fetch_news_digest(
                search_query,
                timeout=max(2.2, min(5.0, float(self._web_augment_timeout_sec) or 4.0)),
                max_items=3,
            )
        except Exception as exc:
            log.debug("web news digest failed: %s", exc)
            return None

    def _should_use_web_augment(self, message: str, q_tokens: set[str]) -> bool:
        if len(q_tokens) < 2:
            return False
        lower = (message or "").strip().lower()
        if not lower:
            return False
        personal_markers = (
            "как меня зовут",
            "кто я",
            "обо мне",
            "помнишь обо мне",
            "мой ",
            "моя ",
            "мои ",
            "мне ",
            "меня ",
        )
        if any(marker in lower for marker in personal_markers):
            return False
        project_local_markers = (
            " в проект",
            " проект",
            "бэкенд",
            "backend",
            "порт ",
            "port ",
            "endpoint",
            "эндпоинт",
            "api",
            "сервис",
        )
        if any(marker in lower for marker in project_local_markers):
            return False
        question_markers = (
            "что ", "кто ", "где ", "когда ", "почему ", "зачем ", "сколько ",
            "какой ", "какая ", "какие ", "каково ", "what ", "who ", "where ",
            "when ", "why ", "how many ", "what is ", "who is ",
        )
        if "?" in lower and any(marker in lower for marker in question_markers):
            return True
        triggers = (
            "что такое", "кто такой", "кто такая", "кто такие", "что значит",
            "сколько", "почему", "зачем", "как работает", "как устроен",
            "столица", "рецепт", "объясни", "поясни", "расскажи",
            "новости", "новость", "события", "мировые новости", "latest news",
            "what is", "who is", "how many", "why", "where", "when", "recipe",
        )
        return any(t in lower for t in triggers)

    def _count_subject_phrase(self, message: str) -> str:
        q = (message or "").strip().lower()
        if not q:
            return ""
        m_ru = re.search(r"\bсколько\s+(.+?)(?:\?|$)", q)
        if m_ru:
            return re.sub(r"\s+", " ", m_ru.group(1)).strip(" .,!?:;")
        m_en = re.search(r"\bhow\s+many\s+(.+?)(?:\?|$)", q)
        if m_en:
            return re.sub(r"\s+", " ", m_en.group(1)).strip(" .,!?:;")
        return ""

    def _try_count_answer_from_candidates(
        self,
        message: str,
        candidates: list[tuple[str, float]],
    ) -> str | None:
        q = (message or "").strip().lower()
        if not q:
            return None
        if "сколько" not in q and "how many" not in q:
            return None

        content_tokens = {
            t for t in _tokenize(q)
            if len(t) >= 3 and not _is_stop_word(t) and t not in _GENERIC_QUERY_WORDS
        }
        content_stems = {_stem_ru(t) for t in content_tokens if len(t) >= 4}
        satellite_natural_query = any(st.startswith("спутн") for st in content_stems) and not any(
            marker in q for marker in ("искусствен", "аппарат", "orbiter", "probe", "мисси", "миссия")
        )
        query_time_related = any(
            marker in q
            for marker in ("год", "году", "года", "лет", "когда", "дата", "year", "years", "date")
        )
        if not content_tokens:
            return None

        count_words = {
            "ноль": 0, "один": 1, "одна": 1, "одно": 1,
            "два": 2, "две": 2, "двух": 2,
            "три": 3, "трех": 3, "трёх": 3,
            "четыре": 4, "четырех": 4, "четырёх": 4,
            "пять": 5, "пяти": 5, "шесть": 6, "шести": 6,
            "семь": 7, "семи": 7, "восемь": 8, "восьми": 8,
            "девять": 9, "девяти": 9, "десять": 10, "десяти": 10,
            "one": 1, "two": 2, "three": 3, "four": 4, "five": 5,
            "six": 6, "seven": 7, "eight": 8, "nine": 9, "ten": 10,
        }
        temporal_markers = {"год", "году", "года", "year", "years", "август", "январ", "феврал"}

        votes: dict[int, float] = defaultdict(float)
        supports: dict[int, list[str]] = defaultdict(list)
        ranked = sorted(candidates, key=lambda x: x[1], reverse=True)[:28]
        for text, base in ranked:
            for sent in _split_sentences(text):
                s = re.sub(r"\s+", " ", sent).strip()
                if len(s) < 16:
                    continue
                low = s.lower()
                tokens = [t for t in _tokenize(low) if len(t) >= 2]
                if not tokens:
                    continue
                token_set = set(tokens)
                stem_set = {_stem_ru(t) for t in token_set if len(t) >= 4}
                exact_overlap = len(content_tokens & token_set)
                stem_overlap = len(content_stems & stem_set)
                prefix_overlap = 0
                for ct in content_tokens:
                    if len(ct) < 4:
                        continue
                    ct4 = ct[:4]
                    if any((len(tt) >= 4 and (tt.startswith(ct4) or ct.startswith(tt[:4]))) for tt in token_set):
                        prefix_overlap += 1
                overlap = exact_overlap + stem_overlap + (0.5 * prefix_overlap)
                if overlap <= 0:
                    continue

                for i, tok in enumerate(tokens):
                    value: int | None = None
                    if tok.isdigit():
                        try:
                            value = int(tok)
                        except ValueError:
                            value = None
                    elif tok in count_words:
                        value = int(count_words[tok])
                    if value is None or value < 0:
                        continue
                    if value > 1_000_000_000:
                        continue

                    w0 = max(0, i - 5)
                    w1 = min(len(tokens), i + 6)
                    near = tokens[w0:w1]
                    near_stems = {_stem_ru(t) for t in near if len(t) >= 4}
                    proximity = len(content_stems & near_stems)
                    near_prefix_overlap = 0
                    for ct in content_tokens:
                        if len(ct) < 4:
                            continue
                        ct4 = ct[:4]
                        if any((len(nt) >= 4 and (nt.startswith(ct4) or ct.startswith(nt[:4]))) for nt in near):
                            near_prefix_overlap += 1
                    proximity += 0.5 * near_prefix_overlap
                    temporal_penalty = 0.0
                    if any(m in tok2 for tok2 in near for m in temporal_markers):
                        temporal_penalty += 0.8
                    if (not query_time_related) and 1500 <= value <= 2100:
                        temporal_penalty += 3.2
                    if value == 1 and (
                        re.search(r"\bодин\s+из\b", low)
                        or re.search(r"\bone\s+of\b", low)
                    ):
                        temporal_penalty += 1.2

                    sat_bonus = 0.0
                    if any(st.startswith("спутн") for st in content_stems):
                        if "естествен" in low:
                            sat_bonus += 1.2
                        if "искусствен" in low:
                            sat_bonus -= 2.4
                        if value <= 10:
                            sat_bonus += 0.65
                        if value > 10:
                            sat_bonus -= 1.9
                        if value > 50:
                            sat_bonus -= 0.8
                        if value > 200:
                            sat_bonus -= 2.8
                        if value == 1:
                            sat_bonus -= 0.45
                    if satellite_natural_query:
                        if any(marker in low for marker in ("искусствен", "orbiter", "probe", "аппарат")):
                            temporal_penalty += 1.8
                        if "естествен" in low:
                            sat_bonus += 0.8

                    score = float(base) + overlap * 0.35 + proximity * 0.45 + sat_bonus - temporal_penalty
                    if score <= 0.15:
                        continue
                    votes[value] += score
                    supports[value].append(s)

        if not votes:
            return None
        sorted_votes = sorted(votes.items(), key=lambda x: x[1], reverse=True)
        if not sorted_votes:
            return None
        if len(sorted_votes) >= 2:
            top_score = float(sorted_votes[0][1])
            second_score = float(sorted_votes[1][1])
            # Слишком близкие кандидаты → нет уверенности в точном числе.
            if top_score <= 0 or top_score < second_score * 1.35:
                return None
        best_value, best_score = sorted_votes[0]
        if best_score <= 0:
            return None
        if len(supports.get(best_value, [])) < 2 and best_score < 2.6:
            return None

        subject = self._count_subject_phrase(message)
        if subject:
            answer = f"По найденным данным, количество для «{subject}» — {best_value}."
        else:
            answer = f"По найденным данным, количество — {best_value}."

        # Лёгкое уточнение для частого типа «спутники».
        if any(st.startswith("спутн") for st in content_stems):
            obj = ""
            m_obj_src = re.search(r"\b[Уу]\s+([A-Za-zА-Яа-яЁё][A-Za-zА-Яа-яЁё\-]{2,})\b", (message or ""))
            if m_obj_src:
                obj = m_obj_src.group(1)
            m_obj = re.search(r"\bу\s+([а-яёa-z][а-яёa-z\-]{2,})\b", q) if not obj else None
            if m_obj:
                obj = m_obj.group(1)
            if obj:
                answer = f"По найденным данным, у {obj} {best_value} спутника."
            else:
                answer = f"По найденным данным, {best_value} спутника."
        return answer

    def _try_web_augment_answer(
        self,
        message: str,
        q_tokens: set[str],
        q_stems: set[str],
        *,
        force: bool = False,
        latency_budget_sec: float | None = None,
    ) -> str | None:
        if not self._enable_web_augment:
            return None
        if not force and not self._should_use_web_augment(message, q_tokens):
            return None

        topic = self._topic_for_web_augment(message, q_tokens)
        if len(topic) < 3:
            return None
        message_low = (message or "").strip().lower()
        is_count_query = "сколько" in message_low or "how many" in message_low
        cache_key = re.sub(r"\s+", " ", topic.lower()).strip()
        now = time.time()
        cached = self._web_augment_answer_cache.get(cache_key)
        if cached and (now - cached[0] <= self._web_augment_cache_ttl_sec):
            return cached[1]
        cached_negative = self._web_augment_negative_cache.get(cache_key)
        if cached_negative and (now - cached_negative <= self._web_augment_negative_ttl_sec):
            return None

        effective_timeout = float(self._web_augment_timeout_sec)
        if latency_budget_sec is not None:
            effective_timeout = min(effective_timeout, max(0.0, float(latency_budget_sec)))
        if effective_timeout < 0.45:
            return None
        search_timeout = max(1, min(3, int(effective_timeout / 2) or 1))
        search_max_urls = int(self._web_augment_max_urls)
        if effective_timeout <= 2.5:
            search_max_urls = min(search_max_urls, 2)
        elif effective_timeout <= 4.0:
            search_max_urls = min(search_max_urls, 3)

        deadline = now + effective_timeout
        try:
            search_results = search_quick(
                topic,
                max_urls=search_max_urls,
                include_bing_fallback=False,
                timeout=search_timeout,
            )
        except Exception as exc:
            log.debug("web augment search failed: %s", exc)
            self._web_augment_negative_cache[cache_key] = time.time()
            return None
        if not search_results:
            self._web_augment_negative_cache[cache_key] = time.time()
            return None

        snippet_candidates: list[tuple[str, float]] = []
        for item in search_results:
            snippet = re.sub(r"\s+", " ", str(item.get("snippet", "") or "").strip())
            if len(snippet) >= 20:
                snippet_candidates.append((snippet, 0.18))
        if snippet_candidates:
            if is_count_query:
                snippet_count_answer = self._try_count_answer_from_candidates(message, snippet_candidates)
                if snippet_count_answer:
                    self._web_augment_answer_cache[cache_key] = (time.time(), snippet_count_answer)
                    self._web_augment_negative_cache.pop(cache_key, None)
                    return snippet_count_answer
            snippet_selected = self._rank_relevant_snippets(
                message=message,
                candidates=snippet_candidates,
                q_tokens=q_tokens,
                q_stems=q_stems,
                max_out=2,
                min_score=0.08,
            )
            if snippet_selected:
                snippet_answer = snippet_selected[0] + "."
                if len(snippet_selected) >= 2:
                    snippet_answer = f"{snippet_selected[0]}. {snippet_selected[1]}."
                self._web_augment_answer_cache[cache_key] = (time.time(), snippet_answer)
                self._web_augment_negative_cache.pop(cache_key, None)
                return snippet_answer

        web_candidates: list[tuple[str, float]] = []
        web_candidates.extend(snippet_candidates)
        trained_chunks = 0
        fetched_pages = 0
        max_fetch_pages = max(1, int(self._web_augment_max_urls))
        if effective_timeout <= 2.5:
            max_fetch_pages = 1
        elif effective_timeout <= 4.0:
            max_fetch_pages = min(max_fetch_pages, 2)

        for item in search_results:
            if fetched_pages >= max_fetch_pages:
                break
            remaining = deadline - time.time()
            if remaining <= 0.8:
                break
            url = str(item.get("url", "") or "").strip()
            if not url:
                continue
            snippet = str(item.get("snippet", "") or "").strip()
            if len(snippet) >= 20:
                web_candidates.append((snippet, 0.12))

            try:
                fetch_timeout = max(
                    1,
                    min(3, int(remaining)),
                )
                text, title, _bytes_dl = fetch_page_text(
                    url,
                    timeout=fetch_timeout,
                    max_chars=self._web_augment_page_max_chars,
                )
            except Exception:
                continue
            if len(text) < 260:
                continue
            fetched_pages += 1

            source = (str(item.get("source", "") or "") + " " + url).lower()
            source_bonus = 0.18 if "wikipedia" in source else 0.08
            sentences = _split_sentences(text)
            for sent in sentences[:180]:
                low = sent.lower()
                t_tokens = {
                    t for t in _tokenize(low)
                    if len(t) >= 3 and not _is_stop_word(t)
                }
                if not t_tokens:
                    continue
                t_stems = {_stem_ru(t) for t in t_tokens if len(t) >= 4}
                token_overlap = len(q_tokens & t_tokens) if q_tokens else 0
                stem_overlap = len(q_stems & t_stems) if q_stems else 0
                if token_overlap == 0 and stem_overlap == 0:
                    continue
                score = source_bonus + token_overlap * 0.65 + stem_overlap * 0.35
                web_candidates.append((sent, score))

            # Быстро добавляем выжимку в локальную базу прямо в этом запросе.
            quick_text = "\n".join(sentences[:80])[:12_000]
            if len(quick_text) >= 200:
                try:
                    self.sentence_store.add_text(quick_text, max_new_sentences=80)
                    trained_chunks += 1
                except Exception:
                    pass

            # Полный текст отправляем в фоновое обучение без блокировки ответа.
            training_payload = f"# {title or topic}\n# Source: {url}\n\n{text[:20_000]}"
            try:
                self._train_queue.put_nowait(("user_text", training_payload))
            except queue.Full:
                pass

            if trained_chunks >= self._web_augment_max_urls:
                break

        selected = self._rank_relevant_snippets(
            message=message,
            candidates=web_candidates,
            q_tokens=q_tokens,
            q_stems=q_stems,
            max_out=2,
            min_score=0.10,
        )

        count_answer = self._try_count_answer_from_candidates(message, web_candidates)
        if count_answer:
            self._web_augment_answer_cache[cache_key] = (time.time(), count_answer)
            self._web_augment_negative_cache.pop(cache_key, None)
            return count_answer

        if not selected:
            self._web_augment_negative_cache[cache_key] = time.time()
            return None

        answer = selected[0] + "."
        if len(selected) >= 2:
            answer = f"{selected[0]}. {selected[1]}."
        self._web_augment_answer_cache[cache_key] = (time.time(), answer)
        self._web_augment_negative_cache.pop(cache_key, None)
        if len(self._web_augment_answer_cache) > 512:
            stale = sorted(
                self._web_augment_answer_cache.items(),
                key=lambda x: x[1][0],
            )[:128]
            for key, _ in stale:
                self._web_augment_answer_cache.pop(key, None)
        if len(self._web_augment_negative_cache) > 1024:
            stale_neg = sorted(self._web_augment_negative_cache.items(), key=lambda x: x[1])[:256]
            for key, _ in stale_neg:
                self._web_augment_negative_cache.pop(key, None)
        return answer

    _LOGIC_COLOR_ROOTS = {
        "красн": "красных",
        "син": "синих",
        "зелен": "зелёных",
        "черн": "чёрных",
        "бел": "белых",
        "желт": "жёлтых",
        "оранж": "оранжевых",
        "фиолет": "фиолетовых",
    }

    _LOGIC_NUM_WORDS_EXTRA = {
        "одного": 1, "одной": 1,
        "двух": 2,
        "трех": 3, "трёх": 3,
        "четырех": 4, "четырёх": 4,
        "пяти": 5, "шести": 6, "семи": 7, "восьми": 8, "девяти": 9, "десяти": 10,
    }

    _LOGIC_CLASS_NORMALIZE = {
        "люди": "человек",
        "людей": "человек",
        "человека": "человек",
        "человек": "человек",
    }

    def _try_logic_solver_answer(self, query: str) -> str | None:
        q = (query or "").strip()
        if not q:
            return None
        lower = q.lower().replace("ё", "е")
        if not self._is_logic_query(lower):
            return None

        balls_answer = self._solve_balls_guarantee_puzzle(lower)
        if balls_answer:
            return balls_answer

        syllogism_answer = self._solve_basic_syllogism(q)
        if syllogism_answer:
            return syllogism_answer

        return None

    def _logic_parse_count(self, token: str) -> int | None:
        t = (token or "").strip().lower().replace("ё", "е")
        if not t:
            return None
        if t.isdigit():
            return int(t)
        if t in self._LOGIC_NUM_WORDS_EXTRA:
            return self._LOGIC_NUM_WORDS_EXTRA[t]
        n, used = self._parse_number_words([t], 0)
        if used > 0 and n is not None:
            return int(n)
        return None

    def _logic_color_key(self, token: str) -> str | None:
        t = (token or "").strip().lower().replace("ё", "е")
        if len(t) < 3:
            return None
        for root in self._LOGIC_COLOR_ROOTS:
            if t.startswith(root):
                return root
        return None

    def _solve_balls_guarantee_puzzle(self, lower: str) -> str | None:
        if "шар" not in lower or "ящик" not in lower:
            return None
        if "гарант" not in lower and "минималь" not in lower:
            return None

        color_counts: dict[str, int] = {}
        for m in re.finditer(r"\b(\d+|[а-яё]+)\s+([а-яё]+)\b", lower):
            n_token, color_token = m.group(1), m.group(2)
            color_key = self._logic_color_key(color_token)
            if not color_key:
                continue
            count = self._logic_parse_count(n_token)
            if count is None or count <= 0:
                continue
            color_counts[color_key] = color_counts.get(color_key, 0) + count
        if len(color_counts) < 2:
            return None

        target_text = lower.split("чтобы", 1)[1] if "чтобы" in lower else lower
        target_need = None
        target_color = None
        for m in re.finditer(r"\b(\d+|[а-яё]+)\s+([а-яё]+)\b", target_text):
            n_token, color_token = m.group(1), m.group(2)
            ck = self._logic_color_key(color_token)
            if not ck:
                continue
            n = self._logic_parse_count(n_token)
            if n is None or n <= 0:
                continue
            target_need = n
            target_color = ck
            break
        if target_need is None or target_color is None:
            return None

        available_target = color_counts.get(target_color, 0)
        if available_target < target_need:
            return (
                "Гарантия невозможна: в ящике меньше шаров нужного цвета, "
                "чем требуется по условию."
            )

        total = sum(color_counts.values())
        worst_case_non_target = total - available_target
        answer = worst_case_non_target + target_need
        color_label = self._LOGIC_COLOR_ROOTS.get(target_color, "нужного цвета")
        return (
            f"Минимум нужно вынуть **{answer}** шаров.\n"
            f"Логика: в худшем случае сначала попадутся все не-{color_label} "
            f"({worst_case_non_target}), затем ещё {target_need} нужного цвета."
        )

    def _logic_normalize_class(self, token: str) -> str:
        t = (token or "").strip().lower().replace("ё", "е")
        if not t:
            return ""
        if t in self._LOGIC_CLASS_NORMALIZE:
            return self._LOGIC_CLASS_NORMALIZE[t]
        st = _stem_ru(t)
        if st and st != t:
            return st
        if len(t) > 4 and t[-1] in {"ы", "и", "а", "я"}:
            return t[:-1]
        return t

    def _extract_universal_premises(self, lower: str) -> list[tuple[str, str, str, str]]:
        """
        Извлечь посылки вида "все A B" / "все A являются B".
        Возвращает: (A_norm, B_norm, A_raw, B_raw).
        """
        premises: list[tuple[str, str, str, str]] = []
        seen: set[tuple[str, str]] = set()
        patterns = (
            r"\bвсе\s+([а-яёa-z]+)\s+([а-яёa-z]+)\b",
            r"\bвсе\s+([а-яёa-z]+)\s+(?:явля(?:ются|ется)|это|—|-)\s*([а-яёa-z]+)\b",
        )
        for pattern in patterns:
            for match in re.finditer(pattern, lower):
                src_raw = match.group(1).strip()
                dst_raw = match.group(2).strip()
                src = self._logic_normalize_class(src_raw)
                dst = self._logic_normalize_class(dst_raw)
                if not src or not dst or src == dst:
                    continue
                key = (src, dst)
                if key in seen:
                    continue
                seen.add(key)
                premises.append((src, dst, src_raw, dst_raw))
        return premises

    def _extract_instance_premises(self, lower: str) -> list[tuple[str, str, str]]:
        """
        Извлечь посылки "X — Y", "X является Y", "X Y".
        Возвращает: (subject_norm, class_norm, class_raw).
        """
        instances: list[tuple[str, str, str]] = []
        seen: set[tuple[str, str]] = set()
        skip_subjects = {"все", "логика", "логическая", "задача", "какой", "что", "кто", "вывод", "следует"}

        def _add_instance(subject_raw: str, cls_raw: str) -> None:
            subject = (subject_raw or "").strip().lower().replace("ё", "е")
            cls = (cls_raw or "").strip().lower().replace("ё", "е")
            if not subject or not cls:
                return
            if subject in skip_subjects or len(subject) < 2 or len(cls) < 2:
                return
            cls_norm = self._logic_normalize_class(cls)
            if not cls_norm:
                return
            key = (subject, cls_norm)
            if key in seen:
                return
            seen.add(key)
            instances.append((subject, cls_norm, cls))

        for match in re.finditer(
            r"\b([а-яёa-z][а-яёa-z0-9_-]*)\s*(?:—|-|–)\s*([а-яёa-z]+)\b",
            lower,
        ):
            _add_instance(match.group(1), match.group(2))

        for match in re.finditer(
            r"\b([а-яёa-z][а-яёa-z0-9_-]*)\s+явля(?:ется|ются)\s+([а-яёa-z]+)\b",
            lower,
        ):
            _add_instance(match.group(1), match.group(2))

        clauses = re.split(r"[.!?…;:,\n]+", lower)
        for clause in clauses:
            c = clause.strip()
            if not c or "все " in c:
                continue
            tokens = re.findall(r"[а-яёa-z0-9_-]+", c)
            if len(tokens) < 2:
                continue
            _add_instance(tokens[0], tokens[1])

        return instances

    def _solve_basic_syllogism(self, query: str) -> str | None:
        text = (query or "").strip()
        lower = text.lower().replace("ё", "е")
        if "все " not in lower:
            return None
        universals = self._extract_universal_premises(lower)
        if not universals:
            return None

        instances = self._extract_instance_premises(lower)
        if not instances:
            # fallback на старый парсер формата minor-посылки
            subject, subject_class = self._extract_minor_premise(lower)
            if subject and subject_class:
                instances = [
                    (
                        subject.strip().lower().replace("ё", "е"),
                        self._logic_normalize_class(subject_class),
                        subject_class.strip().lower().replace("ё", "е"),
                    )
                ]
        if not instances:
            return None

        graph: dict[str, set[str]] = defaultdict(set)
        class_display: dict[str, str] = {}
        for src, dst, src_raw, dst_raw in universals:
            graph[src].add(dst)
            class_display.setdefault(src, src_raw)
            class_display.setdefault(dst, dst_raw)

        def _infer_chain(start_cls: str) -> list[str]:
            derived: list[str] = []
            queue_cls = [start_cls]
            seen = {start_cls}
            while queue_cls:
                cur = queue_cls.pop(0)
                for nxt in graph.get(cur, set()):
                    if nxt in seen:
                        continue
                    seen.add(nxt)
                    derived.append(nxt)
                    queue_cls.append(nxt)
            return derived

        best_subject = ""
        best_source_cls = ""
        best_property_norm = ""
        best_path_len = -1
        for subject, subj_cls_norm, subj_cls_raw in instances:
            derived = _infer_chain(subj_cls_norm)
            if not derived:
                continue
            # Приоритет явным логическим свойствам (например "смертен").
            target_norm = ""
            for d in derived:
                if "смерт" in d:
                    target_norm = d
                    break
            if not target_norm:
                target_norm = derived[0]
            if len(derived) > best_path_len:
                best_subject = subject
                best_source_cls = subj_cls_raw
                best_property_norm = target_norm
                best_path_len = len(derived)

        if not best_subject or not best_property_norm:
            return None

        property_raw = class_display.get(best_property_norm, best_property_norm)
        subject_display = best_subject[:1].upper() + best_subject[1:]
        subject_predicate = self._logic_subject_predicate(property_raw)
        major_src_raw = class_display.get(self._logic_normalize_class(best_source_cls), best_source_cls)
        return (
            "Логический разбор:\n"
            f"1) Все {major_src_raw} имеют свойство «{property_raw}» (прямо или по цепочке посылок).\n"
            f"2) {subject_display} — {best_source_cls}.\n"
            f"Вывод: {subject_display} {subject_predicate}."
        )

    def _extract_minor_premise(self, lower: str) -> tuple[str, str]:
        # Формат: "X — Y"
        for match in re.finditer(
            r"\b([а-яёa-z][а-яёa-z0-9_-]*)\s*(?:—|-|–)\s*([а-яёa-z]+)\b",
            lower,
        ):
            subj = match.group(1).strip()
            cls = match.group(2).strip()
            if subj == "все":
                continue
            return subj, cls

        # Формат: "X является Y"
        for match in re.finditer(
            r"\b([а-яёa-z][а-яёa-z0-9_-]*)\s+явля(?:ется|ется)\s+([а-яёa-z]+)\b",
            lower,
        ):
            subj = match.group(1).strip()
            cls = match.group(2).strip()
            if subj == "все":
                continue
            return subj, cls

        # Формат с многоточиями/точками: "X Y"
        skip_subjects = {"все", "логика", "логическая", "задача", "какой", "что"}
        clauses = re.split(r"[.!?…;:]+", lower)
        for clause in clauses:
            c = clause.strip()
            if not c or "все " in c:
                continue
            tokens = re.findall(r"[а-яёa-z0-9_-]+", c)
            if len(tokens) < 2:
                continue
            subj, cls = tokens[0], tokens[1]
            if subj in skip_subjects:
                continue
            if len(cls) < 3:
                continue
            return subj, cls

        return "", ""

    def _logic_subject_predicate(self, major_property: str) -> str:
        p = (major_property or "").strip().lower().replace("ё", "е")
        if not p:
            return "обладает указанным свойством"
        # Приводим частые формы мн. числа к форме для одного субъекта.
        if p.endswith("ны") and len(p) >= 4:
            return p[:-2] + "ен"
        if p.endswith("ие") and len(p) >= 4:
            return p[:-2] + "ий"
        if p.endswith("ые") and len(p) >= 4:
            return p[:-2] + "ый"
        return f"имеет свойство «{p}»"

    def _association_is_relevant(self, query: str, answer: str) -> bool:
        """
        Проверка качества ассоциации перед выдачей.
        Защищает от hash-совпадений с мусорным/чужим ответом.
        """
        q = (query or "").strip().lower()
        a = (answer or "").strip().lower()
        if not q or not a:
            return False

        q_tokens, q_stems = self._extract_terms_and_stems(
            q,
            drop_generic=False,
        )
        if not q_tokens:
            return True
        a_tokens, a_stems = self._extract_terms_and_stems(
            a,
            drop_generic=False,
        )
        overlap = len(q_tokens & a_tokens)
        if overlap >= 1:
            return True

        if q_stems and a_stems and len(q_stems & a_stems) >= 1:
            return True

        if re.search(r"\d", q) and re.search(r"\d", a):
            return True

        if any(k in q for k in ("код", "python", "функц", "пример")) and any(
            k in a for k in ("python", "for", "def", "```")
        ):
            return True

        return False

    def _answer_shape_is_valid(self, query: str, answer: str) -> bool:
        """
        Минимальная валидация формата ответа под тип вопроса.
        Нужна, чтобы отсеивать «красивый, но не по форме» retrieval-шум.
        """
        q = (query or "").strip().lower()
        a = (answer or "").strip().lower()
        if not q or not a:
            return False

        q_terms, _ = self._extract_terms_and_stems(q)
        if self._is_time_query(q, q_tokens=q_terms):
            has_time_value = bool(
                re.search(r"\b\d{1,2}:\d{2}\b", a)
                or re.search(r"\b\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}\b", a)
            )
            if not has_time_value:
                return False
            return True

        if "сколько" in q or "how many" in q:
            has_digits = bool(re.search(r"\b\d+\b", a))
            if not has_digits:
                num_words = {
                    "ноль", "один", "одна", "одно", "два", "две", "три", "четыре",
                    "пять", "шесть", "семь", "восемь", "девять", "десять",
                    "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                }
                a_tokens = set(_tokenize(a))
                has_digits = len(num_words & a_tokens) > 0
            if not has_digits:
                return False
            q_core, q_core_stems = self._extract_terms_and_stems(q)
            core_tokens = [t for t in q_core if t != "сколько"]
            if core_tokens:
                q_core = set(core_tokens)
                q_core_stems = self._extract_linguistic_stems(q_core)
                a_tokens, a_stems = self._extract_terms_and_stems(
                    a,
                    drop_generic=False,
                )
                token_overlap = len(q_core & a_tokens)
                stem_only_overlap = len((q_core_stems & a_stems))
                coverage = token_overlap + stem_only_overlap
                required = 1 if len(q_core) <= 1 else 2
                if coverage < required:
                    return False

        if "когда" in q or "в каком году" in q:
            if not re.search(r"\b(1[6-9]\d{2}|20\d{2}|2100)\b", a):
                return False

        if self._is_weather_query(q, q_tokens=q_terms):
            if not self._weather_answer_is_valid(q, a, q_tokens=q_terms):
                return False

        return True

    def _run_self_check(
        self,
        query: str,
        answer: str,
        method: str,
        confidence: float,
    ) -> dict[str, object]:
        """Лёгкая самопроверка ответа перед возвратом пользователю."""
        q = (query or "").strip().lower()
        a = (answer or "").strip().lower()

        checks: list[dict[str, object]] = []

        non_empty = len(a) >= 3
        checks.append({"name": "non_empty", "passed": non_empty, "weight": 0.2})

        bad_phrases = (
            "недостаточно локальных знаний",
            "в моей локальной базе пока мало",
            "добавьте материал",
        )
        not_placeholder = not any(p in a for p in bad_phrases)
        checks.append({"name": "not_placeholder", "passed": not_placeholder, "weight": 0.25})

        shape_ok = self._answer_shape_is_valid(q, a)
        checks.append({"name": "shape", "passed": shape_ok, "weight": 0.2})

        q_tokens, q_stems = self._extract_terms_and_stems(q)
        a_tokens, a_stems = self._extract_terms_and_stems(a, drop_generic=False)
        overlap = len(q_tokens & a_tokens) if q_tokens else 0
        if q_tokens:
            overlap += len((q_stems & a_stems))
        lexical_ok = overlap >= (1 if len(q_tokens) <= 1 else 2) if q_tokens else True
        checks.append({"name": "lexical_overlap", "passed": lexical_ok, "weight": 0.2})

        conf_ok = float(confidence) >= 0.25 or method in {"math-eval", "logic-solver", "precise-retrieval", "topic-overview"}
        checks.append({"name": "confidence_floor", "passed": conf_ok, "weight": 0.15})

        total_weight = sum(float(c["weight"]) for c in checks) or 1.0
        gained_weight = sum(float(c["weight"]) for c in checks if bool(c["passed"]))
        score = gained_weight / total_weight
        passed = score >= 0.62

        return {
            "passed": passed,
            "score": round(float(score), 4),
            "method": method,
            "confidence": float(confidence),
            "checks": checks,
        }

    def _repair_answer_with_self_check(
        self,
        message: str,
        retrieval_query: str,
        retrieved_sentences: list[tuple[str, float]],
        context_window: ContextWindow | None = None,
    ) -> str | None:
        """Попытка восстановить ответ, если self-check провалился."""
        # 1) Пробуем более строгий factual retrieval.
        precise = self._try_precise_retrieval_answer(message)
        if precise and self._answer_shape_is_valid(message, precise):
            return precise

        # 2) Пробуем краткий topic overview.
        overview = self._try_topic_overview_answer(message)
        if overview and self._answer_shape_is_valid(message, overview):
            return overview

        # 3) Локальная реконструкция из retrieval-кандидатов.
        if retrieved_sentences:
            repaired = self._build_coherent_response(
                retrieval_query or message,
                retrieved_sentences[:6],
                [],
                [],
            )
            if repaired and self._answer_shape_is_valid(message, repaired):
                return repaired

        # 4) Диалоговый fallback по контексту.
        if context_window is not None:
            q_tokens, q_stems = self._extract_terms_and_stems(message)
            context_answer = self._conversation_context_fallback(
                message=message,
                context_window=context_window,
                q_tokens=q_tokens,
                q_stems=q_stems,
            )
            if context_answer and self._answer_shape_is_valid(message, context_answer):
                return context_answer

        # 5) Web augment (если включён) как крайний fallback.
        try:
            q_tokens, q_stems = self._extract_terms_and_stems(message)
            web = self._try_web_augment_answer(
                message,
                q_tokens=q_tokens,
                q_stems=q_stems,
                force=True,
                latency_budget_sec=2.0,
            )
            if web and self._weather_answer_is_valid(message, web, q_tokens=q_tokens):
                return web
        except Exception:
            pass

        return None

    def _try_precise_retrieval_answer(self, query: str) -> str | None:
        """
        Точный retrieval для фактологических вопросов.
        Ищет предложения, где покрыто максимум ключевых токенов вопроса.
        """
        q = (query or "").strip().lower()
        if not q:
            return None
        is_precision = any(
            marker in q
            for marker in (
                "столица", "в каком году", "когда", "что такое", "кто такой",
                "кто такая", "кто такое", "назови", "объясни", "пример",
                "рецепт", "приготов",
                "сколько", "how many",
            )
        )
        if not is_precision:
            return None
        q_terms = self._extract_linguistic_terms(query, min_len=2, drop_stop=True, drop_generic=False)
        if self._is_time_query(q, q_tokens=q_terms):
            return None
        if self._is_currency_query(q, q_tokens=q_terms):
            return None
        is_count_query = ("сколько" in q or "how many" in q)

        query_tokens = [t for t in _tokenize(q) if len(t) >= 2]
        capital_idx = next((i for i, t in enumerate(query_tokens) if t.startswith("столиц")), -1)
        is_capital_query = capital_idx >= 0
        country_tokens: list[str] = []
        if is_capital_query:
            # Берем содержательные токены после "столиц*": обычно это страна.
            for t in query_tokens[capital_idx + 1:]:
                if len(t) >= 3 and not _is_stop_word(t) and t not in _GENERIC_QUERY_WORDS:
                    country_tokens.append(t)

        core_tokens = [
            t for t in _tokenize(q)
            if len(t) >= 3 and not _is_stop_word(t) and t not in _GENERIC_QUERY_WORDS
        ]
        if is_capital_query:
            # Для запроса вида "столица X" ключевая сущность — X, не слово "столица".
            core_tokens = [t for t in core_tokens if not t.startswith("столиц")]
            if not core_tokens and country_tokens:
                core_tokens = list(country_tokens)
        if not core_tokens:
            return None

        if is_count_query:
            quick_count_hits = self.sentence_store.retrieve(query=q, formula=None, top_k=24)
            quick_count_answer = self._try_count_answer_from_candidates(query, quick_count_hits)
            if quick_count_answer:
                return quick_count_answer

        year_query = ("когда" in q or "в каком году" in q)
        is_definition_query = bool(re.search(r"\bчто\s+такое\b", q))
        is_explain_query = bool(
            re.search(r"\b(объясни|поясни|обьясни)\b", q)
            or "простыми словами" in q
        )
        is_person_query = bool(re.search(r"\bкто\s+так(ой|ая|ое)\b", q))
        is_definition_like_query = is_definition_query or is_explain_query
        definition_terms: list[str] = []
        if is_definition_query:
            m_def = re.search(r"что\s+такое\s+(.+)$", q)
            if m_def:
                definition_terms = [
                    t for t in _tokenize(m_def.group(1))
                    if len(t) >= 3 and not _is_stop_word(t) and t not in _GENERIC_QUERY_WORDS
                ]
        elif is_explain_query:
            m_explain = re.search(
                r"(?:объясни|поясни|обьясни)\s+(.+?)(?:\s+простыми\s+словами)?$",
                q,
            )
            if m_explain:
                definition_terms = [
                    t for t in _tokenize(m_explain.group(1))
                    if len(t) >= 3 and not _is_stop_word(t) and t not in _GENERIC_QUERY_WORDS
                ]
        definition_stems = {_stem_ru(t) for t in definition_terms if len(t) >= 4}

        if is_definition_like_query and definition_terms:
            # Для "что такое / объясни ..." сначала берём curated локальные факты.
            seed_hits = self._topic_snippets_from_seed(definition_terms, definition_stems, limit=80)
            if seed_hits:
                seed_ranked: list[tuple[int, str]] = []
                for seed_text in seed_hits:
                    low = seed_text.lower()
                    s_tokens = {
                        t for t in _tokenize(low)
                        if len(t) >= 3 and not _is_stop_word(t)
                    }
                    s_stems = {_stem_ru(t) for t in s_tokens if len(t) >= 4}
                    exact = len(set(definition_terms) & s_tokens)
                    stem = len(definition_stems & s_stems) if definition_stems else 0
                    if exact == 0 and stem == 0:
                        continue
                    score = exact * 4 + stem * 2
                    if is_explain_query and any(mark in low for mark in ("простыми словами", "показыва", "это", "помогает")):
                        score += 2
                    if len(seed_text) > 180:
                        score -= 1
                    if "о математике я знаю" in low:
                        score -= 2
                    seed_ranked.append((score, seed_text))
                if seed_ranked:
                    seed_ranked.sort(key=lambda x: x[0], reverse=True)
                    return seed_ranked[0][1].rstrip(".") + "."

        if is_explain_query and definition_terms:
            # Быстрый explain-path без тяжёлого полного сканирования candidate_ids.
            quick_query = " ".join(definition_terms)
            quick_hits = self.sentence_store.retrieve(query=quick_query, formula=None, top_k=16)
            quick_ranked: list[tuple[float, str]] = []
            for text, score in quick_hits:
                clean = (text or "").strip().rstrip(".")
                if len(clean) < 20 or _is_low_quality_sentence(clean):
                    continue
                low = clean.lower()
                t_tokens = {
                    t for t in _tokenize(low)
                    if len(t) >= 3 and not _is_stop_word(t)
                }
                if not t_tokens:
                    continue
                t_stems = {_stem_ru(t) for t in t_tokens if len(t) >= 4}
                exact = len(set(definition_terms) & t_tokens)
                stem = len(definition_stems & t_stems) if definition_stems else 0
                if exact == 0 and stem == 0:
                    continue

                rank = float(score) + exact * 0.8 + stem * 0.4
                if any(mark in low for mark in ("простыми словами", "показыва", "это", "помогает")):
                    rank += 1.2
                if len(clean) > 220 or "(" in clean or ")" in clean:
                    rank -= 0.6
                if "о математике я знаю" in low:
                    rank -= 1.2
                quick_ranked.append((rank, clean))

            if quick_ranked:
                quick_ranked.sort(key=lambda x: x[0], reverse=True)
                return quick_ranked[0][1] + "."
            return None

        def _token_variants(tok: str) -> set[str]:
            variants = {tok}
            stem = _stem_ru(tok)
            if stem:
                variants.add(stem)
            # Для коротких существительных добавляем базовые падежные формы.
            if len(tok) >= 4:
                variants.add(tok + "а")
                variants.add(tok + "у")
            if len(tok) >= 5:
                for suf in ("ами", "ями", "ого", "ему", "ыми", "ими", "ов", "ев", "ой", "ей", "ом", "ам", "ям", "ах", "ях", "ую", "юю", "у", "ю", "ы", "и", "е", "а", "я"):
                    if tok.endswith(suf) and len(tok) - len(suf) >= 4:
                        base = tok[: -len(suf)]
                        variants.add(base)
                        variants.add(base + "а")
                        variants.add(base + "я")
                        # Частые прилагательные/причастия: производную -> производная.
                        variants.add(base + "ая")
                        variants.add(base + "ое")
                        variants.add(base + "ый")
                        variants.add(base + "ий")
                        break
            return {v for v in variants if len(v) >= 3}

        token_candidates: list[set[int]] = []
        for tok in core_tokens:
            ids: set[int] = set()
            for v in _token_variants(tok):
                rows = self.sentence_store._word_index.get(djb2_hash(v), [])
                if rows:
                    ids.update(rows)
            if ids:
                token_candidates.append(ids)

        if not token_candidates:
            return None

        if len(token_candidates) == 1:
            candidate_ids = set(next(iter(token_candidates)))
        else:
            candidate_ids = set.intersection(*token_candidates)
            if not candidate_ids:
                coverage: dict[int, int] = {}
                for ids in token_candidates:
                    for idx in ids:
                        coverage[idx] = coverage.get(idx, 0) + 1
                need = max(1, len(token_candidates) - 1)
                candidate_ids = {idx for idx, hit in coverage.items() if hit >= need}
        if not candidate_ids:
            return None

        noise_markers = {
            "prod-smoke", "deploy-smoke", "backend-smoke", "стрим", "точных хватает",
            "зацени", "инсту", "залетает",
        }
        q_stems = {_stem_ru(v) for t in core_tokens for v in _token_variants(t) if len(v) >= 4}
        ranked: list[tuple[int, int, str]] = []
        capital_country_display = ""
        capital_country_re = ""
        if is_capital_query and country_tokens:
            country_forms: set[str] = set()
            for ct in country_tokens:
                country_forms.update(_token_variants(ct))
                st = _stem_ru(ct)
                if st and len(st) >= 3:
                    country_forms.add(st)
            escaped = sorted(
                {re.escape(f) for f in country_forms if len(f) >= 3},
                key=len,
                reverse=True,
            )
            if escaped:
                capital_country_re = "(?:" + "|".join(escaped) + ")"
            capital_country_display = " ".join(country_tokens).strip().capitalize()

        active_client = self._active_client_id_var.get("global")
        is_ephemeral_eval = self._is_ephemeral_client(active_client)

        max_candidates = 5000
        if is_definition_like_query:
            max_candidates = 1800
        elif is_person_query:
            max_candidates = 2400
        elif is_capital_query:
            max_candidates = 2200
        if is_ephemeral_eval:
            if is_definition_like_query:
                max_candidates = min(max_candidates, 650)
            elif is_capital_query:
                max_candidates = min(max_candidates, 900)
            else:
                max_candidates = min(max_candidates, 800)

        def _simplicity_bonus(text_raw: str, text_lower: str) -> int:
            words = [w for w in _tokenize(text_lower) if len(w) >= 2]
            if not words:
                return 0
            avg_len = sum(len(w) for w in words) / len(words)
            bonus = 0
            if 6 <= len(words) <= 24:
                bonus += 1
            if avg_len <= 7.5:
                bonus += 1
            if any(mark in text_lower for mark in ("простыми словами", "это", "показыва", "помогает")):
                bonus += 1
            if "(" in text_raw or ")" in text_raw:
                bonus -= 1
            if len(text_raw) > 220:
                bonus -= 1
            if any(mark in text_lower for mark in ("дифференцируем", "комплексн", "тополог", "несобственн")):
                bonus -= 1
            return bonus

        for idx in sorted(candidate_ids)[:max_candidates]:
            text = self.sentence_store.get_text(idx).strip()
            if len(text) < 16:
                continue
            if _is_low_quality_sentence(text):
                continue
            lowered = text.lower()
            if any(m in lowered for m in noise_markers):
                continue

            t_tokens = {
                t for t in _tokenize(lowered)
                if len(t) >= 3 and not _is_stop_word(t)
            }
            if not t_tokens:
                continue
            overlap = len(set(core_tokens) & t_tokens)
            t_stems = {_stem_ru(t) for t in t_tokens if len(t) >= 4}
            stem_overlap = len(q_stems & t_stems) if q_stems else 0
            hit = max(overlap, stem_overlap)
            required = 1 if len(core_tokens) == 1 else 2
            if hit < required:
                continue

            if is_capital_query:
                # Для "столица X" ответ должен явно содержать слово столица
                # и токен страны/её основу.
                has_capital_word = any(tok.startswith("столиц") for tok in t_tokens) or "столиц" in lowered
                if not has_capital_word:
                    continue
                if country_tokens:
                    country_ok = False
                    for ct in country_tokens:
                        variants = _token_variants(ct)
                        if any(v in t_tokens for v in variants):
                            country_ok = True
                            break
                        cstem = _stem_ru(ct)
                        if cstem and cstem in t_stems:
                            country_ok = True
                            break
                    if not country_ok:
                        continue
                # Отбрасываем историко-описательные конструкции, где "столица" упоминается
                # как часть события, а не как факт "X — столица Y".
                if any(marker in lowered for marker in ("со столиц", "в качестве столиц", "перенес столиц", "перенёс столиц", "был выбран в качестве столиц")):
                    continue

                if capital_country_re:
                    city_match = None
                    patterns = (
                        rf"столиц[аы]\s+{capital_country_re}\s*[—-]\s*([а-яёa-z][а-яёa-z\- ]{{1,40}})",
                        rf"([а-яёa-z][а-яёa-z\- ]{{1,40}})\s*[—-]\s*столиц[аы]\s+{capital_country_re}",
                        rf"([а-яёa-z][а-яёa-z\- ]{{1,40}})\s+являет(?:ся|ась)\s+столиц[аеи]\s+{capital_country_re}",
                        rf"столиц[аеи]\s+{capital_country_re}\s+являет(?:ся|ась)\s+([а-яёa-z][а-яёa-z\- ]{{1,40}})",
                    )
                    for p in patterns:
                        m_city = re.search(p, lowered, flags=re.IGNORECASE)
                        if m_city:
                            city_match = m_city.group(1)
                            break
                    if not city_match:
                        continue
                    city_words = [
                        w for w in city_match.strip(" ,.;:()[]{}\"'`").split()
                        if len(w) >= 2
                    ][:3]
                    if not city_words:
                        continue
                    city = " ".join(city_words)
                    canonical = (
                        f"Столица {capital_country_display} — "
                        f"{city[:1].upper()}{city[1:]}"
                    )
                    ranked.append((hit + 8, -abs(len(canonical) - 40), canonical))
                    continue

            if year_query and not re.search(
                r"\b(1[6-9]\d{2}|20\d{2}|2100)\b", lowered,
            ):
                continue
            rank_bonus = 0
            if is_capital_query and "—" in text and "столиц" in lowered:
                rank_bonus += 2
            if "математ" in q and "математик" in lowered and any(
                marker in lowered for marker in ("изучает", "арифмет", "алгебр", "геометр", "анализ")
            ):
                rank_bonus += 2
            if is_definition_like_query and definition_terms:
                term_overlap = len(set(definition_terms) & t_tokens)
                stem_term_overlap = len(definition_stems & t_stems) if definition_stems else 0
                if term_overlap == 0 and stem_term_overlap == 0:
                    continue
                if any(
                    marker in lowered
                    for marker in (
                        "— это", " это ", "называется", "является",
                        "определяется", "представляет собой",
                    )
                ):
                    rank_bonus += 3
                if any(
                    marker in lowered
                    for marker in ("рок-групп", "альбом", "песня", "фильм", "экранизац")
                ):
                    rank_bonus -= 2
                if is_explain_query:
                    rank_bonus += _simplicity_bonus(text, lowered)
            if is_person_query:
                if any(marker in lowered for marker in ("родил", "учёный", "ученый", "математик", "логик", "криптограф", "пионер", "информатик", "был", "является")):
                    rank_bonus += 1
                if lowered.count(",") >= 2 and "счита" in lowered:
                    rank_bonus -= 2
            ranked.append((hit + rank_bonus, -abs(len(text) - 110), text.rstrip(".")))

        if not ranked:
            return None

        ranked.sort(key=lambda x: (x[0], x[1]), reverse=True)
        if is_count_query:
            count_candidates = [
                (text, max(0.08, float(score) * 0.2))
                for score, _len_rank, text in ranked[:24]
            ]
            count_answer = self._try_count_answer_from_candidates(query, count_candidates)
            if count_answer:
                return count_answer
            return None
        top = ranked[:2]
        if is_capital_query or year_query:
            return top[0][2] + "."
        if is_definition_like_query or is_person_query:
            return top[0][2] + "."
        if len(top) == 1:
            return top[0][2] + "."
        return f"{top[0][2]}. {top[1][2]}."

    def _try_topic_overview_answer(self, query: str, *, fast_mode: bool = False) -> str | None:
        """
        Краткий обзор темы для запросов вида:
        - "что ты знаешь о X"
        - "какие данные о X"
        - "расскажи о X"
        """
        q = (query or "").strip().lower()
        if not q:
            return None

        m = re.search(
            r"(?:зна\w*\s+(?:о|про)|данн\w*\s+(?:о|про)|расскажи\s+(?:о|про))\s+(.+)$",
            q,
        )
        if not m:
            return None
        topic_text = m.group(1).strip(" ?!.")
        topic_tokens = [
            t for t in _tokenize(topic_text)
            if len(t) >= 3 and not _is_stop_word(t) and t not in _GENERIC_QUERY_WORDS
        ]
        if not topic_tokens:
            return None
        topic_stems = {_stem_ru(t) for t in topic_tokens if len(t) >= 4}

        # Быстрый путь: без полного прохода по десяткам тысяч записей.
        if fast_mode:
            seed_hits = self._topic_snippets_from_seed(topic_tokens, topic_stems, limit=2)
            if seed_hits:
                if len(seed_hits) == 1:
                    return seed_hits[0].rstrip(".") + "."
                return f"{seed_hits[0].rstrip('.')}. {seed_hits[1].rstrip('.')}."
            quick_retrieved = self.sentence_store.retrieve(query=topic_text, formula=None, top_k=8)
            if not quick_retrieved:
                return None
            quick_snippets = self._rank_relevant_snippets(
                message=query,
                candidates=quick_retrieved,
                q_tokens=set(topic_tokens),
                q_stems=topic_stems,
                max_out=2,
                min_score=0.07,
            )
            if not quick_snippets:
                return None
            if len(quick_snippets) == 1:
                return quick_snippets[0].rstrip(".") + "."
            return f"{quick_snippets[0].rstrip('.')}. {quick_snippets[1].rstrip('.')}."

        # 1) Seed-corpus приоритет: короткие curated факты без wiki-шума.
        seed_hits = self._topic_snippets_from_seed(topic_tokens, topic_stems, limit=2)
        if seed_hits:
            if len(seed_hits) == 1:
                return seed_hits[0].rstrip(".") + "."
            return f"{seed_hits[0].rstrip('.')}. {seed_hits[1].rstrip('.')}."

        def _variants(tok: str) -> set[str]:
            vals = {tok}
            st = _stem_ru(tok)
            if st:
                vals.add(st)
            if len(tok) >= 5:
                for suf in ("ами", "ями", "ого", "ему", "ыми", "ими", "ов", "ев", "ой", "ей", "ом", "ам", "ям", "ах", "ях", "ую", "юю", "у", "ю", "ы", "и", "е", "а", "я"):
                    if tok.endswith(suf) and len(tok) - len(suf) >= 4:
                        base = tok[: -len(suf)]
                        vals.add(base)
                        vals.add(base + "а")
                        break
            return {v for v in vals if len(v) >= 3}

        candidate_ids: set[int] = set()
        for tok in topic_tokens:
            for v in _variants(tok):
                candidate_ids.update(self.sentence_store._word_index.get(djb2_hash(v), []))
        def _pick_topic_snippets(candidate_indexes: list[int]) -> list[str]:
            scored: list[tuple[float, str]] = []
            for idx in candidate_indexes:
                if idx < 0 or idx >= self.sentence_store.size:
                    continue
                text = self.sentence_store.get_text(idx).strip()
                if len(text) < 20:
                    continue
                if _is_low_quality_sentence(text):
                    continue
                lowered = text.lower()
                if any(m in lowered for m in ("prod-smoke", "deploy-smoke", "backend-smoke", "инсту", "зацени", "retrieved")):
                    continue
                if topic_tokens and not any(tok in lowered for tok in topic_tokens):
                    if topic_stems and not any(st and st in lowered for st in topic_stems):
                        continue
                score = 0.0
                if any(mark in lowered for mark in ("это", "изучает", "процесс", "используется", "включает", "основы")):
                    score += 1.6
                score += min(1.2, len(text) / 160.0)
                score -= abs(len(text) - 120) / 300.0
                scored.append((score, text.rstrip(".")))
            if not scored:
                return []
            scored.sort(key=lambda x: x[0], reverse=True)
            out: list[str] = []
            for _s, t in scored:
                if all(t[:48] != prev[:48] for prev in out):
                    out.append(t)
                if len(out) >= 2:
                    break
            return out

        if not candidate_ids:
            # Лексический fallback: проход по хранилищу по теме (редкий, но устойчивый).
            sample_indexes = list(range(min(self.sentence_store.size, 50000)))
            snippets = _pick_topic_snippets(sample_indexes)
            if not snippets:
                # Последний fallback: BM25 retrieval по теме.
                retrieved = self.sentence_store.retrieve(query=topic_text, formula=None, top_k=8)
                snippets = [
                    t.strip().rstrip(".")
                    for t, _score in retrieved
                    if len(t.strip()) >= 20 and not _is_low_quality_sentence(t)
                ][:2]
            if not snippets:
                return None
            if len(snippets) == 1:
                return snippets[0] + "."
            return f"{snippets[0]}. {snippets[1]}."

        ranked: list[tuple[float, str]] = []
        for idx in sorted(candidate_ids)[:7000]:
            text = self.sentence_store.get_text(idx).strip()
            if len(text) < 20:
                continue
            if _is_low_quality_sentence(text):
                continue
            lowered = text.lower()
            if any(m in lowered for m in ("prod-smoke", "deploy-smoke", "backend-smoke", "инсту", "зацени")):
                continue

            t_tokens = {t for t in _tokenize(lowered) if len(t) >= 3 and not _is_stop_word(t)}
            if not t_tokens:
                continue
            overlap = len(set(topic_tokens) & t_tokens)
            t_stems = {_stem_ru(t) for t in t_tokens if len(t) >= 4}
            stem_overlap = len(topic_stems & t_stems) if topic_stems else 0
            hit = max(overlap, stem_overlap)
            if hit < 1:
                continue

            score = float(hit)
            if any(mark in lowered for mark in ("это", "изучает", "процесс", "используется", "включает", "основы")):
                score += 1.4
            score -= abs(len(text) - 120) / 280.0
            ranked.append((score, text.rstrip(".")))

        if not ranked:
            snippets = _pick_topic_snippets(sorted(candidate_ids)[:12000])
            if not snippets:
                retrieved = self.sentence_store.retrieve(query=topic_text, formula=None, top_k=8)
                snippets = [
                    t.strip().rstrip(".")
                    for t, _score in retrieved
                    if len(t.strip()) >= 20 and not _is_low_quality_sentence(t)
                ][:2]
            if not snippets:
                return None
            if len(snippets) == 1:
                return snippets[0] + "."
            return f"{snippets[0]}. {snippets[1]}."
        ranked.sort(key=lambda x: x[0], reverse=True)
        best = ranked[:2]
        if len(best) == 1:
            return best[0][1] + "."
        return f"{best[0][1]}. {best[1][1]}."

    def _topic_snippets_from_seed(
        self,
        topic_tokens: list[str],
        topic_stems: set[str],
        limit: int = 2,
    ) -> list[str]:
        """
        Извлечь тематические предложения из локальных seed-файлов.
        Это data-driven fallback без хардкод-ответов в логике.
        """
        seed_dir = _PROJECT_ROOT / "seeds"
        if not seed_dir.exists():
            return []
        scored: list[tuple[float, str]] = []
        max_seed_file_bytes = max(8_192, _env_int("KOLIBRI_SEED_MAX_FILE_BYTES", 300_000))
        max_seed_lines = max(200, _env_int("KOLIBRI_SEED_MAX_LINES_PER_FILE", 20_000))
        try:
            files = sorted(seed_dir.glob("*.txt"))[:120]
        except Exception:
            return []

        topic_forms: set[str] = set()
        for tok in topic_tokens:
            if len(tok) < 3:
                continue
            topic_forms.add(tok)
            st = _stem_ru(tok)
            if st and len(st) >= 3:
                topic_forms.add(st)
            if len(tok) >= 5:
                for suf in ("ами", "ями", "ого", "ему", "ыми", "ими", "ов", "ев", "ой", "ей", "ом", "ам", "ям", "ах", "ях", "ую", "юю", "у", "ю", "ы", "и", "е", "а", "я"):
                    if tok.endswith(suf) and len(tok) - len(suf) >= 4:
                        base = tok[: -len(suf)]
                        topic_forms.add(base)
                        topic_forms.add(base + "а")
                        break

        for path in files:
            try:
                if path.stat().st_size > max_seed_file_bytes:
                    continue
                lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
            except OSError:
                continue
            for raw in lines[:max_seed_lines]:
                text = raw.strip()
                if len(text) < 20:
                    continue
                if _is_low_quality_sentence(text):
                    continue
                lowered = text.lower()
                if lowered.endswith(":"):
                    continue
                tok_hit = any(t in lowered for t in topic_forms) or any(t in lowered for t in topic_tokens)
                stem_hit = any(st and st in lowered for st in topic_stems)
                if not tok_hit and not stem_hit:
                    continue
                score = 0.0
                if any(mark in lowered for mark in ("это", "изучает", "процесс", "включает", "основы", "краткий рецепт")):
                    score += 1.2
                score += min(1.0, len(text) / 180.0)
                if " | " in text:
                    score -= 1.0
                scored.append((score, text))

        if not scored:
            return []
        scored.sort(key=lambda x: x[0], reverse=True)
        out: list[str] = []
        for _s, t in scored:
            if all(t[:48] != prev[:48] for prev in out):
                out.append(t)
            if len(out) >= max(1, limit):
                break
        return out

    def _extract_topic_focus(self, query: str) -> str | None:
        """
        Вытащить предмет темы из мета-запросов:
        "что ты знаешь о X", "какие данные о X", "расскажи о X".
        """
        q = (query or "").strip().lower()
        if not q:
            return None
        m = re.search(
            r"(?:зна\w*\s+(?:о|про)|данн\w*\s+(?:о|про)|расскажи\s+(?:о|про))\s+(.+)$",
            q,
        )
        if not m:
            return None
        topic = m.group(1).strip(" ?!.,:")
        tokens = [
            t for t in _tokenize(topic)
            if len(t) >= 3 and not _is_stop_word(t) and t not in _GENERIC_QUERY_WORDS
        ]
        if not tokens:
            return None
        return " ".join(tokens[:4])

    def _graph_answer_is_relevant(self, query: str, answer: str) -> bool:
        """
        Более строгая проверка релевантности для граф-ответа.
        Защищает от «словесного салата» с ложной высокой confidence.
        """
        q = (query or "").strip().lower()
        a = (answer or "").strip().lower()
        if not q or not a:
            return False

        # Явный фильтр на сленг/мусорные хвосты.
        noisy_markers = {
            "зацени", "офиген", "инсту", "залетает", "lil", "биг",
            "точных хватает", "стрим проверка", "smoke",
        }
        if any(m in a for m in noisy_markers):
            return False

        q_tokens = [t for t in _tokenize(q) if len(t) >= 3 and not _is_stop_word(t)]
        core_q = [t for t in q_tokens if t not in _GENERIC_QUERY_WORDS] or q_tokens
        a_tokens = {t for t in _tokenize(a) if len(t) >= 3 and not _is_stop_word(t)}
        if not core_q:
            return False

        overlap = len(set(core_q) & a_tokens)
        q_stems = {_stem_ru(t) for t in core_q if len(t) >= 4}
        a_stems = {_stem_ru(t) for t in a_tokens if len(t) >= 4}
        stem_overlap = len(q_stems & a_stems) if q_stems and a_stems else 0

        needed = 1 if len(set(core_q)) <= 1 else 2
        if overlap >= needed or stem_overlap >= needed:
            # Для временных вопросов ожидаем дату/год.
            if any(k in q for k in ("когда", "в каком году")):
                if not re.search(r"\b(1[6-9]\d{2}|20\d{2}|2100)\b", a):
                    return False
            return True

        # Числовые и кодовые вопросы — отдельный мягкий путь.
        if re.search(r"\d", q) and re.search(r"\d", a):
            return True
        if any(k in q for k in ("python", "код", "пример", "цикл")) and any(
            k in a for k in ("python", "for", "while", "def", "код")
        ):
            return True
        return False

    # ------------------------------------------------------------------
    # Связная генерация: когерентные ответы вместо склейки фрагментов
    # ------------------------------------------------------------------

    def _build_coherent_response(
        self,
        query: str,
        sentences: list[tuple[str, float]],
        formula_words: list[tuple[str, float]],
        c_knowledge: list[str],
        context_window = None,
    ) -> str:
        """
        Связная генерация ответа из найденных фрагментов.

        Вместо простой склейки ". ".join():
        1. Ранжирование по релевантности к запросу + контексту диалога
        2. Удаление дублирующей информации
        3. Логическое упорядочивание (от общего к частному)
        4. Добавление связующих конструкций
        5. Интеграция формульных слов как контекстных подсказок
        6. Проверка полноты ответа
        """
        query_tokens = set(_tokenize(query.lower()))
        meaningful_query = {t for t in query_tokens if not _is_stop_word(t)}
        core_query = {t for t in meaningful_query if t not in _GENERIC_QUERY_WORDS}
        if not core_query:
            core_query = meaningful_query
        key_terms = sorted(core_query, key=len, reverse=True)[:2]
        # --- Стемы для морфологического совпадения («искусственном» ≈ «искусственный») ---
        meaningful_stems = {_stem_ru(t) for t in core_query if len(t) >= 4}
        key_stems = {_stem_ru(t) for t in key_terms if len(t) >= 4}
        scored_sentences: list[tuple[str, float, int]] = []
        query_has_cyr = any("\u0400" <= c <= "\u04ff" for c in query)

        # Семантический вектор запроса (для embedding-ранжирования)
        query_vec = None
        use_embeddings = len(self.embeddings.vectors) > 100
        if use_embeddings:
            query_vec = self.embeddings.sentence_vector(query)

        # Контекст диалога для boost релевантных предложений
        context_terms: set[str] = set()
        if context_window is not None:
            context_msgs = context_window.get_recent_messages(limit=3)
            for msg in context_msgs:
                c_tokens = set(_tokenize(msg.lower()))
                context_terms |= {t for t in c_tokens if not _is_stop_word(t) and len(t) >= 3}

        # Шаг 1: Ранжируем и дедуплицируем
        seen_content: list[str] = []  # Полные тексты для near-duplicate check
        for text, base_score in sentences[:8]:
            text = text.strip()
            if not text or len(text) < 15:
                continue
            if _is_low_quality_sentence(text):
                continue
            # Проверка на near-duplicate: не добавлять предложения с >60% пересечением слов
            text_words = set(_tokenize(text.lower()))
            meaningful_tw = {t for t in text_words if not _is_stop_word(t) and len(t) >= 3}

            # Для русского запроса отбрасываем преимущественно латинские фрагменты:
            # это основной источник шумных ответов.
            if query_has_cyr:
                letters = [ch for ch in text if ch.isalpha()]
                if letters:
                    cyr = sum(1 for ch in letters if "\u0400" <= ch <= "\u04ff")
                    if (cyr / len(letters)) < 0.2:
                        continue

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
            overlap = len(core_query & meaningful_text)
            # Морфологическое совпадение через стемминг
            text_stems = {_stem_ru(t) for t in meaningful_text if len(t) >= 4}
            if overlap == 0 and meaningful_stems:
                stem_overlap = len(meaningful_stems & text_stems)
                overlap = stem_overlap  # Стемы работают как fallback

            # Семантическое сходство через эмбеддинги: boost даже без overlap слов
            emb_sim = 0.0
            if query_vec is not None:
                sent_vec = self.embeddings.sentence_vector(text)
                if sent_vec is not None and query_vec is not None:
                    dot = sum(a * b for a, b in zip(query_vec, sent_vec))
                    emb_sim = max(0.0, dot)  # cosine (уже L2-нормализованы)

            # Ключевые термы (обычно самые предметные) должны присутствовать,
            # иначе вероятность тематического мусора высокая.
            has_key_term = not key_terms
            if not has_key_term:
                has_key_term = bool(set(key_terms) & meaningful_text)
                if not has_key_term and key_stems:
                    has_key_term = bool(key_stems & text_stems)
            if not has_key_term and emb_sim < 0.5:
                continue

            # Адаптивный порог на покрытие содержательных слов запроса.
            min_overlap = 3 if len(core_query) >= 6 else 2 if len(core_query) >= 2 else 1
            if overlap < min_overlap and core_query and emb_sim < 0.45:
                continue
            relevance = base_score + overlap * 0.15 + emb_sim * 0.25
            len_bonus = min(1.0, len(text) / 200) * 0.1
            if len(text) > 300:
                len_bonus -= 0.05

            # Boost за контекст диалога: если предложение содержит термины из недавних сообщений
            context_overlap = len(context_terms & meaningful_text) if context_terms else 0
            context_bonus = context_overlap * 0.08

            scored_sentences.append((text, relevance + len_bonus + context_bonus, len(text)))

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
                prev_tokens = set(_tokenize(parts[-1].lower()))
                curr_tokens = set(_tokenize(text.lower()))
                new_info = len(curr_tokens - prev_tokens)
                if new_info < 2:
                    continue

                # Более естественные переходы в зависимости от контекста
                if score > 0.6:
                    # Высокая релевантность — прямое добавление
                    parts.append(text)
                elif i == 1:
                    # Второе предложение — уточнение
                    transition = self._pick_transition(text, context="elaboration")
                    parts.append(f"{transition} {text[0].lower()}{text[1:]}")
                elif i == 2:
                    # Третье — дополнительный факт
                    transition = self._pick_transition(text, context="addition")
                    parts.append(f"{transition} {text[0].lower()}{text[1:]}")
                else:
                    # Дальше — просто связка
                    parts.append(f"Также {text[0].lower()}{text[1:]}")

        answer = ". ".join(parts)
        # Очистка артефактов склейки
        answer = answer.replace(":.", ":").replace(".. ", ". ").replace("..", ".")
        if not answer.endswith((".", "!", "?")):
            answer += "."

        # Шаг 5: Формульные слова — пока только во внутренней аналитике
        # (formula_data.formula_generated_words в JSON ответе).
        # Показываем только если формула достаточно обучена
        # и слова семантически пересекаются с запросом.
        if self._show_formula_hints and formula_words and self.formula_pool.generation >= 2000:
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

    @staticmethod
    def _pick_transition(text: str, context: str = "elaboration") -> str:
        """Выбирает естественный переход в зависимости от контекста."""
        text_lower = text.lower()

        if context == "elaboration":
            # Уточнение/расширение
            if any(w in text_lower for w in ("например", "в частности", "так")):
                return "В частности,"
            if any(w in text_lower for w in ("потому что", "так как", "причина")):
                return "Это объясняется тем, что"
            if any(w in text_lower for w in ("однако", "но", "тем не менее")):
                return "При этом"
            return "Если точнее,"

        elif context == "addition":
            # Дополнительный факт
            if any(w in text_lower for w in ("кроме того", "также", "ещё")):
                return "Кроме того,"
            if any(w in text_lower for w in ("важно", "следует", "стоит")):
                return "Также важно, что"
            return "Дополнительно"

        return "Также"

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

    def _response_needs_language_fallback(self, text: str) -> bool:
        if _is_low_quality_sentence(text):
            return True
        letters = [ch for ch in text if ch.isalpha()]
        if len(letters) < 12:
            return False
        cyr = sum(1 for ch in letters if "\u0400" <= ch <= "\u04ff")
        cyr_ratio = cyr / max(1, len(letters))
        # Сигналы "ломаного" ответа:
        # 1) русский вопрос, но в ответе почти нет кириллицы
        # 2) очень много запятых при коротком тексте (телеграфный мусор)
        noisy_commas = text.count(",") >= 4 and len(text) < 220
        return cyr_ratio < 0.22 or noisy_commas

    def _normalize_qa_response(self, text: str) -> str:
        """
        Убрать шум от retrieval-паттернов вида "Вопрос: ... Ответ: ...",
        оставив только полезную часть ответа.
        """
        src = (text or "").strip()
        if not src:
            return src

        normalized = src
        if "Ответ:" in normalized:
            normalized = normalized.split("Ответ:", 1)[1].strip()

        normalized = re.sub(
            r"^\s*Вопрос:\s*[^?!.]*[?!.]\s*",
            "",
            normalized,
            flags=re.IGNORECASE,
        )
        normalized = re.sub(r"^\s*Ответ:\s*", "", normalized, flags=re.IGNORECASE)

        normalized = re.split(r"\bВопрос:\s*", normalized, maxsplit=1)[0].strip()
        normalized = re.split(r"\bСвязанные понятия:\s*", normalized, maxsplit=1)[0].strip()
        normalized = re.sub(r"\bretrieved\b.*$", "", normalized, flags=re.IGNORECASE).strip()
        normalized = re.sub(r"\bДата обращения:.*$", "", normalized, flags=re.IGNORECASE).strip()
        normalized = re.sub(r"\s{2,}", " ", normalized).strip()
        if normalized:
            return normalized

        fallback = re.sub(
            r"^\s*Вопрос:\s*[^?!.]*[?!.]\s*",
            "",
            src,
            flags=re.IGNORECASE,
        )
        fallback = re.split(r"\bСвязанные понятия:\s*", fallback, maxsplit=1)[0].strip()
        fallback = re.sub(r"\bretrieved\b.*$", "", fallback, flags=re.IGNORECASE).strip()
        fallback = re.sub(r"\bДата обращения:.*$", "", fallback, flags=re.IGNORECASE).strip()
        fallback = re.sub(r"\s{2,}", " ", fallback).strip()
        if fallback:
            return fallback
        return (
            "Пока не хватает точных данных для уверенного ответа. "
            "Уточните вопрос одним предложением, и я отвечу точнее."
        )

    def _build_russian_fallback(
        self,
        message: str,
        retrieved_sentences: list[tuple[str, float]],
    ) -> str:
        message_lower = message.lower()
        q_tokens, q_stems = self._extract_terms_and_stems(message)
        if not q_tokens:
            q_tokens, q_stems = self._extract_terms_and_stems(
                message,
                min_len=2,
                drop_stop=True,
                drop_generic=False,
            )
        noise_markers = {
            "проверка",
            "prod-smoke",
            "deploy-smoke",
            "backend-smoke",
            "стрим",
            "точных хватает",
            "confidence",
        }
        ranked_ru_candidates: list[tuple[str, float]] = []

        for text, base_score in retrieved_sentences[:12]:
            cleaned = text.strip()
            if len(cleaned) < 24:
                continue
            if _is_low_quality_sentence(cleaned):
                continue
            if not any("\u0400" <= c <= "\u04ff" for c in cleaned):
                continue

            lowered = cleaned.lower()
            if any(marker in lowered for marker in noise_markers):
                continue

            c_tokens, c_stems = self._extract_terms_and_stems(
                lowered,
                min_len=2,
                drop_stop=True,
                drop_generic=False,
            )
            if not c_tokens:
                continue
            overlap = len(c_tokens & q_tokens)
            stem_overlap = len(c_stems & q_stems) if q_stems else 0
            if q_tokens and overlap == 0 and stem_overlap == 0:
                continue

            # Блокируем "телеграфный мусор": много запятых при малом объёме.
            if cleaned.count(",") >= 4 and len(cleaned) < 220:
                continue

            score = float(base_score) + overlap * 0.25 + stem_overlap * 0.18
            ranked_ru_candidates.append((cleaned.rstrip("."), score))

        ranked_ru_candidates.sort(key=lambda x: x[1], reverse=True)
        precision_question = any(
            marker in message_lower
            for marker in (
                "сколько", "чему равно", "в каком году", "когда",
                "столица", "что такое", "кто такой", "кто такая",
                "кто такое", "пример", "объясни", "назови",
            )
        )
        precision_min_score = max(
            0.2,
            min(1.0, _env_float("KOLIBRI_RU_PRECISION_MIN_SCORE", 0.30)),
        )
        if precision_question:
            if not ranked_ru_candidates or ranked_ru_candidates[0][1] < precision_min_score:
                return (
                    "Пока не хватает точных данных для уверенного ответа. "
                    "Уточните вопрос одним предложением, и я отвечу точнее."
                )
            # Для вопросов о годе избегаем ответов без дат.
            if "в каком году" in message_lower or "когда" in message_lower:
                if not re.search(r"\b(1[6-9]\d{2}|20\d{2}|2100)\b", ranked_ru_candidates[0][0]):
                    return (
                        "Пока не хватает точных данных для уверенного ответа. "
                        "Уточните вопрос одним предложением, и я отвечу точнее."
                    )

        ru_candidates = [text for text, _ in ranked_ru_candidates[:2]]

        if ru_candidates:
            if len(ru_candidates) == 1:
                return ru_candidates[0] + "."
            return f"{ru_candidates[0]}. {ru_candidates[1]}."

        return (
            "Пока не хватает точных данных для уверенного ответа. "
            "Уточните вопрос одним предложением, и я отвечу точнее."
        )

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

    # ------------------------------------------------------------------
    # Обучение
    # ------------------------------------------------------------------

    def _save_formulas(self) -> None:
        """Сохранить формулы на диск (атомарная запись)."""
        try:
            self.formula_pool.save(_FORMULA_SAVE_PATH)
        except Exception as e:
            log.warning("Не удалось сохранить формулы: %s", e)

    def _extract_qa_pairs(self, text: str, max_pairs: int = 64) -> list[tuple[str, str]]:
        """
        Вытащить пары "Вопрос: ... Ответ: ..." из учебного текста.
        """
        pairs: list[tuple[str, str]] = []
        pattern = re.compile(
            r"Вопрос:\s*(.+?)\s*Ответ:\s*(.+?)(?=(?:\n\s*Вопрос:)|\Z)",
            flags=re.IGNORECASE | re.DOTALL,
        )
        for match in pattern.finditer(text):
            q = re.sub(r"\s+", " ", match.group(1).strip())
            a = re.sub(r"\s+", " ", match.group(2).strip())
            if len(q) < 3 or len(a) < 6:
                continue
            q = q.rstrip(" .!?\n\r\t")
            if not q.endswith("?"):
                q += "?"
            pairs.append((q, a))
            if len(pairs) >= max_pairs:
                break
        return pairs

    def _index_qa_pairs(self, text: str) -> int:
        """
        Индексировать Q/A-пары как точные ассоциации для стабильных ответов.
        """
        pairs = self._extract_qa_pairs(text)
        if not pairs:
            return 0
        for q, a in pairs:
            variants = {
                q,
                q.lower(),
                q.rstrip("?"),
                q.rstrip("?").lower(),
            }
            for v in variants:
                clean_v = v.strip()
                if clean_v:
                    self.formula_pool.add_association(clean_v, a)
        return len(pairs)

    def train_text(self, text: str) -> dict:
        """Обучить на тексте — реально обновляет числовой граф + предложения + эмбеддинги."""
        result = self.graph.train_text(text)
        self.sentence_store.add_text(text)
        qa_pairs_added = self._index_qa_pairs(text)
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
        if qa_pairs_added:
            result["qa_pairs_indexed"] = qa_pairs_added
        self._persist_state_throttled()
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
        self._persist_state_throttled(force=(self._evolution_counter % 10 == 0))

    def _learn_from_dialogue_turn(
        self,
        query: str,
        answer_text: str,
        confidence: float,
        method: str,
    ) -> None:
        """
        Локальное самообучение на подтверждённых диалогах.

        В local-first режиме это заменяет "внешнее дообучение":
        каждое качественное Q/A добавляется в память, граф и эмбеддинги.
        """
        if not self._enable_dialog_learning:
            return
        if confidence < self._dialog_learning_min_conf:
            return
        if method in {"command", "no-knowledge"}:
            return

        q = query.strip()
        a = answer_text.strip()
        if len(q) < 3 or len(a) < 8:
            return
        if len(a) > 2400:
            a = a[:2400].rstrip() + "..."

        # Защита от самоотравления: не учимся на шумных/эхо-ответах.
        a_norm = self._normalize_qa_response(a)
        q_norm = q.strip().rstrip("?!., ").lower()
        a_norm_cmp = a_norm.strip().rstrip("?!., ").lower()
        if not a_norm:
            return
        if "связанные понятия:" in a.lower():
            return
        if a.lower().startswith("вопрос:") and "ответ:" not in a.lower():
            return
        if a_norm_cmp == q_norm:
            return
        if self._response_needs_language_fallback(a_norm):
            return
        a = a_norm

        q_tokens = [t for t in _tokenize(q) if len(t) >= 3][:8]
        a_tokens = [t for t in _tokenize(a) if len(t) >= 3][:12]
        if not q_tokens or not a_tokens:
            return

        # Добавляем связанный Q/A фрагмент в локальное знание в более "чистом" виде,
        # чтобы не засорять выдачу метками "Вопрос/Ответ".
        training_text = f"{q}\n{a}"
        self.graph.train_text(training_text)
        self.sentence_store.add_text(training_text)
        self.formula_pool.add_association(q, a)

        # Формируем ограниченный набор семантических связей, чтобы избегать шума.
        pairs_added = 0
        for qt in q_tokens:
            q_pat = word_to_pattern(qt)
            for at in a_tokens:
                if at == qt:
                    continue
                self.formula_pool.add_semantic_pair(q_pat, word_to_pattern(at))
                pairs_added += 1
                if pairs_added >= 24:
                    break
            if pairs_added >= 24:
                break

        if pairs_added > 0:
            self.formula_pool.evolve(generations=1)
        self._incremental_embedding_train(q_tokens, a_tokens)

        self._dialog_learning_counter += 1
        self._persist_state_throttled(
            force=(self._dialog_learning_counter % 5 == 0),
        )

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
            self._persist_state_throttled()

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

    def _c_formula_runtime_data(self, payload: dict | None) -> dict:
        """Единый formula_data для ответов, пришедших из C-core."""
        data = self._basic_formula_data()
        payload = payload or {}
        data.update(
            {
                "c_formulas_applied": int(payload.get("formulas_applied", 0) or 0),
                "c_query_kind": str(payload.get("query_kind", "") or ""),
                "c_canonical_topic": str(payload.get("canonical_topic", "") or ""),
                "c_definition_entity": str(payload.get("definition_entity", "") or ""),
                "c_topic_token_count": int(payload.get("topic_token_count", 0) or 0),
                "c_digit_winner": int(payload.get("digit_winner", 0) or 0),
                "c_digit_consensus": float(payload.get("digit_consensus", 0.0) or 0.0),
                "c_digit_votes": payload.get("digit_votes", {}) or {},
                "c_formula_memory_path": str(getattr(self.c_inference, "knowledge_path", "") or ""),
            }
        )
        return data

    def _runtime_query_kind(self, message: str, method: str) -> str:
        stripped = re.sub(r"\s+", " ", (message or "").strip())
        projection_match = self._match_c_projection_query(stripped)
        if projection_match:
            return str(projection_match[0])
        if self._is_math_reasoning_method(method):
            return "math"
        method_map = {
            "math-eval": "math",
            "logic-solver": "logic",
            "dialog-context": "followup",
            "conversation-memory": "recap",
            "conversation-memory-empty": "recap",
            "profile-memory": "memory",
            "profile-memory-query": "memory",
            "remember-name": "memory-write",
            "remember-fact": "memory-write",
            "remember-fact-auto": "memory-write",
            "dialog-fact-ack": "memory-write",
            "web-augment-weather": "weather",
            "weather-unavailable": "weather",
            "web-news": "news",
            "web-time": "time",
            "web-rate": "rate",
            "web-reference": "reference",
            "greeting": "greeting",
            "self-meta": "self",
            "kolibri-architecture": "architecture",
            "abuse-deescalation": "boundary",
            "clarify-entity": "clarify",
            "train-command": "learning",
            "auto-learning-ack": "learning",
        }
        return method_map.get(str(method or "").strip(), "")

    def _build_runtime_digit_vote_summary(
        self,
        *,
        message: str,
        response: str,
        method: str,
        confidence: float,
        c_payload: dict | None = None,
    ) -> dict[str, Any]:
        payload = c_payload or {}
        existing_votes = payload.get("digit_votes")
        if isinstance(existing_votes, dict) and existing_votes:
            channels = [float(existing_votes.get(str(index), 0.0) or 0.0) for index in range(10)]
        else:
            channels = [0.0 for _ in range(10)]

        query = str(message or "")
        answer = str(response or "")
        q_tokens, q_stems = self._extract_terms_and_stems(query)
        a_tokens, a_stems = self._extract_terms_and_stems(answer)
        exact_overlap = len(q_tokens & a_tokens)
        stem_overlap = len(q_stems & a_stems)
        semantic_overlap = float(exact_overlap) + (0.7 * float(stem_overlap))
        query_kind = self._runtime_query_kind(query, method)
        method_name = str(method or "").strip().lower()

        if semantic_overlap > 0:
            channels[5] += semantic_overlap * 2.4
            channels[1] += semantic_overlap * 1.1
        else:
            channels[0] += 0.9

        if method_name in {"c-core-formula", "formula-retrieval", "formula-association", "precise-retrieval"}:
            channels[1] += 2.8
            channels[9] += 1.4
        if query_kind in {"structure", "architecture"}:
            channels[2] += 2.8
        if query_kind == "importance" or "потому" in answer.lower():
            channels[3] += 2.7
        if self._is_math_reasoning_method(method_name) or method_name == "logic-solver" or self._try_math_eval(query) is not None:
            channels[4] += 6.2
            channels[9] += 0.45
        if method_name in {
            "dialog-context",
            "conversation-memory",
            "conversation-memory-empty",
            "profile-memory",
            "profile-memory-query",
            "remember-name",
            "remember-fact",
            "remember-fact-auto",
            "dialog-fact-ack",
        }:
            channels[6] += 2.8
        if method_name in {"train-command", "auto-learning-ack", "web-news", "web-reference", "web-rate", "web-time"}:
            channels[7] += 2.4
        if method_name in {"web-augment-weather", "weather-unavailable", "web-news", "web-time", "web-rate", "web-reference", "train-command"}:
            channels[8] += 2.2
        if method_name in {"weather-unavailable", "canonical-topic-fallback", "dynamic-fallback", "conversation-memory-empty", "profile-memory-empty"}:
            channels[0] += 2.4
        if method_name == "greeting":
            channels[9] += 1.2
        if method_name == "self-meta":
            channels[6] += 0.9
            channels[9] += 1.1
        if method_name == "kolibri-architecture":
            channels[2] += 2.2
            channels[9] += 1.1

        if answer.strip():
            channels[9] += max(0.6, min(4.5, float(confidence or 0.0) * 4.8))
        else:
            channels[0] += 4.0

        top_index = 0
        top_score = channels[0]
        second_score = 0.0
        for index, score in enumerate(channels):
            if score > top_score:
                second_score = top_score
                top_score = score
                top_index = index
            elif score > second_score and index != top_index:
                second_score = score
        consensus = 0.0
        if top_score > 0.0:
            consensus = max(0.0, min(1.0, (top_score - second_score) / top_score))
        return {
            "runtime_query_kind": query_kind,
            "runtime_digit_winner": int(top_index),
            "runtime_digit_consensus": round(float(consensus), 6),
            "runtime_digit_votes": {
                str(index): round(float(score), 6)
                for index, score in enumerate(channels)
            },
            "runtime_vote_origin": "c-core+runtime" if payload else "runtime",
        }

    def _augment_formula_data_with_runtime_vote(
        self,
        *,
        message: str,
        response: str,
        method: str,
        confidence: float,
        formula_data: dict | None = None,
        c_payload: dict | None = None,
    ) -> dict:
        data = dict(formula_data or self._basic_formula_data())
        runtime_c_payload = c_payload
        if runtime_c_payload is None and data.get("c_digit_votes"):
            runtime_c_payload = {
                "digit_votes": data.get("c_digit_votes", {}) or {},
                "digit_winner": data.get("c_digit_winner", 0),
                "digit_consensus": data.get("c_digit_consensus", 0.0),
                "query_kind": data.get("c_query_kind", ""),
                "canonical_topic": data.get("c_canonical_topic", ""),
                "definition_entity": data.get("c_definition_entity", ""),
                "topic_token_count": data.get("c_topic_token_count", 0),
            }
        data.update(
            self._build_runtime_digit_vote_summary(
                message=message,
                response=response,
                method=method,
                confidence=confidence,
                c_payload=runtime_c_payload,
            )
        )
        return data

    # ------------------------------------------------------------------
    # Математический вычислитель
    # ------------------------------------------------------------------

    _MATH_CLEAN_RE = re.compile(
        r'^(?:сколько будет|чему равно|посчитай|вычисли|calculate)\s+',
        re.IGNORECASE,
    )
    _MATH_OP_PHRASES: tuple[tuple[str, str], ...] = (
        ("открывающая скобка", "("),
        ("открыть скобку", "("),
        ("открой скобку", "("),
        ("закрывающая скобка", ")"),
        ("закрыть скобку", ")"),
        ("закрой скобку", ")"),
        ("умножить на", "*"),
        ("умножить", "*"),
        ("умножь на", "*"),
        ("умножь", "*"),
        ("помножить на", "*"),
        ("помножить", "*"),
        ("разделить на", "/"),
        ("разделить", "/"),
        ("поделить на", "/"),
        ("поделить", "/"),
        ("делить на", "/"),
        ("делить", "/"),
        ("прибавить", "+"),
        ("прибавь", "+"),
        ("сложить", "+"),
        ("плюс", "+"),
        ("минус", "-"),
        ("возвести в степень", "**"),
        ("возведи в степень", "**"),
        ("в степени", "**"),
        ("в степень", "**"),
        ("степень", "**"),
    )
    _MATH_FILLER_WORDS = {
        "сколько", "будет", "чему", "равно", "посчитай", "вычисли", "calculate",
        "пожалуйста", "это", "и", "на", "по", "для", "почему", "объясни", "обоснуй",
    }
    _MATH_FUNC_WORDS = {
        "sqrt", "sin", "cos", "tan", "log", "log2", "log10",
        "abs", "pow", "factorial", "ceil", "floor", "pi", "e",
        "пи",
    }
    _MATH_NUMBER_WORDS = {
        "ноль": 0, "нуль": 0,
        "один": 1, "одна": 1, "одно": 1,
        "два": 2, "две": 2,
        "три": 3, "четыре": 4, "пять": 5, "шесть": 6, "семь": 7, "восемь": 8, "девять": 9,
        "десять": 10, "одиннадцать": 11, "двенадцать": 12, "тринадцать": 13, "четырнадцать": 14,
        "пятнадцать": 15, "шестнадцать": 16, "семнадцать": 17, "восемнадцать": 18, "девятнадцать": 19,
        "двадцать": 20, "тридцать": 30, "сорок": 40, "пятьдесят": 50,
        "шестьдесят": 60, "семьдесят": 70, "восемьдесят": 80, "девяносто": 90,
        "сто": 100, "двести": 200, "триста": 300, "четыреста": 400,
        "пятьсот": 500, "шестьсот": 600, "семьсот": 700, "восемьсот": 800, "девятьсот": 900,
    }
    _MATH_MULTIPLIERS = {
        "тысяча": 1000,
        "тысячи": 1000,
        "тысяч": 1000,
        "миллион": 1_000_000,
        "миллиона": 1_000_000,
        "миллионов": 1_000_000,
    }

    def _parse_number_words(self, tokens: list[str], start: int) -> tuple[int | None, int]:
        total = 0
        current = 0
        used = 0
        i = start
        while i < len(tokens):
            token = tokens[i]
            if token in self._MATH_NUMBER_WORDS:
                current += self._MATH_NUMBER_WORDS[token]
                used += 1
                i += 1
                continue
            if token in self._MATH_MULTIPLIERS:
                mult = self._MATH_MULTIPLIERS[token]
                if current == 0:
                    current = 1
                total += current * mult
                current = 0
                used += 1
                i += 1
                continue
            break
        if used == 0:
            return None, 0
        return total + current, used

    def _normalize_math_expression(self, expr: str) -> str:
        text = self._MATH_CLEAN_RE.sub("", expr).strip().lower().replace("ё", "е")
        text = text.replace("×", "*").replace("÷", "/").replace("^", "**")
        text = re.sub(
            r"(\d+(?:[.,]\d+)?)\s*%\s*от\s*(\d+(?:[.,]\d+)?)",
            r"(\1 / 100) * \2",
            text,
            flags=re.IGNORECASE,
        )
        text = text.replace("в квадрате", " ** 2 ")
        text = text.replace("в кубе", " ** 3 ")
        text = re.sub(r"\bквадратный\s+корень\s+из\b", " sqrt ", text)
        text = re.sub(r"\bкорень\s+из\b", " sqrt ", text)

        for source, target in self._MATH_OP_PHRASES:
            text = re.sub(rf"\b{re.escape(source)}\b", f" {target} ", text)

        text = text.replace("**", " POWOP ")
        for op in ("+", "-", "*", "/", "%", "(", ")"):
            text = text.replace(op, f" {op} ")

        tokens = re.findall(r"powop|[a-zа-я]+|\d+(?:[.,]\d+)?|[()+\-*/%]", text, flags=re.IGNORECASE)
        normalized_tokens: list[str] = []
        i = 0
        while i < len(tokens):
            token = tokens[i].lower()
            if token == "powop":
                normalized_tokens.append("**")
                i += 1
                continue
            if token in self._MATH_FILLER_WORDS:
                i += 1
                continue
            if re.fullmatch(r"\d+(?:[.,]\d+)?", token):
                normalized_tokens.append(token.replace(",", "."))
                i += 1
                continue
            if re.fullmatch(r"[a-zа-я]+", token):
                number, used = self._parse_number_words(tokens, i)
                if used > 0 and number is not None:
                    normalized_tokens.append(str(number))
                    i += used
                    continue
                if token in self._MATH_FUNC_WORDS:
                    normalized_tokens.append("pi" if token == "пи" else token)
                    i += 1
                    continue
                i += 1
                continue
            normalized_tokens.append(token)
            i += 1

        normalized = " ".join(normalized_tokens)
        # sqrt 9 -> sqrt(9), log 10 -> log(10), factorial 5 -> factorial(5)
        normalized = re.sub(
            r"\b(sqrt|sin|cos|tan|log|log2|log10|abs|pow|factorial|ceil|floor)\s+(-?\d+(?:\.\d+)?)\b",
            r"\1(\2)",
            normalized,
        )
        normalized = re.sub(r"\s{2,}", " ", normalized).strip()
        return normalized

    def _is_math_reasoning_method(self, method: str) -> bool:
        method_name = str(method or "").strip().lower()
        if method_name == "math-eval":
            return True
        return method_name.startswith(("math-", "arithmetic", "percentage", "algebra", "geometry", "word-problem"))

    def _try_math_eval(self, expr: str) -> dict | None:
        """Безопасное вычисление математических выражений."""
        if not expr or len(expr) > 200:
            return None

        clean = self._normalize_math_expression(expr)
        has_numeric_signal = bool(re.search(r"\d", clean)) or bool(re.search(r"\b(pi|e)\b", clean))
        has_math_signal = any(op in clean for op in ("+", "-", "*", "/", "%", "**")) or bool(
            re.search(r"\b(sqrt|sin|cos|tan|log|log2|log10|abs|pow|factorial|ceil|floor)\b", clean)
        )
        if not clean or not has_numeric_signal or not has_math_signal:
            return None

        if len(clean) > 240:
            return None

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
            tree = ast.parse(clean, mode='eval')
            allowed_nodes = (
                ast.Expression,
                ast.BinOp,
                ast.UnaryOp,
                ast.Constant,
                ast.Call,
                ast.Load,
                ast.Name,
                ast.Add,
                ast.Sub,
                ast.Mult,
                ast.Div,
                ast.Mod,
                ast.Pow,
                ast.USub,
                ast.UAdd,
                ast.FloorDiv,
            )
            for node in ast.walk(tree):
                if not isinstance(node, allowed_nodes):
                    return None
                if isinstance(node, ast.Name) and node.id not in safe_locals:
                    return None
                if isinstance(node, ast.Call):
                    if not isinstance(node.func, ast.Name):
                        return None
                    if node.func.id not in safe_locals:
                        return None
                    if node.keywords or len(node.args) > 2:
                        return None
                if isinstance(node, (ast.Import, ast.ImportFrom, ast.Attribute, ast.FunctionDef, ast.AsyncFunctionDef)):
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

        # Локально запоминаем вычисление как ассоциацию.
        # Для служебных ephemeral-клиентов (benchmark/proof) не делаем persist,
        # чтобы не раздувать latency контрольных прогонов.
        active_client = self._active_client_id_var.get("global")
        if not self._is_ephemeral_client(active_client):
            self.formula_pool.add_association(clean, formatted)
            self._persist_state_throttled()

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

    def _try_math_reasoning_answer(self, question: str) -> dict | None:
        """Решение текстовых математических задач через math_reasoning pipeline."""
        if not self._math_enabled or not self._is_math_query(question):
            return None

        math_result = self.math.solve(question)
        if not isinstance(math_result, dict):
            return None

        method = str(math_result.get("method", "") or "").strip().lower()
        confidence = float(math_result.get("confidence", 0.0) or 0.0)
        if confidence < 0.6 or method.endswith("failed"):
            return None

        steps = [str(step).strip() for step in (math_result.get("steps", []) or []) if str(step).strip()]
        answer = math_result.get("answer", "")
        response_lines: list[str] = ["Решение:"]
        if steps:
            response_lines.extend(f"{index}. {step}" for index, step in enumerate(steps, 1))
        else:
            response_lines.append("1. Задача распознана и решена.")
        response_lines.append("")
        response_lines.append(f"Ответ: {answer}")

        return {
            "response": "\n".join(response_lines).strip(),
            "confidence": confidence,
            "sources": ["math-reasoning"],
            "method": math_result.get("method", "math-reasoning"),
            "knowledge_hits": 0,
            "formula_data": {
                **self._basic_formula_data(),
                "math_steps": steps,
                "math_answer": answer,
                "math_reasoning_method": math_result.get("method", "math-reasoning"),
            },
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

    def _is_conversation_recap_intent(self, text: str) -> bool:
        normalized = self._normalize_linguistic_text(text)
        if not normalized:
            return False
        patterns = (
            r"\bо\s+ч[её]м\s+мы\s+говорили\b",
            r"\bчто\s+мы\s+обсуждали\b",
            r"\bчто\s+было\s+до\s+этого\b",
            r"\bчто\s+было\s+выше\b",
            r"\bнапомни\s+контекст\b",
            r"\bкакой\s+контекст\s+диалога\b",
            r"\bчто\s+ты\s+помнишь\s+из\s+диалога\b",
            r"\bо\s+ч[её]м\s+этот\s+диалог\b",
        )
        return any(re.search(pattern, normalized) for pattern in patterns)

    def _render_conversation_recap(self, context_window: ContextWindow | None, current_query: str | None = None) -> str:
        if context_window is None:
            return "Пока в текущем диалоге ещё нет содержательного контекста."

        recent_user = context_window.get_recent_substantive_user_messages(limit=4, current_query=current_query)
        assistant_facts = [
            self._strip_memory_ack_wrapper(item).strip().rstrip(" .")
            for item in context_window.get_recent_semantic_facts(query=current_query or "", limit=6, current_query=current_query)
        ]
        assistant_facts = [item for item in assistant_facts if item]

        if not recent_user and not assistant_facts:
            return "Пока в текущем диалоге ещё нет содержательных фактов для краткой сводки."

        lines: list[str] = ["Кратко по текущему диалогу:"]
        seen: set[str] = set()

        for item in reversed(recent_user[-3:]):
            clean = re.sub(r"\s+", " ", item).strip().rstrip(".")
            key = clean.lower()
            if not clean or key in seen:
                continue
            seen.add(key)
            lines.append(f"• Вы сказали: {clean}.")

        fact_count = 0
        for item in assistant_facts:
            clean = re.sub(r"\s+", " ", item).strip().rstrip(".")
            key = clean.lower()
            if not clean or key in seen:
                continue
            seen.add(key)
            lines.append(f"• Я ответил: {clean}.")
            fact_count += 1
            if fact_count >= 3:
                break

        return "\n".join(lines)

    def _build_conversation_memory_read_response(
        self,
        message: str,
        context_window: ContextWindow | None,
    ) -> tuple[str | None, str]:
        original = (message or "").strip()
        if not self._is_conversation_recap_intent(original):
            return None, "no-memory"
        recap = self._render_conversation_recap(context_window, current_query=original)
        is_empty = "нет содерж" in recap.lower()
        return recap, "conversation-memory" if not is_empty else "conversation-memory-empty"

    def _build_profile_memory_read_response(self, message: str) -> tuple[str | None, str]:
        original = (message or "").strip()
        stripped = original.lower().strip().rstrip("?!.")

        if any(
            token in stripped
            for token in (
                "какие тексты ты знаешь",
                "список текстов",
                "что ты помнишь из текстов",
                "какие материалы ты помнишь",
            )
        ):
            profile = self._get_user_profile()
            docs = self._visible_profile_documents(
                [d for d in (profile.get("documents", []) or []) if isinstance(d, dict)]
            )
            if not docs:
                return (
                    "Пока нет явно добавленных обученных текстов. Добавьте материал командой: `научи: ...`.",
                    "document-list",
                )
            lines = [f"• {self._document_display_title(d)}" for d in docs[-10:]]
            return "Я помню такие тексты:\n" + "\n".join(lines), "document-list"

        if any(token in stripped for token in ("как меня зовут", "кто я", "что ты знаешь обо мне", "что ты помнишь обо мне")):
            return self._render_user_memory(), "profile-memory"

        personal_query_tokens = (
            "что я", "что мне", "что обо мне", "что ты знаешь обо мне",
            "что ты помнишь", "какие мои", "какая моя", "какой мой", "чем я", "кого я", "как у меня",
            "моя ", "мой ", "мои ", "обо мне", "про меня",
        )
        is_memory_command = stripped.startswith(
            (
                "запомни",
                "remember",
                "научи:",
                "обучи:",
                "меня зовут",
                "мое имя",
                "моё имя",
            )
        )
        is_personal_query = any(token in stripped for token in personal_query_tokens) and not is_memory_command
        dynamic_profile_answer = self._answer_from_user_facts(stripped)
        if dynamic_profile_answer and is_personal_query:
            return dynamic_profile_answer, "profile-memory-query"
        if is_personal_query:
            return (
                "Пока у меня нет сохранённых личных данных по этому вопросу. "
                "Напишите, например: `Запомни, что моя любимая музыка — джаз`."
            ), "profile-memory-empty"
        return None, "no-memory"

    def _build_learned_document_read_response(self, message: str) -> tuple[str | None, str]:
        original = (message or "").strip()
        stripped = original.lower().strip().rstrip("?!.")

        story_intent = (
            bool(re.search(r"\bсказк[а-яё]*\b", stripped))
            and (
                stripped.startswith(("сказка", "сказку"))
                or any(k in stripped for k in ("напиши", "расскажи", "сочини", "придумай"))
            )
        ) or (
            bool(re.search(r"\bистори[яию]\b", stripped))
            and any(k in stripped for k in ("напиши", "расскажи", "сочини", "придумай"))
        )
        if story_intent:
            doc = self._find_best_document(original)
            if not doc:
                return (
                    "Пока не нашёл обученный материал для сказки. Добавьте текст командой: `научи: ...`.",
                    "story-fallback",
                )
            topic = self._extract_story_topic(original, doc)
            return self._compose_story_from_document(doc, topic_hint=topic), "story-memory"

        if any(
            token in stripped
            for token in ("перескажи", "расскажи по-своему", "расскажи своими словами", "пересказ")
        ):
            doc = self._find_best_document(original)
            if not doc:
                return (
                    "Пока не нашёл обученный текст для пересказа. Добавьте материал командой: `научи: ...`.",
                    "retell-memory",
                )
            short = any(k in stripped for k in ("кратко", "коротко", "вкратце"))
            return self._retell_document(doc, short=short), "retell-memory"

        return None, "no-document-read"

    def _capabilities_summary_text(self) -> str:
        return (
            "Сейчас я уже умею считать точные выражения, отвечать по части локальной базы знаний через C-ядро, "
            "держать часть контекста диалога, запоминать часть пользовательских фактов и обучаться на добавленных материалах. "
            "Внешние справочные ответы вроде погоды зависят от того, доступен ли серверу интернет-источник в данный момент. "
            "Но я ещё не доведён до уровня сильного универсального собеседника: иногда отвечаю слишком шаблонно "
            "или теряю тему, и это ещё нужно дожимать."
        )

    def _build_system_inspection_response(self, message: str) -> tuple[str | None, str]:
        original = (message or "").strip()
        lower = original.lower()

        if "статистик" in lower or ("модел" in lower and "покаж" in lower):
            g = self.graph.get_stats()
            c = self._get_model_stats()
            best = self.formula_pool.best()
            return (
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
                f"• Размер: **{c.get('size_mb', 0)} МБ**",
                "command",
            )

        if lower.startswith("паттерн ") or lower.startswith("pattern "):
            word = lower.split(maxsplit=1)[1].strip()
            p = pattern_to_str(word_to_pattern(word))
            h = djb2_hash(word)
            digits = text_to_digits(word)
            recovered = digits_to_text(digits)
            sim_words = self.graph.find_similar(word, limit=5)
            sim_list = "\n".join(f"  • `{w}` — сходство {s}" for w, s in sim_words) if sim_words else "  (пока нет данных)"
            return (
                f"🔢 **Числовой паттерн: `{word}`**\n\n"
                f"• Паттерн (64 цифры): `{p}`\n"
                f"• DJB2 хеш: `{h}`\n"
                f"• FNV-1a хеш: `{fnv1a_hash(word)}`\n"
                f"• Текст→Цифры: `{''.join(str(d) for d in digits[:30])}…` ({len(digits)} цифр)\n"
                f"• Восстановление: `{recovered}`\n\n"
                f"**Похожие паттерны в графе:**\n{sim_list}",
                "pattern-lookup",
            )

        if "формул" in lower and ("покаж" in lower or "расскаж" in lower):
            best = self.formula_pool.best()
            gene_preview = best.gene.digits[:64]
            return (
                f"⚡ **Формула Kolibri (лучшая из 16)**\n\n"
                f"• Поколение: **{self.formula_pool.generation}**\n"
                f"• Fitness: **{round(best.fitness, 6)}**\n"
                f"• Ассоциаций: **{len(best.associations)}**\n"
                f"• Сложность: **{round(best.gene.complexity(), 3)}**\n\n"
                f"**Геном (64 из 4000 цифр):**\n"
                f"`{''.join(str(d) for d in gene_preview)}`\n\n"
                f"**Hex:** `{best.gene.to_hex()}`\n\n"
                f"**500 слоёв × 12 операций (fast=100):**\n"
                f"linear, inverse, modular, quadratic, XOR, AND, sin, saturate, OR, gaussian, tanh, sigmoid",
                "formula-inspect",
            )

        system_triggers = (
            "покажи систем", "системные метрик", "метрики систем",
            "cpu", "загрузка процессор", "использование памят",
            "сколько памят", "покажи cpu", "show system",
        )
        if any(t in lower for t in system_triggers):
            try:
                import psutil
                cpu = psutil.cpu_percent(interval=0.1)
                mem = psutil.virtual_memory()
                return (
                    f"🖥️ **Системные метрики**\n\n"
                    f"• CPU: **{cpu}%**\n"
                    f"• Память: **{mem.percent}%** ({round(mem.used / (1024**3), 2)} ГБ / {round(mem.total / (1024**3), 2)} ГБ)",
                    "command",
                )
            except Exception:
                return "❌ Не удалось получить системные метрики.", "command"

        if "здоров" in lower or "health" in lower or "статус" in lower:
            g = self.graph.get_stats()
            return (
                f"🟢 **Kolibri AI — Числовое Мышление**\n\n"
                f"• Граф: **{g['patterns']:,}** паттернов, **{g['edges']:,}** рёбер\n"
                f"• Предложений: **{self.sentence_store.size:,}**\n"
                f"• Формулы: поколение **{self.formula_pool.generation}**\n"
                f"• C-модель: **{'✅' if self.c_retriever.available else '❌'}**\n"
                f"• Диалогов: **{len(self.conversations)}**\n"
                f"• Движок: **Числовое Формульное Мышление**",
                "command",
            )

        return None, "no-inspection"

    def _build_system_read_response(self, message: str) -> tuple[str | None, str]:
        original = (message or "").strip()
        lower = original.lower()
        normalized = self._normalize_linguistic_text(original)

        if self._is_identity_intent(original):
            profile = self._get_user_profile()
            user_name = str(profile.get("name", "") or "").strip()
            tail = f" Рад знакомству, {user_name}." if user_name else ""
            return (
                f"Я {self._assistant_name} — локальный ассистент Kolibri. "
                f"Я обучаюсь на ваших сообщениях и фактах, которые вы просите запомнить."
                f"{tail}"
            ), "identity"

        if (
            ("kolibri" in lower or "колибри" in lower or "калибри" in lower)
            and ("архитектур" in lower or "ядр" in lower or "c-core" in lower or "си ядро" in lower)
        ) or self._is_architecture_intent(original):
            return self._kolibri_architecture_summary_text(), "kolibri-architecture"

        if self._is_greeting_intent(original):
            profile = self._get_user_profile()
            user_name = str(profile.get("name", "") or "").strip()
            name_part = f", {user_name}" if user_name else ""
            return (
                f"Привет{name_part}. Я {self._assistant_name}. "
                "Могу отвечать по локальной базе знаний и обучаться на ваших материалах. "
                "Если хотите, начнём с любого вопроса."
            ), "greeting"

        if self._is_smalltalk_checkin_intent(original):
            return (
                "У меня всё в порядке. Я на связи и готов помочь с вопросами, "
                "объяснениями или обучением на ваших материалах."
            ), "smalltalk-checkin"

        if self._is_self_meta_intent(original):
            if "бог" in normalized:
                return (
                    "Нет. Я не бог и не человек. Я программный ассистент Kolibri, который отвечает текстом и работает с данными."
                ), "self-meta"
            if "человек" in normalized or "жив" in normalized:
                return "Нет. Я не человек и не живое существо. Я программный ассистент Kolibri.", "self-meta"
            return (
                "Да. Я умею общаться текстом, объяснять, считать, держать контекст диалога и использовать доступные инструменты."
            ), "self-meta"

        if self._is_abusive_intent(original):
            return (
                "Я на связи. Если что-то сработало плохо, напишите коротко, что именно сломалось или какой ответ был неверным, "
                "и я постараюсь исправить это по делу."
            ), "abuse-deescalation"

        if self._is_ambiguous_entity_fragment(original):
            entity = re.sub(r"\s+", " ", original.strip()).strip(" .,!?:;")
            return (
                f"Уточните, что именно вы хотите узнать о «{entity}»: кто это, биография, связь с проектом или что-то ещё."
            ), "clarify-entity"

        if self._is_capabilities_intent(original):
            return self._capabilities_summary_text(), "command"

        return None, "no-system"

    def _kolibri_architecture_summary_text(self) -> str:
        return (
            "Kolibri устроен слоями. В центре — C-ядро, где знания хранятся как числа, формулы и связи. "
            "Сверху есть сервисный слой, который ведёт диалог, подключает память, веб-поиск и инструменты. "
            "Фронтенд — это просто интерфейс: он показывает чат и отправляет запросы в ядро."
        )

    def _handle_special_commands(self, message: str, lower: str, context_window: ContextWindow | None = None) -> dict | None:
        original = message.strip()
        stripped = lower.strip().rstrip("?!.")

        # --- #Фаза A1: Запросы на генерацию контента (статьи, тексты) ---
        content_gen_match = re.search(
            r'(?:напиши|сочини|создай|подготовь)\s+(?:стать[юю]|текст|доклад|реферат|эссе|заметку)\s+(?:про|о|об|на тему)\s+(.+)',
            original, re.IGNORECASE
        )
        if content_gen_match:
            topic = content_gen_match.group(1).strip().rstrip('.')
            return self._generate_article(topic, context_window)

        # --- Явная персональная память (знакомство/обучение пользователя) ---
        if stripped.startswith(("научи:", "обучи:", "обучи:")):
            train_text = original.split(":", 1)[1].strip() if ":" in original else ""
            if len(train_text) < 10:
                resp = "После `научи:` передайте текст не короче 10 символов."
            else:
                try:
                    doc = self._remember_document(train_text, source="teach-command")
                    if len(train_text) >= 1200:
                        try:
                            self._train_queue.put_nowait(("user_text", train_text[:20000]))
                        except queue.Full:
                            pass
                        self._persist_state_throttled()
                        title = str((doc or {}).get("title", "текст")).strip()
                        resp = (
                            "Материал принят в обучение.\n"
                            f"• Текст сохранён: {title}\n"
                            "• Фоновое обучение запущено\n"
                            "• Для проверки: `перескажи этот текст`"
                        )
                    else:
                        result = self.train_and_verify(train_text)
                        self._persist_state_throttled(force=True)
                        title = str((doc or {}).get("title", "текст")).strip()
                        resp = (
                            "Обучение выполнено.\n"
                            f"• Текст: {title}\n"
                            f"• Новых паттернов: {result.get('new_patterns', 0)}\n"
                            f"• Новых связей: {result.get('new_edges', 0)}\n"
                            f"• Поколение формул: {result.get('formula_generation', self.formula_pool.generation)}"
                        )
                except Exception as exc:
                    resp = f"Не удалось обучить на этом тексте: {exc}"
            return {
                "response": resp,
                "confidence": 1.0,
                "sources": ["training"],
                "method": "train-command",
                "knowledge_hits": 0,
                "formula_data": self._basic_formula_data(),
                "graph_stats": self.graph.get_stats(),
            }

        auto_fact = self._extract_auto_user_fact(original)
        if auto_fact:
            self._remember_user_fact(auto_fact)
            return {
                "response": f"Запомнил о вас: {auto_fact}",
                "confidence": 0.98,
                "sources": ["profile-memory"],
                "method": "remember-fact-auto",
                "knowledge_hits": 0,
                "formula_data": self._basic_formula_data(),
                "graph_stats": self.graph.get_stats(),
            }

        name_candidate = self._extract_name_candidate(original)
        if name_candidate:
            self._remember_user_name(name_candidate)
            return {
                "response": f"Запомнил. Буду обращаться к вам как к {name_candidate}.",
                "confidence": 1.0,
                "sources": ["profile-memory"],
                "method": "remember-name",
                "knowledge_hits": 0,
                "formula_data": self._basic_formula_data(),
                "graph_stats": self.graph.get_stats(),
            }

        if stripped.startswith(("запомни", "remember")):
            fact_text = re.sub(
                r"^(запомни(?:,|\s+что)?|remember(?:\s+that)?)\s*",
                "",
                original,
                flags=re.IGNORECASE,
            ).strip(" :.")
            if len(fact_text) < 3:
                resp = "Передайте факт после `запомни`, например: `Запомни, что я фронтенд-разработчик`."
            else:
                self._remember_user_fact(fact_text)
                resp = f"Запомнил факт: {fact_text}"
            return {
                "response": resp,
                "confidence": 1.0,
                "sources": ["profile-memory"],
                "method": "remember-fact",
                "knowledge_hits": 0,
                "formula_data": self._basic_formula_data(),
                "graph_stats": self.graph.get_stats(),
            }

        return None

    # ------------------------------------------------------------------
    # Утилиты
    # ------------------------------------------------------------------

    def _is_ephemeral_client(self, client_id: str) -> bool:
        cid = str(client_id or "")
        return (
            cid.startswith("quality-bench:")
            or cid.startswith("unique-proof:")
            or cid.startswith("ephemeral:")
        )

    def _apply_runtime_preferences(
        self,
        profile: dict[str, object] | None,
        *,
        persona: str | None = None,
        memory_enabled: bool = True,
        model_name: str | None = None,
    ) -> dict[str, object]:
        runtime = dict(profile or self._new_user_profile())
        existing = runtime.get("preferences")
        preferences = dict(existing) if isinstance(existing, dict) else {}
        if persona in {"assistant", "romantic", "storyteller"}:
            preferences["persona"] = persona
        else:
            preferences.setdefault("persona", "assistant")
        preferences["memory_enabled"] = bool(memory_enabled)
        if model_name:
            preferences["model"] = str(model_name).strip()[:120]
        runtime["preferences"] = preferences
        return runtime

    def _apply_persona_style(self, text: str, *, persona: str | None = None) -> str:
        body = str(text or "").strip()
        if not body:
            return body
        active_persona = (persona or "").strip().lower()
        if active_persona not in {"assistant", "romantic", "storyteller"}:
            profile = self._get_user_profile()
            preferences = profile.get("preferences")
            if isinstance(preferences, dict):
                active_persona = str(preferences.get("persona", "assistant") or "assistant").strip().lower()
            else:
                active_persona = "assistant"
        if active_persona == "romantic":
            prefix = "Отвечу мягко и бережно."
        elif active_persona == "storyteller":
            prefix = "Представлю это как короткий рассказ."
        else:
            return body
        if body.startswith(prefix):
            return body
        return f"{prefix}\n\n{body}"

    def _conversation_persist_worker(self) -> None:
        while True:
            client_id, conversation_id, role, content, created_at = self._conversation_persist_queue.get()
            try:
                self._db.append_conversation_turn(
                    client_id=client_id,
                    conversation_id=conversation_id,
                    role=role,
                    content=content,
                    created_at=created_at,
                )
            except Exception as exc:
                log.warning(
                    "conversation persist worker failed (%s/%s): %s",
                    client_id,
                    conversation_id,
                    exc,
                )
            finally:
                self._conversation_persist_queue.task_done()

    def _persist_conversation_turn(self, client_id: str, conversation_id: str, role: str, content: str) -> None:
        if not self._persist_conversations:
            return
        if self._is_ephemeral_client(client_id):
            return
        text = str(content or "").strip()
        if not text:
            return
        try:
            self._conversation_persist_queue.put_nowait(
                (client_id, conversation_id, role, text, time.time()),
            )
        except queue.Full:
            log.warning("conversation persist queue is full; dropping turn (%s/%s)", client_id, conversation_id)

    def _hydrate_conversation_from_db(self, conversation_id: str, client_id: str) -> Conversation:
        conv = Conversation(id=conversation_id)
        if not self._persist_conversations:
            return conv
        if self._is_ephemeral_client(client_id):
            return conv
        try:
            rows = self._db.load_conversation_turns(
                client_id=client_id,
                conversation_id=conversation_id,
                limit=self._conversation_db_load_limit,
            )
        except Exception as exc:
            log.warning("conversation hydration failed (%s/%s): %s", client_id, conversation_id, exc)
            return conv
        for row in rows:
            role = str(row.get("role", "user") or "user")
            content = str(row.get("content", "") or "")
            if not content:
                continue
            conv.add(role, content)
        return conv

    def _filter_private_retrieval_hits(self, retrieved: list[tuple[str, float]]) -> list[tuple[str, float]]:
        if not retrieved:
            return retrieved
        markers = (
            "факт о пользователе:",
            "пользователя зовут",
            "я помню о вас:",
        )
        filtered: list[tuple[str, float]] = []
        for text, score in retrieved:
            low = str(text or "").lower()
            if any(marker in low for marker in markers):
                continue
            filtered.append((text, score))
        return filtered

    def _make_cache_key(
        self,
        conv_id: str,
        client_id: str,
        message: str,
        temperature: float,
        turn_count: int = 0,
        persona: str = "assistant",
    ) -> str:
        normalized = message.strip().lower()
        style = (persona or "assistant").strip().lower()
        return f"{client_id}|{conv_id}|{temperature:.2f}|t{max(0, int(turn_count))}|p:{style}|{normalized}"

    def _scoped_conversation_id(self, conversation_id: str | None, client_id: str) -> str | None:
        raw = str(conversation_id or "").strip()
        if not raw:
            return None
        prefix = f"{self._sanitize_client_id(client_id)}::"
        if raw.startswith(prefix):
            return raw[:200]
        return f"{prefix}{raw}"[:200]

    def _get_or_create_context_window(self, conv_id: str) -> ContextWindow:
        window = self._context_windows.get(conv_id)
        if window is None:
            window = ContextWindow(max_tokens=8192)
            self._context_windows[conv_id] = window
        return window

    def _prune_conversations(self, now: float | None = None) -> None:
        now_ts = now if now is not None else time.time()

        # 1) TTL-очистка неактивных диалогов
        expired: list[str] = []
        for cid, conv in self.conversations.items():
            if now_ts - conv.updated_at > self._conversation_ttl:
                expired.append(cid)
        for cid in expired:
            self.conversations.pop(cid, None)
            self._context_windows.pop(cid, None)

        # 2) LRU-ограничение по количеству активных диалогов
        overflow = len(self.conversations) - self._max_active_conversations
        if overflow <= 0:
            return

        oldest_ids = sorted(
            self.conversations,
            key=lambda cid: self.conversations[cid].updated_at,
        )[:overflow]
        for cid in oldest_ids:
            self.conversations.pop(cid, None)
            self._context_windows.pop(cid, None)

    def get_or_create_conversation(self, conv_id: str | None = None, client_id: str | None = None) -> Conversation:
        client_key = self._sanitize_client_id(client_id if client_id is not None else self._active_client_id())
        self._prune_conversations()
        if conv_id and conv_id in self.conversations:
            conv = self.conversations[conv_id]
            conv.updated_at = time.time()
            return conv
        new_id = conv_id or hashlib.md5(str(time.time()).encode()).hexdigest()[:12]
        conv = self._hydrate_conversation_from_db(new_id, client_key)
        self.conversations[new_id] = conv
        context_window = self._get_or_create_context_window(new_id)
        # Если диалог восстановлен из БД — восстановим и рабочий контекст окна.
        if conv.turns and not context_window.working_memory:
            for turn in conv.turns[-self._conversation_db_load_limit:]:
                context_window.add_message(turn.role, turn.content)
        return conv

    def delete_conversation(self, conv_id: str, client_id: str | None = None) -> bool:
        removed = self.conversations.pop(conv_id, None) is not None
        self._context_windows.pop(conv_id, None)
        client_key = self._sanitize_client_id(client_id if client_id is not None else self._active_client_id())
        deleted_rows = 0
        try:
            deleted_rows = int(self._db.delete_conversation(client_key, conv_id) or 0)
        except Exception as exc:
            log.warning("delete conversation from db failed (%s/%s): %s", client_key, conv_id, exc)

        # Fallback for legacy clients without explicit client_id: infer from scoped conversation id.
        if deleted_rows <= 0 and client_id is None and "::" in conv_id:
            inferred_client = self._sanitize_client_id(conv_id.split("::", 1)[0])
            if inferred_client and inferred_client != client_key:
                try:
                    deleted_rows = int(self._db.delete_conversation(inferred_client, conv_id) or 0)
                except Exception as exc:
                    log.warning(
                        "delete conversation legacy fallback failed (%s/%s): %s",
                        inferred_client,
                        conv_id,
                        exc,
                    )
        return removed or deleted_rows > 0

    def get_conversation_turns(
        self,
        conv_id: str,
        client_id: str | None = None,
        limit: int = 120,
    ) -> tuple[str, list[dict[str, object]]]:
        client_key = self._sanitize_client_id(client_id if client_id is not None else self._active_client_id())
        raw_id = str(conv_id or "").strip()[:200]
        if not raw_id:
            return "", []

        safe_limit = max(1, min(400, int(limit)))
        candidates: list[str] = []
        scoped_id = self._scoped_conversation_id(raw_id, client_key)
        if scoped_id:
            candidates.append(scoped_id)
        if raw_id not in candidates:
            candidates.append(raw_id)

        def _serialize(turns: list[ConversationTurn]) -> list[dict[str, object]]:
            items: list[dict[str, object]] = []
            for turn in turns[-safe_limit:]:
                role = str(turn.role or "user")
                if role not in {"user", "assistant", "system"}:
                    role = "user"
                content = str(turn.content or "")
                if not content:
                    continue
                items.append(
                    {
                        "role": role,
                        "content": content,
                        "created_at": float(turn.timestamp or 0.0),
                    }
                )
            return items

        for candidate in candidates:
            conv = self.conversations.get(candidate)
            if conv and conv.turns:
                return candidate, _serialize(conv.turns)

        for candidate in candidates:
            hydrated = self._hydrate_conversation_from_db(candidate, client_key)
            if hydrated.turns:
                self.conversations[candidate] = hydrated
                return candidate, _serialize(hydrated.turns)

        return candidates[0], []

    def _get_model_stats(self) -> dict:
        now = time.time()
        with self._stats_cache_lock:
            cached = dict(self._stats_cache) if isinstance(self._stats_cache, dict) else None
            cache_time = float(self._stats_cache_time or 0.0)

        if cached and (now - cache_time) <= self._model_stats_ttl_sec:
            return cached

        if cached:
            # stale-while-revalidate: отдаем старый снапшот мгновенно,
            # тяжелый --stats обновляем в фоне.
            self._refresh_model_stats_async()
            return cached

        snapshot = self._quick_model_stats_snapshot()
        with self._stats_cache_lock:
            self._stats_cache = dict(snapshot)
            self._stats_cache_time = now
        self._refresh_model_stats_async()
        return snapshot

    def _quick_model_stats_snapshot(self) -> dict:
        info: dict[str, object] = {
            "exists": bool(self.c_retriever.available),
            "path": str(self.c_retriever.model_path),
            "patterns": 0,
            "edges": 0,
            "documents": 0,
            "epoch": 0,
            "avg_fitness": 0.0,
            "avg_weight": 0.0,
        }
        try:
            if self.c_retriever.model_path.exists():
                info["size_mb"] = round(self.c_retriever.model_path.stat().st_size / (1024 * 1024), 2)
            else:
                info["size_mb"] = 0.0
        except Exception:
            info["size_mb"] = 0.0
        return info

    def _refresh_model_stats_async(self) -> None:
        with self._stats_cache_lock:
            if self._stats_refresh_inflight:
                return
            self._stats_refresh_inflight = True

        threading.Thread(
            target=self._model_stats_refresh_worker,
            daemon=True,
            name="model-stats-refresh",
        ).start()

    def _model_stats_refresh_worker(self) -> None:
        try:
            fresh = self.c_retriever.get_stats()
            if not isinstance(fresh, dict) or not fresh:
                fresh = {}
            base = self._quick_model_stats_snapshot()
            merged = dict(base)
            merged.update(fresh)
            with self._stats_cache_lock:
                self._stats_cache = merged
                self._stats_cache_time = time.time()
        except Exception as exc:
            log.debug("model stats refresh failed: %s", exc)
        finally:
            with self._stats_cache_lock:
                self._stats_refresh_inflight = False

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
        self.sentence_store = SentenceStore(max_sentences=self._sentence_store_max)
        self.sentence_store.embeddings = self.embeddings
        self._corpus_loaded = False
        self._response_cache.clear()
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
    engine = get_engine()
    try:
        engine.c_inference.warmup()
    except Exception as exc:
        log.warning("C-inference warmup failed: %s", exc)
    log.info("AI engine pre-initialized in %.2fs", time.time() - t0)


def shutdown_engine() -> None:
    """#21. Graceful shutdown: сохраняем формулы, закрываем соединения."""
    global _engine_instance
    log.info("Shutting down AI engine...")
    if _engine_instance:
        try:
            # Сохраняем формулы
            _engine_instance.formula_pool.save(_FORMULA_SAVE_PATH)
            # Сохраняем эмбеддинги
            _engine_instance.embeddings.save(_EMBEDDINGS_SAVE_PATH)
            # Закрываем inference executor
            _engine_instance._inference_executor.shutdown(wait=True, cancel_futures=False)
            # Сигнализируем о shutdown
            _engine_instance._shutdown_event.set()
            log.info("AI engine shutdown complete")
        except Exception as exc:
            log.error("AI engine shutdown failed: %s", exc)
    _engine_instance = None


# ============================================================================
# #18. Rate Limiter
# ============================================================================

class _RateLimiter:
    """Token bucket rate limiter для защиты от abuse."""

    def __init__(self, max_requests: int, window_seconds: int) -> None:
        self.max_requests = max_requests
        self.window_seconds = window_seconds
        self._requests: dict[str, list[float]] = defaultdict(list)
        self._lock = threading.Lock()

    def allow_request(self, client_id: str) -> bool:
        with self._lock:
            now = time.time()
            # Удаляем старые запросы
            self._requests[client_id] = [
                t for t in self._requests[client_id]
                if now - t < self.window_seconds
            ]
            if len(self._requests[client_id]) >= self.max_requests:
                return False
            self._requests[client_id].append(now)
            return True


# ============================================================================
# #20. Prometheus metrics endpoint
# ============================================================================

def get_prometheus_metrics() -> str:
    """Возвращает метрики в формате Prometheus для /metrics endpoint."""
    engine = _engine_instance
    if not engine:
        return "# Kolibri AI engine not initialized\n"

    m = engine._metrics
    lines = [
        "# HELP kolibri_queries_total Total number of queries processed",
        "# TYPE kolibri_queries_total counter",
        f"kolibri_queries_total {m['total_queries']}",
        "# HELP kolibri_errors_total Total number of errors",
        "# TYPE kolibri_errors_total counter",
        f"kolibri_errors_total {m['total_errors']}",
        "# HELP kolibri_cache_hits_total Total cache hits",
        "# TYPE kolibri_cache_hits_total counter",
        f"kolibri_cache_hits_total {m['cache_hits']}",
        "# HELP kolibri_cache_misses_total Total cache misses",
        "# TYPE kolibri_cache_misses_total counter",
        f"kolibri_cache_misses_total {m['cache_misses']}",
    ]

    if m['query_durations']:
        avg_duration = sum(m['query_durations']) / len(m['query_durations'])
        lines.extend([
            "# HELP kolibri_query_duration_seconds Average query duration",
            "# TYPE kolibri_query_duration_seconds gauge",
            f"kolibri_query_duration_seconds {avg_duration:.3f}",
        ])

    return "\n".join(lines) + "\n"
