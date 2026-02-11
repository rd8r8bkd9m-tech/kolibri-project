"""
Тесты для number_mind — KnowledgeGraph, FormulaPool, SentenceStore.
"""
from __future__ import annotations

import pytest

from backend.service.number_mind import (
    KnowledgeGraph,
    djb2_hash,
    word_to_pattern,
    _tokenize,
    _is_stop_word,
    _stem_ru,
)


# ---------------------------------------------------------------------------
# djb2_hash + word_to_pattern
# ---------------------------------------------------------------------------


class TestHashing:
    def test_djb2_deterministic(self) -> None:
        assert djb2_hash("hello") == djb2_hash("hello")

    def test_djb2_different_words(self) -> None:
        assert djb2_hash("hello") != djb2_hash("world")

    def test_word_to_pattern_length(self) -> None:
        pat = word_to_pattern("тест")
        assert len(pat) == 64
        assert all(0 <= d <= 9 for d in pat)

    def test_word_to_pattern_deterministic(self) -> None:
        assert word_to_pattern("кот") == word_to_pattern("кот")


# ---------------------------------------------------------------------------
# Tokenizer + helpers
# ---------------------------------------------------------------------------


class TestTokenizer:
    def test_tokenize_basic(self) -> None:
        tokens = _tokenize("Привет мир!")
        assert "привет" in tokens
        assert "мир" in tokens

    def test_tokenize_empty(self) -> None:
        assert _tokenize("") == []

    def test_is_stop_word(self) -> None:
        assert _is_stop_word("и")
        assert _is_stop_word("в")
        assert not _is_stop_word("нейросеть")

    def test_stem_ru(self) -> None:
        s = _stem_ru("программирования")
        # Стем может быть той же длины если слово не сокращается
        assert len(s) <= len("программирования")
        assert len(s) >= 4


# ---------------------------------------------------------------------------
# KnowledgeGraph
# ---------------------------------------------------------------------------


class TestKnowledgeGraph:
    def setup_method(self) -> None:
        self.g = KnowledgeGraph()

    def test_train_text_creates_patterns(self) -> None:
        self.g.train_text("кот кошка котёнок")
        assert len(self.g.patterns) > 0

    def test_train_text_creates_edges(self) -> None:
        self.g.train_text("кот кошка котёнок")
        assert len(self.g.edges) > 0

    def test_answer_basic(self) -> None:
        self.g.train_text("Python — язык программирования")
        self.g.train_text("Python используется для машинного обучения")
        answer, conf, meta = self.g.answer("Python", max_words=10)
        assert isinstance(answer, str)
        assert isinstance(conf, float)
        assert isinstance(meta, dict)

    def test_answer_unknown_topic(self) -> None:
        answer, conf, meta = self.g.answer("квантовая гравитация")
        assert conf < 0.5

    def test_find_similar(self) -> None:
        self.g.train_text("кот домашнее животное")
        self.g.train_text("собака домашнее животное")
        results = self.g.find_similar("кот", limit=3)
        assert isinstance(results, list)

    def test_hash_to_word(self) -> None:
        self.g.train_text("тестовое слово")
        h = djb2_hash("тестовое")
        assert self.g._hash_to_word.get(h) == "тестовое"

    def test_reason_abstract(self) -> None:
        self.g.train_text("кот домашнее животное")
        self.g.train_text("собака домашнее животное")
        result = self.g.reason_abstract("кот", depth=2)
        assert isinstance(result, tuple)
        assert len(result) == 2  # (answer, confidence)

    def test_build_causal_index(self) -> None:
        texts = ["дождь идёт лужи появляются", "солнце светит тепло"]
        pairs = self.g.build_causal_index(texts, window=5)
        assert isinstance(pairs, dict)

    def test_induce_rules(self) -> None:
        self.g.train_text("кот домашнее животное")
        self.g.train_text("собака домашнее животное")
        rules = self.g.induce_rules(min_support=1, min_confidence=0.1)
        assert isinstance(rules, list)

    def test_self_model(self) -> None:
        self.g.train_text("Python программирование код")
        model = self.g.self_model("Python")
        assert isinstance(model, dict)
        assert "predicted_confidence" in model

    def test_train_multiple_documents(self) -> None:
        for i in range(5):
            self.g.train_text(f"документ {i} содержит информацию")
        assert self.g.documents_trained == 5
