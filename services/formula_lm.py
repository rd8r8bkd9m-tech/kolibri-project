"""Формульная языковая модель — генерация текста через эволюцию.

Уникальный подход Kolibri: вместо Transformer + backprop,
следующий токен предсказывается эволюционирующими формулами.
"""
from __future__ import annotations

import json
import math
import random
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from numpy.typing import NDArray


# ------------------------------------------------------------------
# GenerationFormula — одна формула-предиктор
# ------------------------------------------------------------------

@dataclass
class GenerationFormula:
    """Одна генерационная формула — 3-слойная MLP с GELU + residual.

    Трансформирует контекстный вектор (embed_dim,) → logits (vocab_size,).
    """

    embed_dim: int = 128
    hidden_dim: int = 256
    vocab_size: int = 32_000
    fitness: float = -1e6

    w1: NDArray[np.float32] = field(init=False, repr=False)
    b1: NDArray[np.float32] = field(init=False, repr=False)
    w2: NDArray[np.float32] = field(init=False, repr=False)
    b2: NDArray[np.float32] = field(init=False, repr=False)
    w3: NDArray[np.float32] = field(init=False, repr=False)
    b3: NDArray[np.float32] = field(init=False, repr=False)

    def __post_init__(self) -> None:
        s1 = math.sqrt(2.0 / (self.embed_dim + self.hidden_dim))
        s2 = math.sqrt(2.0 / (self.hidden_dim * 2))
        s3 = math.sqrt(2.0 / (self.hidden_dim + self.vocab_size))

        rng = np.random.default_rng()
        self.w1 = (rng.standard_normal((self.embed_dim, self.hidden_dim)) * s1).astype(np.float32)
        self.b1 = np.zeros(self.hidden_dim, dtype=np.float32)
        self.w2 = (rng.standard_normal((self.hidden_dim, self.hidden_dim)) * s2).astype(np.float32)
        self.b2 = np.zeros(self.hidden_dim, dtype=np.float32)
        self.w3 = (rng.standard_normal((self.hidden_dim, self.vocab_size)) * s3).astype(np.float32)
        self.b3 = np.zeros(self.vocab_size, dtype=np.float32)

    # ---------- forward ----------

    def forward(self, ctx_vector: NDArray[np.float32]) -> NDArray[np.float32]:
        """(embed_dim,) → (vocab_size,) logits."""
        h1 = _gelu(ctx_vector @ self.w1 + self.b1)
        h2 = _gelu(h1 @ self.w2 + self.b2) + h1  # residual
        return h2 @ self.w3 + self.b3

    # ---------- эволюция ----------

    def mutate(self, rate: float = 0.05) -> None:
        """Gaussian noise к весам с вероятностью rate."""
        rng = np.random.default_rng()
        for name in ("w1", "b1", "w2", "b2", "w3", "b3"):
            param: NDArray[np.float32] = getattr(self, name)
            mask = rng.random(param.shape) < rate
            noise = rng.standard_normal(param.shape).astype(np.float32) * 0.1
            setattr(self, name, param + mask * noise)

    def crossover(self, other: GenerationFormula) -> GenerationFormula:
        """Uniform crossover с другой формулой."""
        child = self.copy()
        rng = np.random.default_rng()
        for name in ("w1", "b1", "w2", "b2", "w3", "b3"):
            p_self: NDArray[np.float32] = getattr(self, name)
            p_other: NDArray[np.float32] = getattr(other, name)
            mask = rng.random(p_self.shape) < 0.5
            setattr(child, name, np.where(mask, p_self, p_other).copy())
        child.fitness = -1e6
        return child

    def copy(self) -> GenerationFormula:
        """Глубокая копия."""
        c = GenerationFormula.__new__(GenerationFormula)
        c.embed_dim = self.embed_dim
        c.hidden_dim = self.hidden_dim
        c.vocab_size = self.vocab_size
        c.fitness = self.fitness
        for name in ("w1", "b1", "w2", "b2", "w3", "b3"):
            setattr(c, name, getattr(self, name).copy())
        return c


# ------------------------------------------------------------------
# FormulaLM — языковая модель с популяцией формул
# ------------------------------------------------------------------

@dataclass
class FormulaLM:
    """Языковая модель на основе числовых формул.

    Эволюционирует num_formulas формул для предсказания следующего токена.
    """

    vocab_size: int = 32_000
    embed_dim: int = 128
    context_size: int = 512
    num_formulas: int = 64

    embeddings: NDArray[np.float32] = field(init=False, repr=False)
    formulas: list[GenerationFormula] = field(default_factory=list, repr=False)
    _best_idx: int = field(default=0, repr=False)
    generation: int = 0

    def __post_init__(self) -> None:
        scale = 1.0 / math.sqrt(self.embed_dim)
        self.embeddings = (
            np.random.default_rng()
            .standard_normal((self.vocab_size, self.embed_dim))
            .astype(np.float32)
            * scale
        )
        if not self.formulas:
            self.formulas = [
                GenerationFormula(
                    embed_dim=self.embed_dim,
                    hidden_dim=256,
                    vocab_size=self.vocab_size,
                )
                for _ in range(self.num_formulas)
            ]

    # ---------- inference ----------

    def predict_next(self, context_ids: list[int], temperature: float = 0.8) -> int:
        """Предсказать следующий токен."""
        ctx = context_ids[-self.context_size:]
        if not ctx:
            return random.randint(0, self.vocab_size - 1)

        # Эмбеддинги контекста с позиционным взвешиванием
        safe_ids = [min(max(i, 0), self.vocab_size - 1) for i in ctx]
        ctx_embeds = self.embeddings[safe_ids]
        n = len(ctx)
        positions = np.arange(n, dtype=np.float32)
        weights = np.exp(positions / n) / math.e
        weights /= weights.sum()
        ctx_vector = (ctx_embeds * weights[:, None]).sum(axis=0)

        # Лучшая формула → logits
        logits = self.formulas[self._best_idx].forward(ctx_vector)

        # Temperature + top-p sampling
        logits = logits / max(temperature, 1e-8)
        logits = _top_p_filter(logits, p=0.9)
        probs = _softmax(logits)
        return int(np.random.choice(len(probs), p=probs))

    def generate(
        self,
        prompt_ids: list[int],
        max_tokens: int = 256,
        temperature: float = 0.8,
        stop_token: int | None = None,
    ) -> list[int]:
        """Сгенерировать последовательность токенов."""
        generated = list(prompt_ids)
        for _ in range(max_tokens):
            next_id = self.predict_next(generated, temperature)
            generated.append(next_id)
            if stop_token is not None and next_id == stop_token:
                break
        return generated[len(prompt_ids):]

    # ---------- эволюция ----------

    def evolve(self, training_sequences: list[list[int]], generations: int = 100) -> None:
        """Эволюция формул: fitness = -perplexity."""
        if not training_sequences:
            return

        for gen in range(generations):
            # Оценка fitness
            for f in self.formulas:
                f.fitness = self._evaluate(f, training_sequences)

            # Сортировка
            ranked = sorted(range(len(self.formulas)), key=lambda i: self.formulas[i].fitness, reverse=True)
            self._best_idx = ranked[0]

            # Элитизм 25%
            elite_count = max(2, self.num_formulas // 4)
            new_formulas = [self.formulas[ranked[i]].copy() for i in range(elite_count)]

            # Дети
            while len(new_formulas) < self.num_formulas:
                pa = self._tournament(ranked)
                pb = self._tournament(ranked)
                child = self.formulas[pa].crossover(self.formulas[pb])
                rate = 0.05 if gen < generations // 2 else 0.02
                child.mutate(rate=rate)
                new_formulas.append(child)

            self.formulas = new_formulas
            self.generation += 1

    def get_perplexity(self, sequences: list[list[int]]) -> float:
        """Текущий perplexity лучшей формулы."""
        if not sequences:
            return 1e6
        neg_fitness = self._evaluate(self.formulas[self._best_idx], sequences)
        return math.exp(-neg_fitness) if neg_fitness > -500 else 1e6

    # ---------- сериализация ----------

    def save(self, path: Path) -> None:
        """Сохранить модель: embeddings + лучшая формула."""
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)

        best = self.formulas[self._best_idx]
        np.savez_compressed(
            str(path),
            embeddings=self.embeddings,
            w1=best.w1, b1=best.b1,
            w2=best.w2, b2=best.b2,
            w3=best.w3, b3=best.b3,
        )
        meta_path = path.with_suffix(".json")
        meta_path.write_text(json.dumps({
            "vocab_size": self.vocab_size,
            "embed_dim": self.embed_dim,
            "context_size": self.context_size,
            "num_formulas": self.num_formulas,
            "generation": self.generation,
            "best_fitness": float(best.fitness),
        }), encoding="utf-8")

    def load(self, path: Path) -> None:
        """Загрузить модель."""
        path = Path(path)
        data = np.load(str(path))
        meta_path = path.with_suffix(".json")
        meta = json.loads(meta_path.read_text(encoding="utf-8"))

        self.vocab_size = meta["vocab_size"]
        self.embed_dim = meta["embed_dim"]
        self.context_size = meta["context_size"]
        self.generation = meta.get("generation", 0)
        self.embeddings = data["embeddings"]

        # Восстанавливаем лучшую формулу и создаём остальные
        best = GenerationFormula(
            embed_dim=self.embed_dim, hidden_dim=256, vocab_size=self.vocab_size,
        )
        best.w1 = data["w1"]
        best.b1 = data["b1"]
        best.w2 = data["w2"]
        best.b2 = data["b2"]
        best.w3 = data["w3"]
        best.b3 = data["b3"]
        best.fitness = meta.get("best_fitness", 0.0)

        self.formulas = [best] + [
            GenerationFormula(embed_dim=self.embed_dim, hidden_dim=256, vocab_size=self.vocab_size)
            for _ in range(self.num_formulas - 1)
        ]
        self._best_idx = 0

    # ---------- приватные ----------

    def _evaluate(self, formula: GenerationFormula, sequences: list[list[int]]) -> float:
        """Fitness = средний log-probability на выборке."""
        total_log = 0.0
        count = 0
        sample = random.sample(sequences, min(32, len(sequences)))

        for seq in sample:
            max_len = min(len(seq), self.context_size)
            for i in range(1, max_len):
                safe_ids = [min(max(s, 0), self.vocab_size - 1) for s in seq[:i]]
                ctx_embeds = self.embeddings[safe_ids]
                n = len(safe_ids)
                positions = np.arange(n, dtype=np.float32)
                weights = np.exp(positions / n) / math.e
                weights /= weights.sum()
                ctx_vec = (ctx_embeds * weights[:, None]).sum(axis=0)

                logits = formula.forward(ctx_vec)
                probs = _softmax(logits)
                target = min(max(seq[i], 0), self.vocab_size - 1)
                p = max(float(probs[target]), 1e-10)
                total_log += math.log(p)
                count += 1

        return total_log / count if count > 0 else -1e6

    def _tournament(self, ranked: list[int], k: int = 3) -> int:
        candidates = random.sample(ranked, min(k, len(ranked)))
        return min(candidates, key=lambda i: ranked.index(i))


# ------------------------------------------------------------------
# Утилиты
# ------------------------------------------------------------------

def _gelu(x: NDArray[np.float32]) -> NDArray[np.float32]:
    return x * 0.5 * (1.0 + np.tanh(np.sqrt(2.0 / np.pi) * (x + 0.044715 * x**3)))


def _softmax(logits: NDArray[np.float32]) -> NDArray[np.float32]:
    x = logits - logits.max()
    e = np.exp(x)
    return e / e.sum()


def _top_p_filter(logits: NDArray[np.float32], p: float = 0.9) -> NDArray[np.float32]:
    sorted_idx = np.argsort(logits)[::-1]
    sorted_logits = logits[sorted_idx]
    probs = _softmax(sorted_logits)
    cumsum = np.cumsum(probs)
    cutoff = int(np.searchsorted(cumsum, p)) + 1
    mask = np.full_like(logits, -1e10)
    mask[sorted_idx[:cutoff]] = logits[sorted_idx[:cutoff]]
    return mask
