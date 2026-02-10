"""
Тесты KnowledgeCollector — сборщик корпуса знаний.

Тестирует:
1. Загрузка корпуса из data/corpus/
2. Добавление новых знаний
3. Генерация обучающих последовательностей
4. Статистика корпуса
5. Поиск по корпусу
"""
from __future__ import annotations

import pytest
import tempfile
from pathlib import Path
from backend.service.knowledge_collector import KnowledgeCollector, CorpusStats


@pytest.fixture
def tmp_corpus(tmp_path: Path) -> KnowledgeCollector:
    """Коллектор с временной директорией."""
    corpus_dir = tmp_path / "corpus"
    corpus_dir.mkdir()
    # Создадим пару тестовых файлов
    (corpus_dir / "test1.txt").write_text(
        "Kolibri OS — система искусственного интеллекта. "
        "Использует числовое мышление и формулы.",
        encoding="utf-8",
    )
    (corpus_dir / "test2.txt").write_text(
        "Сжатие данных — фундаментальная задача. "
        "Арифметическое кодирование обеспечивает оптимальное сжатие.",
        encoding="utf-8",
    )
    (corpus_dir / "test3.txt").write_text(
        "Python is a high-level programming language. "
        "It supports multiple paradigms including OOP.",
        encoding="utf-8",
    )
    return KnowledgeCollector(corpus_dir=corpus_dir)


@pytest.fixture
def real_collector() -> KnowledgeCollector:
    """Коллектор с реальным корпусом."""
    return KnowledgeCollector()


class TestLoading:
    def test_load_corpus(self, tmp_corpus: KnowledgeCollector) -> None:
        count = tmp_corpus.load_corpus()
        assert count == 3

    def test_load_max_files(self, tmp_corpus: KnowledgeCollector) -> None:
        count = tmp_corpus.load_corpus(max_files=2)
        assert count == 2

    def test_get_all_texts(self, tmp_corpus: KnowledgeCollector) -> None:
        texts = tmp_corpus.get_all_texts()
        assert len(texts) == 3
        assert any("Kolibri" in t for t in texts)


class TestAddKnowledge:
    def test_add_knowledge(self, tmp_corpus: KnowledgeCollector) -> None:
        path = tmp_corpus.add_knowledge(
            "Тестовое знание для модуля.",
            topic="тест",
        )
        assert path.exists()
        assert "knowledge_" in path.name
        assert path.suffix == ".txt"

    def test_add_knowledge_updates_texts(self, tmp_corpus: KnowledgeCollector) -> None:
        tmp_corpus.load_corpus()
        before = len(tmp_corpus.get_all_texts())
        tmp_corpus.add_knowledge("Новое знание.")
        after = len(tmp_corpus.get_all_texts())
        assert after == before + 1


class TestSentences:
    def test_get_sentences(self, tmp_corpus: KnowledgeCollector) -> None:
        sents = tmp_corpus.get_sentences()
        assert len(sents) > 0
        assert all(len(s) >= 10 for s in sents)

    def test_get_sentences_max(self, tmp_corpus: KnowledgeCollector) -> None:
        sents = tmp_corpus.get_sentences(max_sentences=2)
        assert len(sents) <= 2


class TestTrainingSequences:
    def test_get_training_sequences(self, tmp_corpus: KnowledgeCollector) -> None:
        seqs = tmp_corpus.get_training_sequences(seq_length=16, max_sequences=10)
        assert len(seqs) > 0
        assert all(len(s) == 16 for s in seqs)
        assert all(0 <= b <= 255 for s in seqs for b in s)

    def test_sequences_max_count(self, tmp_corpus: KnowledgeCollector) -> None:
        seqs = tmp_corpus.get_training_sequences(max_sequences=5)
        assert len(seqs) <= 5


class TestStats:
    def test_stats_structure(self, tmp_corpus: KnowledgeCollector) -> None:
        stats = tmp_corpus.get_stats()
        assert isinstance(stats, CorpusStats)
        assert stats.total_files == 3
        assert stats.total_chars > 0
        assert stats.total_words > 0
        assert stats.unique_words > 0

    def test_stats_languages(self, tmp_corpus: KnowledgeCollector) -> None:
        stats = tmp_corpus.get_stats()
        assert "ru" in stats.languages or "en" in stats.languages


class TestSearch:
    def test_search_basic(self, tmp_corpus: KnowledgeCollector) -> None:
        results = tmp_corpus.search("сжатие данных")
        assert len(results) > 0
        assert all(isinstance(r, tuple) and len(r) == 2 for r in results)

    def test_search_empty_query(self, tmp_corpus: KnowledgeCollector) -> None:
        results = tmp_corpus.search("")
        assert results == []


class TestRealCorpus:
    def test_real_corpus_loads(self, real_collector: KnowledgeCollector) -> None:
        """Проверяем что реальный корпус загружается."""
        count = real_collector.load_corpus()
        assert count > 0
        stats = real_collector.get_stats()
        assert stats.total_files > 0
        assert stats.total_chars > 0
