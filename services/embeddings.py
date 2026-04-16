"""
embeddings.py — Обучаемые эмбеддинги слов Kolibri

Замена DJB2 хеш-паттернов на НАСТОЯЩИЕ семантические вектора.

Ключевое отличие от DJB2:
- DJB2: "кот" → случайные 64 цифры, "кошка" → ДРУГИЕ случайные 64 цифры
- Embeddings: "кот" → [0.82, -0.15, ...], "кошка" → [0.79, -0.12, ...] (ПОХОЖИЕ!)

Алгоритм обучения: Word2Vec Skip-gram с негативным семплингом
- Positive pairs: из рёбер графа знаний (co-occurrence)
- Negative sampling: случайные пары → вектора отталкиваются
- SGD с linear decay learning rate

Результат: cosine_similarity("кот", "кошка") > 0.7
"""
from __future__ import annotations

import json
import logging
import math
import random
import time
from pathlib import Path
from typing import Optional

log = logging.getLogger("kolibri.embeddings")

# ---------------------------------------------------------------------------
# Константы
# ---------------------------------------------------------------------------

EMBEDDING_DIM = 64       # Размерность вектора (совпадает с KLM_PATTERN_SIZE)
MAX_VOCAB = 131072       # Макс слов в таблице
MIN_EDGE_WEIGHT = 0.3    # Мин вес ребра для обучения


# ---------------------------------------------------------------------------
# Вспомогательные функции
# ---------------------------------------------------------------------------

def _cosine(v1: list[float], v2: list[float]) -> float:
    """Косинусное сходство двух векторов."""
    dot = sum(a * b for a, b in zip(v1, v2))
    n1 = math.sqrt(sum(a * a for a in v1))
    n2 = math.sqrt(sum(b * b for b in v2))
    if n1 < 1e-10 or n2 < 1e-10:
        return 0.0
    return dot / (n1 * n2)


def _l2_normalize(v: list[float]) -> list[float]:
    """L2-нормализация вектора."""
    norm = math.sqrt(sum(x * x for x in v))
    if norm < 1e-10:
        return v
    return [x / norm for x in v]


def _sigmoid(x: float) -> float:
    """Сигмоида с клиппингом для численной стабильности."""
    x = max(-15.0, min(15.0, x))
    return 1.0 / (1.0 + math.exp(-x))


# ---------------------------------------------------------------------------
# EmbeddingTable — обучаемые вектора слов
# ---------------------------------------------------------------------------

class EmbeddingTable:
    """
    Таблица обучаемых эмбеддингов слов.

    Каждое слово (по DJB2 хешу) → 64-мерный float вектор.
    Обучение на co-occurrence из графа знаний (Word2Vec-style).

    Зачем:
    - DJB2 хеш → СЛУЧАЙНЫЕ цифры → "кот" ≠ "кошка" (similarity ~0.3)
    - Embeddings → ОБУЧЕННЫЕ вектора → "кот" ≈ "кошка" (similarity ~0.8)

    Это ПЕРВЫЙ НАСТОЯЩИЙ шаг к семантическому AI в Kolibri.
    """

    def __init__(self, dim: int = EMBEDDING_DIM) -> None:
        self.dim = dim
        self.vectors: dict[int, list[float]] = {}   # hash → vector
        self._word_map: dict[int, str] = {}          # hash → word (отладка)
        self.trained_pairs: int = 0
        self.epochs_completed: int = 0
        self._training_loss_history: list[float] = []

    # ------------------------------------------------------------------
    # Базовые операции
    # ------------------------------------------------------------------

    def get_or_create(self, word_hash: int, word: str = "") -> list[float]:
        """
        Получить вектор слова. Если не существует — создать с Xavier init.

        Xavier инициализация: N(0, 1/√dim) — стандарт для нейросетей.
        Это гарантирует, что начальные вектора имеют разумный масштаб
        и не вырождаются при dot product.
        """
        if word_hash not in self.vectors:
            if len(self.vectors) >= MAX_VOCAB:
                return [0.0] * self.dim  # Overflow protection
            scale = 1.0 / math.sqrt(self.dim)
            self.vectors[word_hash] = [
                random.gauss(0.0, scale) for _ in range(self.dim)
            ]
            if word:
                self._word_map[word_hash] = word
        return self.vectors[word_hash]

    def has(self, word_hash: int) -> bool:
        """Есть ли эмбеддинг для данного хеша."""
        return word_hash in self.vectors

    @property
    def vocab_size(self) -> int:
        """Размер словаря."""
        return len(self.vectors)

    # ------------------------------------------------------------------
    # Обучение: Word2Vec Skip-gram
    # ------------------------------------------------------------------

    def train_pair(self, h1: int, h2: int, lr: float = 0.025) -> float:
        """
        Positive pair: сблизить вектора слов h1 и h2.

        Skip-gram: если слова встречаются вместе (ребро в графе),
        их dot product должен быть высоким → sigmoid(dot) → 1.

        Gradient: ∂L/∂v1 = (1 - σ(dot)) · v2
                  ∂L/∂v2 = (1 - σ(dot)) · v1
        """
        v1 = self.get_or_create(h1)
        v2 = self.get_or_create(h2)

        dot = sum(a * b for a, b in zip(v1, v2))
        sig = _sigmoid(dot)

        # Positive: gradient = (1 - sigmoid) * lr
        grad = (1.0 - sig) * lr

        for i in range(self.dim):
            g1 = grad * v2[i]
            g2 = grad * v1[i]
            v1[i] += g1
            v2[i] += g2

        self.trained_pairs += 1
        return -math.log(sig + 1e-10)

    def train_negative(self, h1: int, h_neg: int, lr: float = 0.025) -> float:
        """
        Negative pair: оттолкнуть вектора.

        Случайное слово h_neg НЕ связано с h1 →
        dot product должен быть низким → sigmoid(dot) → 0.

        Gradient: ∂L/∂v1 = -σ(dot) · v_neg
                  ∂L/∂v_neg = -σ(dot) · v1
        """
        v1 = self.get_or_create(h1)
        v_neg = self.get_or_create(h_neg)

        dot = sum(a * b for a, b in zip(v1, v_neg))
        sig = _sigmoid(dot)

        # Negative: gradient = -sigmoid * lr
        grad = -sig * lr

        for i in range(self.dim):
            g1 = grad * v_neg[i]
            g2 = grad * v1[i]
            v1[i] += g1
            v_neg[i] += g2

        return -math.log(1.0 - sig + 1e-10)

    def train_on_graph(
        self,
        edges: dict,
        hash_to_word: dict[int, str],
        all_hashes: set[int] | list[int] | None = None,
        epochs: int = 5,
        lr: float = 0.025,
        neg_samples: int = 5,
    ) -> dict:
        """
        Полный цикл обучения на рёбрах графа знаний.

        Word2Vec-style:
        1. Каждое ребро (A, B) = positive pair → вектора A, B сближаются
        2. Для каждого positive — neg_samples случайных negative pairs
        3. Повторяем epochs раз с убывающим learning rate

        Weighted training: сильные рёбра (высокий co-occurrence)
        генерируют больше обучающих пар → частые контексты важнее.

        Returns: {"loss", "pairs", "epochs", "vocab_size", "duration_ms"}
        """
        if not edges:
            return {"loss": 0.0, "pairs": 0, "epochs": 0, "vocab_size": 0}

        t0 = time.time()

        # --- Инициализация словаря ---
        for h, word in hash_to_word.items():
            self.get_or_create(h, word)

        all_hash_list = list(all_hashes) if all_hashes else list(self.vectors.keys())
        if len(all_hash_list) < 2:
            return {"loss": 0.0, "pairs": 0, "epochs": 0, "vocab_size": len(self.vectors)}

        # --- Подготовка обучающих пар (с учётом весов рёбер) ---
        edge_list: list[tuple[int, int, float]] = []
        for (src_h, tgt_h), edge in edges.items():
            if edge.weight >= MIN_EDGE_WEIGHT:
                edge_list.append((src_h, tgt_h, edge.weight))

        if not edge_list:
            # Если нет достаточно сильных рёбер, берём все
            for (src_h, tgt_h), edge in edges.items():
                edge_list.append((src_h, tgt_h, edge.weight))

        total_loss = 0.0
        total_pairs = 0

        for epoch in range(epochs):
            epoch_loss = 0.0
            # Linear decay: lr уменьшается к концу
            current_lr = lr * (1.0 - epoch / (epochs + 1))

            random.shuffle(edge_list)

            for src_h, tgt_h, weight in edge_list:
                # Weighted repetitions: сильные связи → больше примеров
                reps = max(1, min(4, int(weight * 3)))

                for _ in range(reps):
                    # Positive pair
                    loss = self.train_pair(src_h, tgt_h, lr=current_lr)
                    epoch_loss += loss
                    total_pairs += 1

                    # Negative sampling
                    for _ in range(neg_samples):
                        neg_h = random.choice(all_hash_list)
                        if neg_h != src_h and neg_h != tgt_h:
                            loss_neg = self.train_negative(src_h, neg_h, lr=current_lr * 0.5)
                            epoch_loss += loss_neg

            avg_epoch_loss = epoch_loss / max(total_pairs, 1)
            self._training_loss_history.append(avg_epoch_loss)
            total_loss += epoch_loss
            self.epochs_completed += 1

        duration = round((time.time() - t0) * 1000, 1)
        avg_loss = total_loss / max(total_pairs, 1)

        log.info(
            "Embeddings trained: %d pairs, %d epochs, loss=%.4f, vocab=%d (%.0fms)",
            total_pairs, epochs, avg_loss, len(self.vectors), duration,
        )

        return {
            "loss": round(avg_loss, 6),
            "pairs": total_pairs,
            "epochs": epochs,
            "vocab_size": len(self.vectors),
            "duration_ms": duration,
        }

    def train_incremental(
        self,
        new_edges: list[tuple[int, int, float]],
        all_hashes: list[int],
        lr: float = 0.015,
        neg_samples: int = 3,
    ) -> float:
        """
        Инкрементальное обучение — для новых рёбер после chat/train.

        Не проходит весь граф заново, а обучает только на свежих связях.
        Быстро (мс, не секунды) → можно вызывать после каждого ответа.
        """
        if not new_edges or len(all_hashes) < 2:
            return 0.0

        total_loss = 0.0
        count = 0

        for src_h, tgt_h, weight in new_edges:
            reps = max(1, min(3, int(weight * 2)))
            for _ in range(reps):
                loss = self.train_pair(src_h, tgt_h, lr=lr)
                total_loss += loss
                count += 1

                for _ in range(neg_samples):
                    neg_h = random.choice(all_hashes)
                    if neg_h != src_h and neg_h != tgt_h:
                        self.train_negative(src_h, neg_h, lr=lr * 0.5)

        return total_loss / max(count, 1)

    # ------------------------------------------------------------------
    # Сходство и поиск
    # ------------------------------------------------------------------

    def cosine_similarity(self, h1: int, h2: int) -> float:
        """Косинусное сходство между двумя словами."""
        if h1 not in self.vectors or h2 not in self.vectors:
            return 0.0
        return _cosine(self.vectors[h1], self.vectors[h2])

    def find_similar(
        self,
        word_hash: int,
        top_k: int = 10,
        min_sim: float = 0.1,
    ) -> list[tuple[int, str, float]]:
        """
        Найти top_k ближайших слов по cosine similarity.

        Returns: [(hash, word, similarity), ...]
        """
        if word_hash not in self.vectors:
            return []

        v = self.vectors[word_hash]
        norm_v = math.sqrt(sum(a * a for a in v))
        if norm_v < 1e-10:
            return []

        results: list[tuple[int, str, float]] = []

        for h, vec in list(self.vectors.items()):
            if h == word_hash:
                continue
            dot = sum(a * b for a, b in zip(v, vec))
            norm_h = math.sqrt(sum(b * b for b in vec))
            if norm_h < 1e-10:
                continue
            sim = dot / (norm_v * norm_h)
            if sim >= min_sim:
                word = self._word_map.get(h, f"#{h}")
                results.append((h, word, sim))

        results.sort(key=lambda x: x[2], reverse=True)
        return results[:top_k]

    def sentence_vector(self, word_hashes: list[int] | set[int] | frozenset[int]) -> list[float]:
        """
        Вектор предложения = среднее эмбеддингов его слов + L2 norm.

        Простой, но эффективный метод (baseline для Sentence-BERT).
        Для Kolibri достаточно: sentence similarity ≈ word overlap + semantics.
        """
        result = [0.0] * self.dim
        count = 0

        for h in word_hashes:
            if h in self.vectors:
                v = self.vectors[h]
                for i in range(self.dim):
                    result[i] += v[i]
                count += 1

        if count == 0:
            return result

        # Average
        result = [x / count for x in result]
        # L2 normalize
        return _l2_normalize(result)

    def sentence_similarity(
        self,
        hashes1: set[int] | frozenset[int],
        hashes2: set[int] | frozenset[int],
    ) -> float:
        """Семантическое сходство двух предложений через эмбеддинги."""
        v1 = self.sentence_vector(list(hashes1))
        v2 = self.sentence_vector(list(hashes2))
        return _cosine(v1, v2)

    # ------------------------------------------------------------------
    # Персистенция
    # ------------------------------------------------------------------

    def save(self, path: str | Path) -> None:
        """Сохранить эмбеддинги на диск (JSON)."""
        data = {
            "version": 1,
            "dim": self.dim,
            "trained_pairs": self.trained_pairs,
            "epochs_completed": self.epochs_completed,
            "vocab_size": len(self.vectors),
            "timestamp": time.time(),
            "loss_history": self._training_loss_history[-20:],
            "vectors": {
                str(h): [round(x, 6) for x in v]
                for h, v in self.vectors.items()
            },
            "word_map": {
                str(h): w for h, w in self._word_map.items()
            },
        }
        p = Path(path)
        p.parent.mkdir(parents=True, exist_ok=True)
        tmp = p.with_suffix(".tmp")
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False)
        tmp.rename(p)
        log.info(
            "Embeddings saved: vocab=%d, pairs=%d, epochs=%d → %s",
            len(self.vectors), self.trained_pairs, self.epochs_completed, path,
        )

    @classmethod
    def load(cls, path: str | Path) -> EmbeddingTable:
        """Загрузить эмбеддинги с диска."""
        p = Path(path)
        if not p.exists():
            raise FileNotFoundError(f"Embeddings not found: {path}")

        with open(p, "r", encoding="utf-8") as f:
            data = json.load(f)

        table = cls(dim=data.get("dim", EMBEDDING_DIM))
        table.trained_pairs = data.get("trained_pairs", 0)
        table.epochs_completed = data.get("epochs_completed", 0)
        table._training_loss_history = data.get("loss_history", [])

        for h_str, v in data.get("vectors", {}).items():
            table.vectors[int(h_str)] = v
        for h_str, w in data.get("word_map", {}).items():
            table._word_map[int(h_str)] = w

        log.info(
            "Embeddings loaded: vocab=%d, pairs=%d, epochs=%d ← %s",
            len(table.vectors), table.trained_pairs, table.epochs_completed, path,
        )
        return table

    @classmethod
    def load_or_create(cls, path: str | Path, dim: int = EMBEDDING_DIM) -> EmbeddingTable:
        """Загрузить если есть, иначе создать новую таблицу."""
        try:
            return cls.load(path)
        except (FileNotFoundError, Exception) as e:
            log.info("EmbeddingTable: новая таблица (%s)", e)
            return cls(dim=dim)

    # ------------------------------------------------------------------
    # Статистика
    # ------------------------------------------------------------------

    def get_stats(self) -> dict:
        """Статистика эмбеддингов."""
        avg_norm = 0.0
        if self.vectors:
            norms = [math.sqrt(sum(x * x for x in v)) for v in self.vectors.values()]
            avg_norm = sum(norms) / len(norms)

        return {
            "vocab_size": len(self.vectors),
            "dim": self.dim,
            "trained_pairs": self.trained_pairs,
            "epochs_completed": self.epochs_completed,
            "avg_vector_norm": round(avg_norm, 4),
            "last_loss": round(self._training_loss_history[-1], 6) if self._training_loss_history else 0.0,
        }
