"""BPE-токенизатор для Kolibri — субсловные единицы для генерации текста.

Поддержка русского и английского языков. Vocab size до 32K.
"""
from __future__ import annotations

import json
import re
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path


_WORD_RE = re.compile(r"[а-яёa-z]+|[А-ЯЁA-Z][а-яёa-z]*|\d+|[^\s]")

_END_OF_WORD = "</w>"


@dataclass
class BPETokenizer:
    """Byte Pair Encoding токенизатор.

    Разбивает текст на субсловные единицы для генерации.
    """

    vocab: dict[str, int] = field(default_factory=dict)
    merges: list[tuple[str, str]] = field(default_factory=list)
    vocab_size: int = 32_000
    _inverse_vocab: dict[int, str] = field(default_factory=dict, repr=False)

    # ------------------------------------------------------------------
    # Обучение

    def train(self, texts: list[str]) -> None:
        """Обучить BPE на корпусе текстов."""
        # Частоты слов
        word_freqs: Counter[str] = Counter()
        for text in texts:
            words = _WORD_RE.findall(text)
            for w in words:
                key = " ".join(w) + " " + _END_OF_WORD
                word_freqs[key] += 1

        if not word_freqs:
            return

        # Начальный словарь — все уникальные символы
        chars: set[str] = set()
        for word in word_freqs:
            for ch in word.split():
                chars.add(ch)
        self.vocab = {ch: i for i, ch in enumerate(sorted(chars))}
        idx = len(self.vocab)
        self.merges = []

        # Итеративное слияние наиболее частых пар
        while len(self.vocab) < self.vocab_size:
            pairs: Counter[tuple[str, str]] = Counter()
            for word, freq in word_freqs.items():
                symbols = word.split()
                for i in range(len(symbols) - 1):
                    pairs[(symbols[i], symbols[i + 1])] += freq

            if not pairs:
                break

            best = pairs.most_common(1)[0][0]
            self.merges.append(best)
            merged = best[0] + best[1]
            self.vocab[merged] = idx
            idx += 1

            # Применяем слияние ко всем словам
            new_freqs: Counter[str] = Counter()
            for word, freq in word_freqs.items():
                new_word = word.replace(f"{best[0]} {best[1]}", merged)
                new_freqs[new_word] = freq
            word_freqs = new_freqs

        self._rebuild_inverse()

    # ------------------------------------------------------------------
    # Кодирование / декодирование

    def encode(self, text: str) -> list[int]:
        """Текст -> список token IDs."""
        words = _WORD_RE.findall(text)
        tokens: list[int] = []
        for word in words:
            symbols = list(word) + [_END_OF_WORD]
            for a, b in self.merges:
                i = 0
                while i < len(symbols) - 1:
                    if symbols[i] == a and symbols[i + 1] == b:
                        symbols[i] = a + b
                        del symbols[i + 1]
                    else:
                        i += 1
            for s in symbols:
                tid = self.vocab.get(s)
                if tid is not None:
                    tokens.append(tid)
                # Неизвестные символы — пропускаем (graceful degradation)
        return tokens

    def decode(self, token_ids: list[int]) -> str:
        """Список token IDs -> текст."""
        parts: list[str] = []
        for tid in token_ids:
            sym = self._inverse_vocab.get(tid)
            if sym is not None:
                parts.append(sym)
        text = "".join(parts).replace(_END_OF_WORD, " ")
        return text.strip()

    # ------------------------------------------------------------------
    # Сериализация

    def save(self, path: Path) -> None:
        """Сохранить vocab + merges в JSON."""
        data = {
            "vocab": self.vocab,
            "merges": [list(m) for m in self.merges],
            "vocab_size": self.vocab_size,
        }
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")

    def load(self, path: Path) -> None:
        """Загрузить vocab + merges из JSON."""
        path = Path(path)
        data = json.loads(path.read_text(encoding="utf-8"))
        self.vocab = data["vocab"]
        self.merges = [tuple(m) for m in data["merges"]]  # type: ignore[misc]
        self.vocab_size = data.get("vocab_size", 32_000)
        self._rebuild_inverse()

    # ------------------------------------------------------------------
    # Утилиты

    def __len__(self) -> int:
        return len(self.vocab)

    def _rebuild_inverse(self) -> None:
        self._inverse_vocab = {v: k for k, v in self.vocab.items()}
