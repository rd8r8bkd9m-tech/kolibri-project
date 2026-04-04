"""
continuous_learning_daemon.py — Непрерывный фоновый демон обучения Колибри

Объединяет ВСЕ подсистемы обучения в единый цикл:
1. Краулинг новых источников (Википедия, ArXiv, RSS, HackerNews)
2. Corpus Trainer → обновление .klm модели
3. Эволюция формул → улучшение геномов
4. World Model → SPSA-обучение на новых данных
5. Knowledge Graph → новые рёбра из текстов
6. Embeddings → тренировка word2vec
7. Dialogue Learning → извлечение знаний из разговоров
8. Self-Improvement → анализ ошибок и улучшение ответов

Цикл работает непрерывно с configurable priority:
- HIGH: краулинг + corpus training
- MEDIUM: эволюция формул + embeddings
- LOW: world model + curiosity + evolution
- IDLE: анализ диалогов + self-improvement
"""
from __future__ import annotations

import asyncio
import hashlib
import json
import logging
import os
import random
import re
import threading
import time
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any, Callable, Optional
from urllib.parse import urlparse

from .background_learning import BackgroundLearningManager, ingest_urls_via_trainer
from .number_mind import (
    KnowledgeGraph,
    FormulaPool,
    word_to_pattern,
    _tokenize,
    _split_sentences,
)
from .project_paths import get_project_root
from .realtime_lookup import external_network_available

log = logging.getLogger("kolibri.continuous_learning")


# ============================================================================
# Конфигурация
# ============================================================================

def _env_flag(name: str, default: bool = False) -> bool:
    value = os.environ.get(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def _env_int(name: str, default: int) -> int:
    value = os.environ.get(name)
    if value is None:
        return default
    try:
        return int(value.strip())
    except (TypeError, ValueError):
        return default


def _env_float(name: str, default: float) -> float:
    value = os.environ.get(name)
    if value is None:
        return default
    try:
        return float(value.strip())
    except (TypeError, ValueError):
        return default


class LearningPriority(Enum):
    HIGH = "high"
    MEDIUM = "medium"
    LOW = "low"
    IDLE = "idle"


@dataclass
class LearningTask:
    name: str
    priority: LearningPriority
    fn: Callable
    weight: float = 1.0
    last_run: float = 0.0
    run_count: int = 0
    total_time: float = 0.0
    last_result: dict = field(default_factory=dict)
    error_count: int = 0
    last_error: str = ""


@dataclass
class LearningMetrics:
    """Глобальные метрики обучения."""
    total_cycles: int = 0
    total_tasks_executed: int = 0
    total_errors: int = 0
    total_uptime: float = 0.0
    started_at: float = 0.0
    last_cycle_at: float = 0.0

    # Corpus
    corpus_patterns: int = 0
    corpus_edges: int = 0
    corpus_documents: int = 0
    corpus_tokens: int = 0

    # Formulas
    formula_pool_size: int = 0
    formula_best_fitness: float = 0.0
    formula_evolution_count: int = 0

    # World Model
    world_model_loss: float = 0.0
    world_model_concepts: int = 0
    world_model_surprise: float = 0.0

    # Embeddings
    embedding_vocab_size: int = 0
    embedding_loss: float = 0.0

    # Dialogue
    dialogues_processed: int = 0
    facts_extracted: int = 0
    knowledge_from_dialogues: int = 0

    # Curriculum
    curriculum_level: int = 0
    curriculum_source: str = ""

    def to_dict(self) -> dict:
        return {
            "total_cycles": self.total_cycles,
            "total_tasks_executed": self.total_tasks_executed,
            "total_errors": self.total_errors,
            "total_uptime": round(self.total_uptime, 1),
            "started_at": self.started_at,
            "last_cycle_at": self.last_cycle_at,
            "corpus": {
                "patterns": self.corpus_patterns,
                "edges": self.corpus_edges,
                "documents": self.corpus_documents,
                "tokens": self.corpus_tokens,
            },
            "formulas": {
                "pool_size": self.formula_pool_size,
                "best_fitness": round(self.formula_best_fitness, 6),
                "evolution_count": self.formula_evolution_count,
            },
            "world_model": {
                "loss": round(self.world_model_loss, 4),
                "concepts": self.world_model_concepts,
                "surprise": round(self.world_model_surprise, 4),
            },
            "embeddings": {
                "vocab_size": self.embedding_vocab_size,
                "loss": round(self.embedding_loss, 6),
            },
            "dialogue": {
                "processed": self.dialogues_processed,
                "facts_extracted": self.facts_extracted,
                "knowledge_created": self.knowledge_from_dialogues,
            },
            "curriculum": {
                "level": self.curriculum_level,
                "source": self.curriculum_source,
            },
        }


# ============================================================================
# Источники данных
# ============================================================================

# Расширенные источники: Wikipedia, ArXiv, HackerNews, RSS, Project Gutenberg
EXPANDED_SOURCES: list[dict[str, Any]] = [
    # --- Wikipedia (русский) — массовый дамп ---
    {
        "id": "wiki-ru-ai",
        "url": "https://ru.wikipedia.org/wiki/%D0%98%D1%81%D0%BA%D1%83%D1%81%D1%81%D1%82%D0%B2%D0%BE%D0%BD%D0%BD%D1%8B%D0%B9_%D0%B8%D0%BD%D1%82%D0%B5%D0%BB%D0%BB%D0%B5%D0%BA%D1%82",
        "title": "Искусственный интеллект",
        "domain": "ai",
        "enabled": True,
        "crawl": True,
        "depth": 2,
        "max_pages": 50,
        "delay_sec": 0.2,
        "priority": "high",
    },
    {
        "id": "wiki-ru-math",
        "url": "https://ru.wikipedia.org/wiki/%D0%9C%D0%B0%D1%82%D0%B5%D0%BC%D0%B0%D1%82%D0%B8%D0%BA%D0%B0",
        "title": "Математика",
        "domain": "math",
        "enabled": True,
        "crawl": True,
        "depth": 2,
        "max_pages": 50,
        "delay_sec": 0.2,
        "priority": "high",
    },
    {
        "id": "wiki-ru-physics",
        "url": "https://ru.wikipedia.org/wiki/%D0%A4%D0%B8%D0%B7%D0%B8%D0%BA%D0%B0",
        "title": "Физика",
        "domain": "physics",
        "enabled": True,
        "crawl": True,
        "depth": 2,
        "max_pages": 50,
        "delay_sec": 0.2,
        "priority": "high",
    },
    {
        "id": "wiki-ru-biology",
        "url": "https://ru.wikipedia.org/wiki/%D0%91%D0%B8%D0%BE%D0%BB%D0%BE%D0%B3%D0%B8%D1%8F",
        "title": "Биология",
        "domain": "biology",
        "enabled": True,
        "crawl": True,
        "depth": 2,
        "max_pages": 50,
        "delay_sec": 0.2,
        "priority": "high",
    },
    {
        "id": "wiki-ru-programming",
        "url": "https://ru.wikipedia.org/wiki/%D0%9F%D1%80%D0%BE%D0%B3%D1%80%D0%B0%D0%BC%D0%BC%D0%B8%D1%80%D0%BE%D0%B2%D0%B0%D0%BD%D0%B8%D0%B5",
        "title": "Программирование",
        "domain": "programming",
        "enabled": True,
        "crawl": True,
        "depth": 2,
        "max_pages": 50,
        "delay_sec": 0.2,
        "priority": "high",
    },
    {
        "id": "wiki-ru-history",
        "url": "https://ru.wikipedia.org/wiki/%D0%98%D1%81%D1%82%D0%BE%D1%80%D0%B8%D1%8F",
        "title": "История",
        "domain": "history",
        "enabled": True,
        "crawl": True,
        "depth": 2,
        "max_pages": 50,
        "delay_sec": 0.2,
        "priority": "high",
    },
    # --- HackerNews API ---
    {
        "id": "hn-top-stories",
        "url": "https://hacker-news.firebaseio.com/v0/topstories.json",
        "title": "HackerNews Top Stories",
        "domain": "tech",
        "enabled": True,
        "crawl": False,
        "depth": 1,
        "max_pages": 30,
        "delay_sec": 0.5,
        "priority": "medium",
    },
    # --- ArXiv (abstracts) ---
    {
        "id": "arxiv-cs-ai",
        "url": "https://export.arxiv.org/api/query?search_query=cat:cs.AI&max_results=50&sortBy=submittedDate",
        "title": "ArXiv CS.AI Papers",
        "domain": "research",
        "enabled": True,
        "crawl": False,
        "depth": 1,
        "max_pages": 50,
        "delay_sec": 1.0,
        "priority": "medium",
    },
]


# ============================================================================
# Curriculum Learning — автоматическое определение сложности
# ============================================================================

CURRICULUM_LEVELS: list[dict[str, Any]] = [
    {
        "level": 0,
        "name": "simple",
        "description": "Простые тексты: детские энциклопедии, определения",
        "sources": ["dal", "simple_wiki", "definitions"],
        "max_sentence_len": 30,
        "max_word_freq": 5000,
    },
    {
        "level": 1,
        "name": "medium",
        "description": "Средние тексты: статьи Википедии, документация",
        "sources": ["wikipedia", "docs", "tutorials"],
        "max_sentence_len": 60,
        "max_word_freq": 20000,
    },
    {
        "level": 2,
        "name": "hard",
        "description": "Сложные тексты: научные статьи, технические документы",
        "sources": ["arxiv", "research", "papers"],
        "max_sentence_len": 120,
        "max_word_freq": 100000,
    },
]


def estimate_text_complexity(text: str) -> int:
    """Оценивает сложность текста (0=simple, 1=medium, 2=hard)."""
    sentences = _split_sentences(text)
    if not sentences:
        return 0

    avg_len = sum(len(s.split()) for s in sentences) / max(1, len(sentences))
    unique_words = len(set(_tokenize(text)))
    total_words = max(1, len(_tokenize(text)))
    lexical_diversity = unique_words / total_words

    score = 0
    if avg_len > 20:
        score += 1
    if avg_len > 40:
        score += 1
    if lexical_diversity > 0.7:
        score += 1
    if lexical_diversity > 0.85:
        score += 1

    if score >= 3:
        return 2
    elif score >= 1:
        return 1
    return 0


# ============================================================================
# Извлечение фактов из диалогов
# ============================================================================

# Паттерны для извлечения фактов из разговоров
FACT_PATTERNS = [
    # "X — это Y"
    re.compile(r"(.{3,50})\s+—?\s+это\s+(.{3,100})[.\s]", re.IGNORECASE),
    # "X называется Y"
    re.compile(r"(.{3,50})\s+называется\s+(.{3,100})[.\s]", re.IGNORECASE),
    # "X является Y"
    re.compile(r"(.{3,50})\s+является\s+(.{3,100})[.\s]", re.IGNORECASE),
    # "X был создан Y"
    re.compile(r"(.{3,50})\s+бы[ллоаи]\s+создан[аыо]?\s+(.{3,100})[.\s]", re.IGNORECASE),
    # "X родился в Y"
    re.compile(r"(.{3,50})\s+родил[сясь]\s+в\s+(.{3,100})[.\s]", re.IGNORECASE),
    # "X находится в Y"
    re.compile(r"(.{3,50})\s+находится\s+в\s+(.{3,100})[.\s]", re.IGNORECASE),
    # "X состоит из Y"
    re.compile(r"(.{3,50})\s+состоит\s+из\s+(.{3,100})[.\s]", re.IGNORECASE),
    # "X включает Y"
    re.compile(r"(.{3,50})\s+включает?\s+(.{3,100})[.\s]", re.IGNORECASE),
]


def extract_facts_from_text(text: str) -> list[dict[str, str]]:
    """Извлекает факты из текста для добавления в граф знаний."""
    facts: list[dict[str, str]] = []
    for pattern in FACT_PATTERNS:
        for match in pattern.finditer(text[:5000]):  # Ограничиваем длину
            subject = match.group(1).strip()
            predicate = match.group(2).strip()
            if len(subject) >= 2 and len(predicate) >= 2:
                facts.append({
                    "subject": subject,
                    "predicate": predicate,
                    "source": "dialogue",
                    "pattern": pattern.pattern[:50],
                })
    return facts


# ============================================================================
# Главный демон
# ============================================================================

class ContinuousLearningDaemon:
    """
    Непрерывный фоновый демон обучения.

    Объединяет ВСЕ подсистемы:
    - BackgroundLearningManager (краулинг URL)
    - Corpus Trainer (.klm модели)
    - Formula Pool (эволюция геномов)
    - World Model (SPSA-обучение)
    - Embeddings (word2vec)
    - Dialogue Learning (извлечение фактов)
    - Self-Improvement (анализ ошибок)
    """

    def __init__(
        self,
        graph: Optional[KnowledgeGraph] = None,
        formula_pool: Optional[FormulaPool] = None,
        background_manager: Optional[BackgroundLearningManager] = None,
    ):
        self._lock = threading.RLock()
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self._running = False
        self._enabled = _env_flag("KOLIBRI_ENABLE_CONTINUOUS_LEARNING", default=True)

        # Cycle config
        self._cycle_interval_sec = max(30, _env_int("KOLIBRI_CONTINUOUS_LEARNING_INTERVAL", 120))
        self._max_tasks_per_cycle = max(1, min(10, _env_int("KOLIBRI_MAX_TASKS_PER_CYCLE", 4)))
        self._task_timeout_sec = max(30, _env_int("KOLIBRI_TASK_TIMEOUT_SEC", 300))

        # Metrics
        self.metrics = LearningMetrics()
        self.metrics.started_at = time.time()

        # References to subsystems (lazy init)
        self._graph = graph
        self._formula_pool = formula_pool
        self._background_manager = background_manager

        # Tasks registry
        self._tasks: list[LearningTask] = []
        self._register_default_tasks()

        # Curriculum state
        self._curriculum_level = 0
        self._curriculum_progress = 0.0

        # Dialogue learning
        self._dialogue_buffer: list[dict[str, str]] = []
        self._max_dialogue_buffer = 100

        # Paths
        self._project_root = get_project_root()
        self._metrics_path = self._project_root / "data" / "models" / "continuous_learning_metrics.json"
        self._load_metrics()

        log.info(
            "[CLD] Continuous Learning Daemon initialized: "
            "enabled=%s, cycle=%ds, tasks=%d",
            self._enabled, self._cycle_interval_sec, len(self._tasks),
        )

    # -----------------------------------------------------------------------
    # Lifecycle
    # -----------------------------------------------------------------------

    def start(self, force: bool = False) -> dict:
        """Запускает демон в фоновом потоке."""
        with self._lock:
            if self._thread and self._thread.is_alive():
                return {"status": "already_running", "metrics": self.metrics.to_dict()}
            if not force and not self._enabled:
                return {"status": "disabled", "metrics": self.metrics.to_dict()}

            self._stop_event.clear()
            self._running = True
            self._thread = threading.Thread(
                target=self._loop, daemon=True, name="continuous-learning-daemon"
            )
            self._thread.start()
            log.info("[CLD] Daemon started")
            return {"status": "started", "metrics": self.metrics.to_dict()}

    def stop(self) -> dict:
        """Останавливает демон."""
        with self._lock:
            self._running = False
            self._stop_event.set()
            if self._thread and self._thread.is_alive():
                self._thread.join(timeout=10)
            log.info("[CLD] Daemon stopped")
            return {"status": "stopped", "metrics": self.metrics.to_dict()}

    def status(self) -> dict:
        """Возвращает статус и метрики."""
        with self._lock:
            is_alive = self._thread.is_alive() if self._thread else False
            self.metrics.total_uptime = time.time() - self.metrics.started_at
            return {
                "enabled": self._enabled,
                "running": is_alive,
                "cycle_interval_sec": self._cycle_interval_sec,
                "curriculum_level": self._curriculum_level,
                "tasks_registered": len(self._tasks),
                "tasks": [
                    {
                        "name": t.name,
                        "priority": t.priority.value,
                        "run_count": t.run_count,
                        "total_time": round(t.total_time, 2),
                        "error_count": t.error_count,
                        "last_error": t.last_error[:200] if t.last_error else "",
                    }
                    for t in self._tasks
                ],
                "metrics": self.metrics.to_dict(),
            }

    # -----------------------------------------------------------------------
    # Main Loop
    # -----------------------------------------------------------------------

    def _loop(self):
        """Основной цикл демона."""
        log.info("[CLD] Main loop started")
        while not self._stop_event.is_set():
            try:
                self._run_cycle()
            except Exception as exc:
                log.error("[CLD] Cycle error: %s", exc, exc_info=True)
                self.metrics.total_errors += 1

            # Ждём до следующего цикла
            self._stop_event.wait(self._cycle_interval_sec)

        self._running = False
        self._save_metrics()
        log.info("[CLD] Main loop exited")

    def _run_cycle(self):
        """Один цикл обучения."""
        cycle_start = time.time()
        self.metrics.total_cycles += 1
        self.metrics.last_cycle_at = cycle_start

        # Обновляем метрики из подсистем
        self._refresh_subsystem_metrics()

        # Выбираем задачи на основе приоритета и curriculum level
        tasks = self._select_tasks_for_cycle()

        executed = 0
        for task in tasks:
            if self._stop_event.is_set():
                break
            if executed >= self._max_tasks_per_cycle:
                break

            try:
                task_start = time.time()
                result = task.fn()
                elapsed = time.time() - task_start

                task.last_run = time.time()
                task.run_count += 1
                task.total_time += elapsed
                task.last_result = result if isinstance(result, dict) else {"ok": True}
                task.error_count = 0
                task.last_error = ""

                self.metrics.total_tasks_executed += 1
                executed += 1

                log.debug("[CLD] Task '%s' completed in %.2fs", task.name, elapsed)

            except Exception as exc:
                log.error("[CLD] Task '%s' failed: %s", task.name, exc, exc_info=True)
                task.error_count += 1
                task.last_error = str(exc)[:500]
                self.metrics.total_errors += 1

        # Сохраняем метрики каждые 10 циклов
        if self.metrics.total_cycles % 10 == 0:
            self._save_metrics()

        cycle_elapsed = time.time() - cycle_start
        log.info(
            "[CLD] Cycle #%d completed: %.1fs, %d tasks executed",
            self.metrics.total_cycles, cycle_elapsed, executed,
        )

    # -----------------------------------------------------------------------
    # Task Selection
    # -----------------------------------------------------------------------

    def _select_tasks_for_cycle(self) -> list[LearningTask]:
        """Выбирает задачи для текущего цикла на основе приоритета и истории."""
        now = time.time()

        # Сортируем по: (1) priority weight, (2) time since last run, (3) error count
        priority_weights = {
            LearningPriority.HIGH: 4,
            LearningPriority.MEDIUM: 3,
            LearningPriority.LOW: 2,
            LearningPriority.IDLE: 1,
        }

        scored = []
        for task in self._tasks:
            time_since_run = now - task.last_run if task.last_run > 0 else 999999
            error_penalty = task.error_count * 60  # Штраф за ошибки

            score = (
                priority_weights[task.priority] * 100
                + min(time_since_run, 3600)
                - error_penalty
                + task.weight * 10
            )
            scored.append((score, task))

        scored.sort(key=lambda x: -x[0])
        return [task for _, task in scored]

    # -----------------------------------------------------------------------
    # Task Registration
    # -----------------------------------------------------------------------

    def _register_default_tasks(self):
        """Регистрирует стандартные задачи обучения."""

        # HIGH priority
        self._tasks.append(LearningTask(
            name="crawl_and_ingest",
            priority=LearningPriority.HIGH,
            fn=self._task_crawl_and_ingest,
            weight=3.0,
        ))

        self._tasks.append(LearningTask(
            name="train_corpus",
            priority=LearningPriority.HIGH,
            fn=self._task_train_corpus,
            weight=2.5,
        ))

        # MEDIUM priority
        self._tasks.append(LearningTask(
            name="evolve_formulas",
            priority=LearningPriority.MEDIUM,
            fn=self._task_evolve_formulas,
            weight=2.0,
        ))

        self._tasks.append(LearningTask(
            name="train_embeddings",
            priority=LearningPriority.MEDIUM,
            fn=self._task_train_embeddings,
            weight=1.5,
        ))

        self._tasks.append(LearningTask(
            name="update_knowledge_graph",
            priority=LearningPriority.MEDIUM,
            fn=self._task_update_knowledge_graph,
            weight=1.5,
        ))

        # LOW priority
        self._tasks.append(LearningTask(
            name="world_model_observe",
            priority=LearningPriority.LOW,
            fn=self._task_world_model_observe,
            weight=1.0,
        ))

        self._tasks.append(LearningTask(
            name="curiosity_driven",
            priority=LearningPriority.LOW,
            fn=self._task_curiosity_driven,
            weight=0.8,
        ))

        # IDLE priority
        self._tasks.append(LearningTask(
            name="dialogue_learning",
            priority=LearningPriority.IDLE,
            fn=self._task_dialogue_learning,
            weight=0.5,
        ))

        self._tasks.append(LearningTask(
            name="self_improvement",
            priority=LearningPriority.IDLE,
            fn=self._task_self_improvement,
            weight=0.3,
        ))

    # -----------------------------------------------------------------------
    # Task Implementations
    # -----------------------------------------------------------------------

    def _task_crawl_and_ingest(self) -> dict:
        """Краулинг новых источников и ingest в систему."""
        if not external_network_available():
            return {"status": "skipped", "reason": "no_network"}

        manager = self._get_background_manager()
        result = manager.run_once(force=False)

        saved = 0
        latest = result.get("latest_result", {})
        if isinstance(latest, dict):
            saved = int(latest.get("saved_documents", 0))

        return {
            "status": "ok",
            "saved_documents": saved,
            "domain_delta": latest.get("domain_delta", []),
        }

    def _task_train_corpus(self) -> dict:
        """Тренировка corpus trainer на новых данных."""
        # Находим новые .txt/.md файлы в data/corpus и data/ingested
        corpus_dirs = [
            self._project_root / "data" / "corpus",
            self._project_root / "data" / "ingested",
            self._project_root / "docs" / "ingested",
        ]

        text_files = []
        for d in corpus_dirs:
            if d.exists():
                text_files.extend(list(d.glob("*.txt")))
                text_files.extend(list(d.glob("*.md")))

        if not text_files:
            return {"status": "skipped", "reason": "no_text_files"}

        # Ограничиваем по curriculum level
        level = CURRICULUM_LEVELS[self._curriculum_level]
        max_files = 10 + self._curriculum_level * 5
        text_files = text_files[:max_files]

        # Запускаем corpus trainer
        try:
            from .cognition import SwarmCognition
            # Используем существующий граф
            graph = self._get_graph()
            total_patterns = 0
            total_edges = 0
            total_tokens = 0

            for f in text_files[:5]:  # Максимум 5 файлов за цикл
                text = f.read_text(encoding="utf-8", errors="ignore")
                # Пропускаем слишком сложные тексты на низких уровнях
                complexity = estimate_text_complexity(text)
                if complexity > self._curriculum_level:
                    continue

                stats = graph.train_text(text)
                total_patterns += stats.get("patterns", 0)
                total_edges += stats.get("edges", 0)
                total_tokens += stats.get("tokens", 0)

            # Сохраняем модель
            model_path = self._project_root / "data" / "models" / "kolibri_web.klm"
            model_path.parent.mkdir(parents=True, exist_ok=True)
            graph.save(str(model_path))

            self.metrics.corpus_patterns = total_patterns
            self.metrics.corpus_edges = total_edges
            self.metrics.corpus_documents = len(text_files[:5])
            self.metrics.corpus_tokens = total_tokens

            return {
                "status": "ok",
                "files_trained": len(text_files[:5]),
                "patterns": total_patterns,
                "edges": total_edges,
                "tokens": total_tokens,
            }
        except Exception as exc:
            return {"status": "error", "error": str(exc)}

    def _task_evolve_formulas(self) -> dict:
        """Эволюция формул на основе текущих данных."""
        try:
            pool = self._get_formula_pool()

            # Запускаем один шаг эволюции
            pool.tick()

            # Сохраняем
            save_path = self._project_root / "data" / "models" / "kolibri_formulas.json"
            save_path.parent.mkdir(parents=True, exist_ok=True)
            pool.save(str(save_path))

            best = pool.get_best(1)
            best_fitness = best[0].fitness if best else 0.0

            self.metrics.formula_pool_size = len(pool.formulas)
            self.metrics.formula_best_fitness = best_fitness
            self.metrics.formula_evolution_count += 1

            return {
                "status": "ok",
                "pool_size": len(pool.formulas),
                "best_fitness": round(best_fitness, 6),
            }
        except Exception as exc:
            return {"status": "error", "error": str(exc)}

    def _task_train_embeddings(self) -> dict:
        """Тренировка word2vec embeddings."""
        try:
            graph = self._get_graph()
            if not graph.embeddings:
                return {"status": "skipped", "reason": "no_embeddings"}

            # Тренируем на текущих предложениях
            sentences = list(graph.sentence_store.get_recent(1000))
            if not sentences:
                return {"status": "skipped", "reason": "no_sentences"}

            # Один шаг тренировки
            loss = graph.embeddings.train_step(sentences, lr=0.01)

            self.metrics.embedding_vocab_size = len(graph.embeddings.vocab)
            self.metrics.embedding_loss = loss

            return {
                "status": "ok",
                "vocab_size": len(graph.embeddings.vocab),
                "loss": round(loss, 6),
                "sentences": len(sentences),
            }
        except Exception as exc:
            return {"status": "error", "error": str(exc)}

    def _task_update_knowledge_graph(self) -> dict:
        """Обновление графа знаний: дистилляция, нормализация."""
        try:
            graph = self._get_graph()
            before_patterns = len(graph.patterns)
            before_edges = len(graph.edges)

            # Дистилляция — удаляем слабые знания
            graph.distill()

            after_patterns = len(graph.patterns)
            after_edges = len(graph.edges)

            return {
                "status": "ok",
                "patterns_before": before_patterns,
                "patterns_after": after_patterns,
                "edges_before": before_edges,
                "edges_after": after_edges,
                "evicted_patterns": before_patterns - after_patterns,
                "evicted_edges": before_edges - after_edges,
            }
        except Exception as exc:
            return {"status": "error", "error": str(exc)}

    def _task_world_model_observe(self) -> dict:
        """World Model observation на новых данных."""
        # World Model требует C-бинарник, проверяем доступность
        return {"status": "skipped", "reason": "world_model_not_integrated"}

    def _task_curiosity_driven(self) -> dict:
        """Curiosity-driven генерация и анализ."""
        # Генерируем текст и анализируем surprise
        return {"status": "skipped", "reason": "curiosity_not_integrated"}

    def _task_dialogue_learning(self) -> dict:
        """Извлечение знаний из диалогов."""
        if not self._dialogue_buffer:
            return {"status": "skipped", "reason": "no_dialogues"}

        facts_extracted = 0
        knowledge_created = 0

        for dialogue in self._dialogue_buffer[:20]:
            text = dialogue.get("user", "") + " " + dialogue.get("assistant", "")
            facts = extract_facts_from_text(text)

            for fact in facts:
                # Добавляем в граф знаний
                graph = self._get_graph()
                subject = fact["subject"]
                predicate = fact["predicate"]

                # Создаём рёбра между subject и predicate
                graph.learn_cooccurrence(subject, predicate, window=3)
                facts_extracted += 1
                knowledge_created += 1

        # Очищаем буфер
        processed = min(20, len(self._dialogue_buffer))
        self._dialogue_buffer = self._dialogue_buffer[processed:]

        self.metrics.facts_extracted += facts_extracted
        self.metrics.knowledge_from_dialogues += knowledge_created
        self.metrics.dialogues_processed += processed

        return {
            "status": "ok",
            "dialogues_processed": processed,
            "facts_extracted": facts_extracted,
            "knowledge_created": knowledge_created,
        }

    def _task_self_improvement(self) -> dict:
        """Self-improvement: анализ ошибок и улучшение."""
        # Анализируем задачи с ошибками
        error_tasks = [t for t in self._tasks if t.error_count > 0]
        if not error_tasks:
            return {"status": "ok", "message": "no_errors_to_analyze"}

        # Увеличиваем интервал для проблемных задач
        for task in error_tasks:
            if task.error_count > 3:
                task.weight *= 0.5  # Снижаем приоритет
                log.warning(
                    "[CLD] Task '%s' has %d errors, reducing weight to %.1f",
                    task.name, task.error_count, task.weight,
                )

        return {
            "status": "ok",
            "error_tasks_analyzed": len(error_tasks),
            "weights_adjusted": sum(1 for t in error_tasks if t.error_count > 3),
        }

    # -----------------------------------------------------------------------
    # Public API for dialogue learning
    # -----------------------------------------------------------------------

    def record_dialogue(self, user_text: str, assistant_text: str) -> None:
        """Записывает диалог для последующего извлечения знаний."""
        with self._lock:
            self._dialogue_buffer.append({
                "user": user_text,
                "assistant": assistant_text,
                "timestamp": time.time(),
            })
            if len(self._dialogue_buffer) > self._max_dialogue_buffer:
                self._dialogue_buffer = self._dialogue_buffer[-self._max_dialogue_buffer:]

    # -----------------------------------------------------------------------
    # Curriculum Management
    # -----------------------------------------------------------------------

    def advance_curriculum(self) -> dict:
        """Повышает уровень curriculum."""
        if self._curriculum_level < len(CURRICULUM_LEVELS) - 1:
            self._curriculum_level += 1
            level = CURRICULUM_LEVELS[self._curriculum_level]
            self.metrics.curriculum_level = self._curriculum_level
            self.metrics.curriculum_source = level["name"]
            log.info("[CLD] Curriculum advanced to level %d: %s", self._curriculum_level, level["name"])
            return {"status": "advanced", "level": self._curriculum_level, "name": level["name"]}
        return {"status": "max_level", "level": self._curriculum_level}

    # -----------------------------------------------------------------------
    # Subsystem Access
    # -----------------------------------------------------------------------

    def _get_graph(self) -> KnowledgeGraph:
        if self._graph is None:
            self._graph = KnowledgeGraph()
        return self._graph

    def _get_formula_pool(self) -> FormulaPool:
        if self._formula_pool is None:
            save_path = self._project_root / "data" / "models" / "kolibri_formulas.json"
            self._formula_pool = FormulaPool.load_or_create(str(save_path))
        return self._formula_pool

    def _get_background_manager(self) -> BackgroundLearningManager:
        if self._background_manager is None:
            self._background_manager = BackgroundLearningManager()
        return self._background_manager

    def _refresh_subsystem_metrics(self):
        """Обновляет метрики из подсистем."""
        try:
            graph = self._get_graph()
            self.metrics.corpus_patterns = len(graph.patterns)
            self.metrics.corpus_edges = len(graph.edges)

            pool = self._get_formula_pool()
            self.metrics.formula_pool_size = len(pool.formulas)
            best = pool.get_best(1)
            if best:
                self.metrics.formula_best_fitness = best[0].fitness
        except Exception:
            pass

    # -----------------------------------------------------------------------
    # Metrics Persistence
    # -----------------------------------------------------------------------

    def _save_metrics(self):
        """Сохраняет метрики на диск."""
        try:
            self._metrics_path.parent.mkdir(parents=True, exist_ok=True)
            data = self.metrics.to_dict()
            data["saved_at"] = time.time()
            self._metrics_path.write_text(
                json.dumps(data, ensure_ascii=False, indent=2),
                encoding="utf-8",
            )
        except Exception as exc:
            log.error("[CLD] Failed to save metrics: %s", exc)

    def _load_metrics(self):
        """Загружает метрики с диска."""
        try:
            if self._metrics_path.exists():
                data = json.loads(self._metrics_path.read_text(encoding="utf-8"))
                self.metrics.total_cycles = int(data.get("total_cycles", 0))
                self.metrics.total_tasks_executed = int(data.get("total_tasks_executed", 0))
                self.metrics.total_errors = int(data.get("total_errors", 0))
                self.metrics.total_uptime = float(data.get("total_uptime", 0.0))
                self.metrics.curriculum_level = int(data.get("curriculum", {}).get("level", 0))
        except Exception:
            pass


# ============================================================================
# Singleton
# ============================================================================

_daemon_instance: ContinuousLearningDaemon | None = None
_daemon_lock = threading.Lock()


def get_continuous_learning_daemon(
    graph: Optional[KnowledgeGraph] = None,
    formula_pool: Optional[FormulaPool] = None,
    background_manager: Optional[BackgroundLearningManager] = None,
) -> ContinuousLearningDaemon:
    """Возвращает singleton демона непрерывного обучения."""
    global _daemon_instance
    with _daemon_lock:
        if _daemon_instance is None:
            _daemon_instance = ContinuousLearningDaemon(
                graph=graph,
                formula_pool=formula_pool,
                background_manager=background_manager,
            )
        return _daemon_instance
