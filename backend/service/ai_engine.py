"""
ai_engine.py — Движок «Числового Мышления» Kolibri

НЕ классический TF-IDF / N-gram. Настоящая архитектура Kolibri:

1. Каждое слово = 64-цифровой числовой паттерн (DJB2 → LCG каскад)
2. Знания = граф связей между паттернами (co-occurrence edges)
3. Формулы = 1024 цифры генома → 100-слойная нейросеть из 12 операций
4. Эволюция: мутация + кроссовер + селекция = улучшение формул
5. Восстановление: из числового паттерна → исходное слово
6. Всё хранится в ЧИСЛАХ. Формулах. Паттернах.
"""
from __future__ import annotations

import hashlib
import math
import os
import re
import subprocess
import time
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

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
)

import logging

log = logging.getLogger("kolibri.ai")

# ---------------------------------------------------------------------------
# Конфигурация
# ---------------------------------------------------------------------------

_PROJECT_ROOT = Path("/workspaces/kolibri-project")
_TRAINER_BIN = _PROJECT_ROOT / "build" / "kolibri_mass_trainer"
_DEFAULT_MODEL = _PROJECT_ROOT / "data" / "models" / "kolibri_web.klm"
_CORPUS_DIR = _PROJECT_ROOT / "data" / "corpus"
_FORMULA_SAVE_PATH = _PROJECT_ROOT / "data" / "models" / "kolibri_formulas.json"

_MAX_CONTEXT_TURNS = 20
_QUERY_TIMEOUT = 10


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
    4. Формулы = геном 1024 цифр → 100 слоёв, 12 операций
    5. Эволюция формул = мутация + кроссовер + селекция
    """

    def __init__(self, model_path: Path | None = None) -> None:
        self.graph = KnowledgeGraph()
        # Загружаем формулы с диска — эволюция ПРОДОЛЖАЕТСЯ между перезапусками
        self.formula_pool = FormulaPool.load_or_create(_FORMULA_SAVE_PATH)
        self.sentence_store = SentenceStore()
        self.c_retriever = CModelRetriever(model_path)
        self.conversations: dict[str, Conversation] = {}
        self._corpus_loaded = False
        self._stats_cache: dict | None = None
        self._stats_cache_time = 0.0
        self._evolution_counter = 0  # Счётчик для периодического сохранения
        self._load_corpus()

    def _load_corpus(self) -> None:
        """Загрузить тексты и обучить ЧИСЛОВОЙ ГРАФ."""
        import logging
        log = logging.getLogger("kolibri.ai")
        t0 = time.time()

        _MAX_FILE_SIZE = 100_000      # 100 КБ макс на файл
        _MAX_FILES     = 200          # Макс файлов для загрузки (увеличено для агентских текстов)
        _PRIORITY_DIR  = _CORPUS_DIR  # Приоритетная директория

        corpus_dirs = [
            _CORPUS_DIR,
            _PROJECT_ROOT / "data" / "training",
            _PROJECT_ROOT / "seeds",
            _PROJECT_ROOT / "training",
        ]
        total_texts = 0
        for corpus_dir in corpus_dirs:
            if not corpus_dir.exists():
                continue
            for f in sorted(corpus_dir.rglob("*.txt")):
                if total_texts >= _MAX_FILES:
                    break
                try:
                    fsize = f.stat().st_size
                    if fsize < 50 or fsize > _MAX_FILE_SIZE:
                        log.debug("Skip %s (%d bytes)", f.name, fsize)
                        continue
                    content = f.read_text(encoding="utf-8", errors="ignore")
                    self.graph.train_text(content)
                    self.sentence_store.add_text(content)
                    total_texts += 1
                except OSError:
                    continue
        if total_texts > 0:
            self._corpus_loaded = True
            self._train_formulas_from_graph()
        log.info(
            "Corpus loaded: %d files, %d sentences in %.1fs",
            total_texts, self.sentence_store.size, time.time() - t0,
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
        edges_sorted = sorted(
            self.graph.edges.items(),
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
            # Больше поколений → реальный рост fitness
            fitness = self.formula_pool.evolve(generations=10)
            log.info(
                "Formula semantic training: %d pairs, fitness=%.4f, gen=%d",
                len(self.formula_pool.semantic_pairs),
                fitness,
                self.formula_pool.generation,
            )
            # Автосохранение после обучения
            self._save_formulas()

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
        Pipeline: паттерны → граф → формулы → C-модель → синтез.
        """
        start_time = time.time()
        conv = self.get_or_create_conversation(conversation_id)
        conv.add("user", message)
        lower = message.lower()

        special = self._handle_special_commands(lower)
        if special:
            conv.add("assistant", special["response"])
            special["conversation_id"] = conv.id
            special["duration_ms"] = round((time.time() - start_time) * 1000, 1)
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
        best_formula = self.formula_pool.best()
        retrieved = self.sentence_store.retrieve(
            query=message, formula=best_formula, top_k=5,
        )

        # === Формульная генерация слов ===
        # Формула трансформирует паттерны запроса → новые паттерны → слова
        formula_words = self.graph.generate_words(
            query=message, formula=best_formula, max_words=8,
        )

        graph_answer, graph_confidence, graph_meta = self.graph.answer(message, max_words=15)
        formula_result = self._formula_predict(message)
        c_knowledge = self.c_retriever.query(message) if self.c_retriever.available else []
        # Числовой запрос к C-модели — ответ в цифрах
        c_digits = self.c_retriever.query_digits(message) if self.c_retriever.available else []
        assoc_answer = self.formula_pool.lookup(message)

        response, confidence, method = self._synthesize_response(
            message=message,
            retrieved_sentences=retrieved,
            formula_words=formula_words,
            graph_answer=graph_answer,
            graph_confidence=graph_confidence,
            graph_meta=graph_meta,
            formula_result=formula_result,
            c_knowledge=c_knowledge,
            assoc_answer=assoc_answer,
        )

        # === Непрерывное обучение: формулы УЧАТСЯ на КАЖДОМ ответе ===
        # 1. Feedback от успешных ответов (любой метод, confidence >= 0.2)
        if confidence >= 0.2 and method != "no-knowledge":
            self._train_formula_on_retrieval(message, response)

        # 2. Кросс-обучение: C-модель → Python-формулы
        if c_knowledge:
            self._train_formula_on_c_knowledge(message, c_knowledge)

        full_response = response

        formula_hex = best_formula.gene.to_hex()
        conv.add("assistant", full_response, formula_used=formula_hex)

        duration = round((time.time() - start_time) * 1000, 1)

        return {
            "response": full_response,
            "confidence": confidence,
            "sources": [method],
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
            },
            "graph_stats": self.graph.get_stats(),
        }

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
        3. Чистая формульная генерация (слова из трансформации паттернов)
        4. C-модель (.klm бинарь)
        5. Граф слов (fallback)
        """
        # 1. Формульные ассоциации (точное совпадение через хеш)
        if assoc_answer:
            return (assoc_answer, 0.95, "formula-association")

        # 2. Гибрид: sentence retrieval + формульная генерация
        #    Формула участвует В ОБОИХ процессах:
        #    - Re-ranks предложения через predict(query ⊕ sentence)
        #    - Генерирует слова-подсказки через трансформацию паттернов
        if retrieved_sentences:
            best_text, best_score = retrieved_sentences[0]
            if best_score >= 0.12:
                parts: list[str] = []
                seen: set[str] = set()
                for text, score in retrieved_sentences[:3]:
                    if score >= best_score * 0.4:
                        key = text[:50]
                        if key not in seen:
                            parts.append(text)
                            seen.add(key)
                answer = ". ".join(parts)
                if not answer.endswith((".", "!", "?")):
                    answer += "."

                confidence = min(0.95, best_score + 0.2)
                return (answer, confidence, "formula-retrieval")

        # 3. Чистая формульная генерация
        #    Нет retrieved предложений, но формула ПОРОЖДАЕТ слова
        #    Пробуем найти предложения, содержащие сгенерированные слова
        if formula_words and len(formula_words) >= 2:
            words_only = [w for w, s in formula_words if s > 0.2]
            if len(words_only) >= 2:
                # Ищем предложения из памяти, содержащие формульные слова
                fw_query = " ".join(words_only[:5])
                fw_sentences = self.sentence_store.retrieve(
                    query=fw_query, formula=None, top_k=3,
                )
                if fw_sentences and fw_sentences[0][1] >= 0.1:
                    parts = []
                    seen_s: set[str] = set()
                    for text, score in fw_sentences[:2]:
                        key = text[:50]
                        if key not in seen_s:
                            parts.append(text)
                            seen_s.add(key)
                    answer = ". ".join(parts)
                    if not answer.endswith((".", "!", "?")):
                        answer += "."
                    avg_score = sum(s for _, s in formula_words[:5]) / min(5, len(formula_words))
                    return (answer, min(0.7, avg_score + 0.15), "formula-generation")

                # Если предложений не нашли — просто перечисляем слова
                generated = ", ".join(words_only[:8])
                answer = (
                    f"Формула числового мышления ассоциирует ваш запрос "
                    f"с понятиями: **{generated}**."
                )
                avg_score = sum(s for _, s in formula_words[:5]) / min(5, len(formula_words))
                return (answer, min(0.5, avg_score), "formula-generation")

        # 4. C-модель (.klm)
        if c_knowledge:
            clean = [
                k for k in c_knowledge
                if len(k) > 10 and "://" not in k
                and not k.startswith("[") and not k.startswith("(")
            ]
            if clean:
                return (" ".join(clean[:3]), 0.5, "c-model")

        # 5. Граф слов (fallback)
        if graph_answer and graph_confidence >= 0.15:
            return (graph_answer, graph_confidence, "knowledge-graph")

        return (
            "У меня пока недостаточно знаний по этой теме. "
            "Обучите меня — отправьте текст или URL для обучения.",
            0.1, "no-knowledge",
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

    def train_text(self, text: str) -> dict:
        """Обучить на тексте — реально обновляет числовой граф + предложения."""
        result = self.graph.train_text(text)
        self.sentence_store.add_text(text)
        self._train_formulas_from_graph()
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

    def _train_formula_on_retrieval(self, query: str, answer_text: str) -> None:
        """
        Feedback loop: формулы УЧАТСЯ на каждом успешном ответе.

        Из пары (query, answer) извлекаем семантические связи:
        каждое слово запроса → каждое слово ответа = семантическая пара.
        Формулы учатся трансформировать паттерны запроса → паттерны ответа.

        Непрерывная эволюция — каждый ответ = тренировка.
        """
        q_tokens = _tokenize(query)
        a_tokens = _tokenize(answer_text)
        if not q_tokens or not a_tokens:
            return

        # Семантические пары: слово_запроса → слово_ответа
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

        # Ассоциация Q→A в лучшие формулы
        self.formula_pool.add_association(query, answer_text)

        # Эволюция НА КАЖДЫЙ успешный ответ — реальное обучение
        if pairs_added > 0:
            self._evolution_counter += 1
            fitness = self.formula_pool.evolve(generations=3)
            log.debug(
                "Formula feedback: +%d pairs, fitness=%.4f, gen=%d",
                pairs_added, fitness, self.formula_pool.generation,
            )
            # Автосохранение каждые 5 эволюций
            if self._evolution_counter % 5 == 0:
                self._save_formulas()

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

    def _handle_special_commands(self, lower: str) -> dict | None:
        stripped = lower.strip().rstrip("?!.")

        # --- Математические выражения ---
        math_result = self._try_math_eval(stripped)
        if math_result is not None:
            return math_result

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
                "• ⚡ **Формулы** — 1024 цифры генома, 100 слоёв, 12 операций\n"
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
                f"**Геном (64 из 1024 цифр):**\n"
                f"`{''.join(str(d) for d in gene_preview)}`\n\n"
                f"**Hex:** `{best.gene.to_hex()}`\n\n"
                f"**100 слоёв × 12 операций:**\n"
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
        # Формулы НЕ сбрасываем — они продолжают эволюцию
        # Загружаем с диска (если сохранены) или оставляем текущие
        self.sentence_store = SentenceStore()
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

_engine_instance: KolibriAIEngine | None = None


def get_engine() -> KolibriAIEngine:
    global _engine_instance
    if _engine_instance is None:
        _engine_instance = KolibriAIEngine()
    return _engine_instance
