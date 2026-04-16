"""
Тесты для EmbeddingTable — Word2Vec Skip-gram эмбеддинги.
"""
from __future__ import annotations

import json
import tempfile
from pathlib import Path

import pytest

from backend.service.embeddings import EmbeddingTable, EMBEDDING_DIM, _cosine


# ---------------------------------------------------------------------------
# EmbeddingTable: базовые операции
# ---------------------------------------------------------------------------


class TestEmbeddingTable:
    def setup_method(self) -> None:
        self.table = EmbeddingTable(dim=EMBEDDING_DIM)

    def test_init_default_dim(self) -> None:
        assert self.table.dim == EMBEDDING_DIM
        assert len(self.table.vectors) == 0

    def test_get_or_create_vector(self) -> None:
        vec = self.table.get_or_create(12345)
        assert len(vec) == EMBEDDING_DIM
        # Повторный вызов — тот же вектор
        vec2 = self.table.get_or_create(12345)
        assert vec is vec2

    def test_vectors_different_for_different_hashes(self) -> None:
        v1 = self.table.get_or_create(111)
        v2 = self.table.get_or_create(222)
        assert v1 != v2

    def test_save_load_roundtrip(self) -> None:
        self.table.get_or_create(100)
        self.table.get_or_create(200)
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            path = Path(f.name)
        try:
            self.table.save(path)
            loaded = EmbeddingTable(dim=EMBEDDING_DIM)
            loaded = loaded.load(path)
            assert len(loaded.vectors) == 2
            assert 100 in loaded.vectors
            assert 200 in loaded.vectors
        finally:
            path.unlink(missing_ok=True)

    def test_find_similar(self) -> None:
        # Создадим векторы и сделаем два похожими
        v1 = self.table.get_or_create(1)
        v2 = self.table.get_or_create(2)
        # Скопируем v1 → v2 (должен быть cosine ≈ 1.0)
        for i in range(len(v1)):
            v2[i] = v1[i]
        self.table.get_or_create(3)  # случайный, непохожий
        results = self.table.find_similar(1, top_k=2, min_sim=0.0)
        assert len(results) >= 1
        # Hash 2 должен быть первым (идентичный вектор)
        assert results[0][0] == 2
        # Score is 3rd element: (hash, word, score)
        assert results[0][2] > 0.99

    def test_sentence_vector(self) -> None:
        # Добавим слова в hash_to_word
        from backend.service.number_mind import djb2_hash
        h = djb2_hash("тест")
        self.table.get_or_create(h)
        vec = self.table.sentence_vector("тест проверка")
        # Должен вернуть вектор или None
        assert vec is None or len(vec) == EMBEDDING_DIM


# ---------------------------------------------------------------------------
# Cosine similarity
# ---------------------------------------------------------------------------


class TestCosine:
    def test_identical_vectors(self) -> None:
        v = [1.0, 2.0, 3.0]
        assert abs(_cosine(v, v) - 1.0) < 1e-6

    def test_orthogonal_vectors(self) -> None:
        v1 = [1.0, 0.0]
        v2 = [0.0, 1.0]
        assert abs(_cosine(v1, v2)) < 1e-6

    def test_zero_vector(self) -> None:
        v1 = [0.0, 0.0]
        v2 = [1.0, 2.0]
        assert _cosine(v1, v2) == 0.0

    def test_opposite_vectors(self) -> None:
        v1 = [1.0, 2.0]
        v2 = [-1.0, -2.0]
        assert abs(_cosine(v1, v2) - (-1.0)) < 1e-6


# ---------------------------------------------------------------------------
# Train on graph
# ---------------------------------------------------------------------------


class TestTrainOnGraph:
    def test_train_on_small_graph(self) -> None:
        from backend.service.number_mind import KnowledgeGraph
        g = KnowledgeGraph()
        g.train_text("кот кошка котёнок")
        g.train_text("собака щенок пёс")

        table = EmbeddingTable(dim=32)
        result = table.train_on_graph(
            edges=dict(g.edges),
            hash_to_word=dict(g._hash_to_word),
            all_hashes=set(g.patterns.keys()),
            epochs=3,
            lr=0.05,
            neg_samples=3,
        )
        assert result["vocab_size"] > 0
        assert result["pairs"] > 0
        assert result["loss"] >= 0.0
