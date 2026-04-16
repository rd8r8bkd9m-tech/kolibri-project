"""
knowledge_collector.py — Сборщик корпуса знаний для Kolibri AI.

Функции:
1. Сканирование data/corpus/ и загрузка всех .txt файлов
2. Добавление новых знаний (текст → файл в corpus)
3. Генерация обучающих последовательностей для FormulaLM
4. Статистика корпуса: кол-во файлов, токенов, уникальных слов
"""
from __future__ import annotations

import os
import re
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


@dataclass
class CorpusStats:
    """Статистика корпуса."""
    total_files: int = 0
    total_chars: int = 0
    total_words: int = 0
    unique_words: int = 0
    total_sentences: int = 0
    languages: dict[str, int] = field(default_factory=dict)


class KnowledgeCollector:
    """Сборщик и менеджер корпуса знаний."""

    def __init__(self, corpus_dir: str | Path | None = None) -> None:
        if corpus_dir is None:
            # Определяем путь относительно проекта
            root = Path(__file__).resolve().parent.parent.parent
            self._corpus_dir = root / "data" / "corpus"
        else:
            self._corpus_dir = Path(corpus_dir)

        self._corpus_dir.mkdir(parents=True, exist_ok=True)
        self._texts: list[str] = []
        self._file_paths: list[Path] = []
        self._loaded = False

    @property
    def corpus_dir(self) -> Path:
        return self._corpus_dir

    # ── Загрузка ──────────────────────────────────────────────────────

    def load_corpus(self, max_files: int = 0) -> int:
        """Загрузить все .txt файлы из corpus_dir.

        Args:
            max_files: Макс. файлов (0 = все)

        Returns:
            Количество загруженных файлов
        """
        self._texts.clear()
        self._file_paths.clear()

        txt_files = sorted(self._corpus_dir.glob("*.txt"))
        if max_files > 0:
            txt_files = txt_files[:max_files]

        for path in txt_files:
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
                if text.strip():
                    self._texts.append(text)
                    self._file_paths.append(path)
            except OSError:
                continue

        self._loaded = True
        return len(self._texts)

    # ── Добавление знаний ─────────────────────────────────────────────

    def add_knowledge(self, text: str, topic: str = "general") -> Path:
        """Добавить текст как новый файл в корпус.

        Args:
            text: Текст знания
            topic: Тема (для имени файла)

        Returns:
            Путь к созданному файлу
        """
        # Санитизация имени
        safe_topic = re.sub(r"[^\w\-]", "_", topic.lower())[:40]
        timestamp = int(time.time())
        filename = f"knowledge_{safe_topic}_{timestamp}.txt"
        path = self._corpus_dir / filename

        path.write_text(text, encoding="utf-8")
        self._texts.append(text)
        self._file_paths.append(path)

        return path

    # ── Извлечение данных ─────────────────────────────────────────────

    def get_all_texts(self) -> list[str]:
        """Получить все загруженные тексты."""
        if not self._loaded:
            self.load_corpus()
        return list(self._texts)

    def get_sentences(self, max_sentences: int = 0) -> list[str]:
        """Извлечь предложения из корпуса.

        Returns:
            Список предложений (разделённых по . ! ? \n)
        """
        if not self._loaded:
            self.load_corpus()

        sentences: list[str] = []
        for text in self._texts:
            for sent in re.split(r'[.!?]\s+|\n+', text):
                sent = sent.strip()
                if len(sent) >= 10:
                    sentences.append(sent)
                    if max_sentences > 0 and len(sentences) >= max_sentences:
                        return sentences
        return sentences

    def get_training_sequences(
        self,
        seq_length: int = 32,
        max_sequences: int = 1000,
    ) -> list[list[int]]:
        """Генерация обучающих последовательностей (byte-level) для FormulaLM.

        Args:
            seq_length: Длина каждой последовательности
            max_sequences: Макс. количество последовательностей

        Returns:
            Список последовательностей индексов байтов
        """
        if not self._loaded:
            self.load_corpus()

        sequences: list[list[int]] = []
        for text in self._texts:
            encoded = text.encode("utf-8")
            for i in range(0, len(encoded) - seq_length, seq_length // 2):
                seq = list(encoded[i:i + seq_length])
                sequences.append(seq)
                if len(sequences) >= max_sequences:
                    return sequences
        return sequences

    # ── Статистика ────────────────────────────────────────────────────

    def get_stats(self) -> CorpusStats:
        """Статистика корпуса."""
        if not self._loaded:
            self.load_corpus()

        stats = CorpusStats(total_files=len(self._texts))
        all_words: set[str] = set()

        for text in self._texts:
            stats.total_chars += len(text)
            words = re.findall(r'\b\w+\b', text.lower())
            stats.total_words += len(words)
            all_words.update(words)

            # Подсчёт предложений
            sents = re.split(r'[.!?]\s+|\n+', text)
            stats.total_sentences += sum(1 for s in sents if len(s.strip()) >= 10)

            # Определение языка (простая эвристика)
            cyrillic = len(re.findall(r'[а-яё]', text.lower()))
            latin = len(re.findall(r'[a-z]', text.lower()))
            if cyrillic > latin:
                stats.languages["ru"] = stats.languages.get("ru", 0) + 1
            elif latin > 0:
                stats.languages["en"] = stats.languages.get("en", 0) + 1

        stats.unique_words = len(all_words)
        return stats

    # ── Поиск по корпусу ──────────────────────────────────────────────

    def search(self, query: str, top_k: int = 5) -> list[tuple[str, float]]:
        """Простой keyword-поиск по корпусу.

        Returns:
            Список (filename, relevance_score)
        """
        if not self._loaded:
            self.load_corpus()

        query_words = set(re.findall(r'\b\w+\b', query.lower()))
        if not query_words:
            return []

        results: list[tuple[str, float]] = []
        for i, text in enumerate(self._texts):
            text_words = set(re.findall(r'\b\w+\b', text.lower()))
            overlap = len(query_words & text_words)
            if overlap > 0:
                score = overlap / len(query_words)
                name = self._file_paths[i].name if i < len(self._file_paths) else f"text_{i}"
                results.append((name, score))

        results.sort(key=lambda x: x[1], reverse=True)
        return results[:top_k]
