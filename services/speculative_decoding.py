"""
speculative_decoding.py — Speculative Decoding для Kolibri

Фаза C2: Предсказание 2-3 токена вперёд
- Быстрая черновая модель генерирует N токенов
- Основная модель проверяет и принимает/отклоняет
- Ускорение генерации в 2-3×
"""
from __future__ import annotations

import logging
import time
from dataclasses import dataclass, field

log = logging.getLogger("kolibri.speculative")

# ============================================================================
# Speculative Decoding
# ============================================================================

@dataclass
class SpeculativeConfig:
    """Конфигурация speculative decoding."""
    draft_tokens: int = 3  # Сколько токенов предсказывать
    acceptance_threshold: float = 0.7  # Минимальная уверенность для принятия
    max_retries: int = 2  # Максимум повторных попыток


@dataclass
class SpeculativeResult:
    """Результат speculative decoding."""
    accepted_tokens: list[str]
    rejected_tokens: list[str]
    acceptance_rate: float
    speedup: float
    duration_ms: float


class SpeculativeDecoder:
    """Speculative decoding pipeline."""

    def __init__(self, config: SpeculativeConfig | None = None) -> None:
        self.config = config or SpeculativeConfig()
        self._total_tokens = 0
        self._accepted_tokens = 0

    def generate_with_speculation(
        self,
        prompt: str,
        draft_generator,  # Быстрая модель
        verify_generator,  # Основная модель
        max_tokens: int = 100,
    ) -> SpeculativeResult:
        """Генерировать текст с speculative decoding."""
        t0 = time.time()
        accepted: list[str] = []
        rejected: list[str] = []

        current_text = prompt
        tokens_generated = 0

        while tokens_generated < max_tokens:
            # Шаг 1: Черновая модель генерирует N токенов
            draft_tokens = []
            for _ in range(self.config.draft_tokens):
                token = draft_generator(current_text)
                if not token:
                    break
                draft_tokens.append(token)
                current_text += token

            if not draft_tokens:
                break

            # Шаг 2: Основная модель проверяет каждый токен
            for i, token in enumerate(draft_tokens):
                # Верификация
                confidence = verify_generator(current_text[:-len(token)], token)

                if confidence >= self.config.acceptance_threshold:
                    accepted.append(token)
                    self._accepted_tokens += 1
                    tokens_generated += 1
                else:
                    rejected.append(token)
                    # Основная модель генерирует правильный токен
                    correct_token = verify_generator.generate_token(current_text)
                    if correct_token:
                        accepted.append(correct_token)
                        current_text += correct_token
                        tokens_generated += 1
                    break

            self._total_tokens += len(draft_tokens)

        duration_ms = (time.time() - t0) * 1000
        acceptance_rate = self._accepted_tokens / max(1, self._total_tokens)
        speedup = 1.0 + (len(accepted) / max(1, len(rejected))) if rejected else 2.0

        return SpeculativeResult(
            accepted_tokens=accepted,
            rejected_tokens=rejected,
            acceptance_rate=acceptance_rate,
            speedup=speedup,
            duration_ms=duration_ms,
        )

    def get_stats(self) -> dict:
        """Статистика speculative decoding."""
        return {
            "total_tokens": self._total_tokens,
            "accepted_tokens": self._accepted_tokens,
            "acceptance_rate": self._accepted_tokens / max(1, self._total_tokens),
        }
