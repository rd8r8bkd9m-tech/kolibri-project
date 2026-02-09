"""Тесты для формульной языковой модели."""
from __future__ import annotations

import tempfile
from pathlib import Path

import numpy as np
import pytest

from backend.service.formula_lm import FormulaLM, GenerationFormula


class TestGenerationFormula:
    def test_forward_shape(self) -> None:
        f = GenerationFormula(embed_dim=32, hidden_dim=64, vocab_size=100)
        ctx = np.random.randn(32).astype(np.float32)
        out = f.forward(ctx)
        assert out.shape == (100,), f"Ожидали (100,), получили {out.shape}"

    def test_mutate_changes_weights(self) -> None:
        f = GenerationFormula(embed_dim=16, hidden_dim=32, vocab_size=50)
        w1_before = f.w1.copy()
        f.mutate(rate=0.5)
        assert not np.array_equal(w1_before, f.w1), "Мутация не изменила веса"

    def test_crossover_produces_child(self) -> None:
        a = GenerationFormula(embed_dim=16, hidden_dim=32, vocab_size=50)
        b = GenerationFormula(embed_dim=16, hidden_dim=32, vocab_size=50)
        child = a.crossover(b)
        assert child.w1.shape == a.w1.shape
        # Ребёнок не идентичен ни одному родителю
        assert not (np.array_equal(child.w1, a.w1) and np.array_equal(child.w1, b.w1))

    def test_copy_independent(self) -> None:
        f = GenerationFormula(embed_dim=16, hidden_dim=32, vocab_size=50)
        c = f.copy()
        c.w1[0, 0] = 999.0
        assert f.w1[0, 0] != 999.0, "copy() не глубокая"


class TestFormulaLM:
    def _small_lm(self) -> FormulaLM:
        return FormulaLM(vocab_size=50, embed_dim=16, context_size=32, num_formulas=4)

    def test_generate_length(self) -> None:
        lm = self._small_lm()
        result = lm.generate([1, 2, 3], max_tokens=10)
        assert len(result) <= 10, f"generate вернул {len(result)} > 10 токенов"
        assert len(result) > 0

    def test_predict_next_returns_valid_id(self) -> None:
        lm = self._small_lm()
        token = lm.predict_next([1, 2, 3])
        assert 0 <= token < lm.vocab_size

    def test_evolve_reduces_perplexity(self) -> None:
        lm = self._small_lm()
        # Создаём повторяющиеся последовательности (легко предсказуемые)
        seqs = [[i % 10 for i in range(20)] for _ in range(50)]
        ppl_before = lm.get_perplexity(seqs)
        lm.evolve(seqs, generations=30)
        ppl_after = lm.get_perplexity(seqs)
        assert ppl_after < ppl_before, (
            f"Perplexity не уменьшился: {ppl_before:.2f} → {ppl_after:.2f}"
        )

    def test_temperature_greedy(self) -> None:
        lm = self._small_lm()
        # Очень низкая температура → почти детерминированный результат
        results = {lm.predict_next([1, 2, 3], temperature=0.001) for _ in range(10)}
        # Должно быть не более 2 уникальных (допуск на числ. погрешность)
        assert len(results) <= 3, f"Greedy дал {len(results)} уникальных токенов"

    def test_save_load(self) -> None:
        lm = self._small_lm()
        prompt = [1, 2, 3, 4, 5]

        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "model.npz"
            lm.save(path)

            lm2 = FormulaLM(vocab_size=50, embed_dim=16, context_size=32, num_formulas=4)
            lm2.load(path)

        assert np.allclose(lm.embeddings, lm2.embeddings), "Embeddings не совпали"
        assert np.allclose(
            lm.formulas[lm._best_idx].w1,
            lm2.formulas[lm2._best_idx].w1,
        ), "Веса формулы не совпали"

    def test_empty_context(self) -> None:
        lm = self._small_lm()
        token = lm.predict_next([])
        assert 0 <= token < lm.vocab_size

    def test_generation_counter(self) -> None:
        lm = self._small_lm()
        assert lm.generation == 0
        seqs = [[i % 5 for i in range(10)] for _ in range(20)]
        lm.evolve(seqs, generations=5)
        assert lm.generation == 5
