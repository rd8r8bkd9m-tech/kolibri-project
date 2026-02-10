"""Контекстное окно — долгосрочная и краткосрочная память разговора.

Три уровня:
1. Рабочая память (working_memory) — полные последние сообщения.
2. Сжатая память (compressed_memory) — саммари старых.
3. Ключевые слова — для обогащения запросов контекстом.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime


@dataclass
class Message:
    """Одно сообщение в диалоге."""

    role: str  # "user" | "assistant"
    content: str
    timestamp: datetime = field(default_factory=datetime.now)
    token_count: int = 0

    def __post_init__(self) -> None:
        if self.token_count == 0:
            self.token_count = len(self.content.split())


@dataclass
class ContextWindow:
    """Управление контекстом разговора и многоходовыми диалогами.

    Автоматически сжимает старые сообщения, сохраняя ключевые слова.
    Обогащает новые запросы контекстом предыдущих.
    """

    max_tokens: int = 8192
    working_memory: list[Message] = field(default_factory=list)
    compressed_memory: list[str] = field(default_factory=list)
    _total_tokens: int = 0
    _min_working: int = 4  # Минимум сообщений в рабочей памяти

    def add_message(self, role: str, content: str) -> None:
        """Добавить сообщение. Автосжатие при превышении max_tokens."""
        msg = Message(role=role, content=content)
        self.working_memory.append(msg)
        self._total_tokens += msg.token_count

        # Автосжатие: пока превышен лимит и можно сжимать
        while (
            self._total_tokens > self.max_tokens
            and len(self.working_memory) > self._min_working
        ):
            oldest = self.working_memory.pop(0)
            self._total_tokens -= oldest.token_count
            summary = self._compress_message(oldest)
            self.compressed_memory.append(summary)

    def get_context(self) -> str:
        """Полный контекст для модели."""
        parts: list[str] = []

        # Сжатая память
        if self.compressed_memory:
            parts.append("=== Предыдущий контекст ===")
            for summary in self.compressed_memory[-5:]:
                parts.append(f"• {summary}")

        # Рабочая память
        if self.working_memory:
            parts.append("=== Текущий диалог ===")
            for msg in self.working_memory:
                prefix = "Пользователь" if msg.role == "user" else "Kolibri"
                parts.append(f"{prefix}: {msg.content}")

        return "\n".join(parts)

    def get_query_with_context(self, query: str) -> str:
        """Обогатить запрос ключевыми словами из последних сообщений."""
        context_words: list[str] = []
        for msg in self.working_memory[-3:]:
            words = [
                w
                for w in msg.content.lower().split()
                if len(w) > 5 and w.isalpha()
            ]
            context_words.extend(words[:5])

        unique = list(dict.fromkeys(context_words))  # сохраняем порядок
        if unique:
            return f"{query} (контекст: {', '.join(unique[:8])})"
        return query

    def clear(self) -> None:
        """Полный сброс памяти."""
        self.working_memory.clear()
        self.compressed_memory.clear()
        self._total_tokens = 0

    def get_stats(self) -> dict[str, int | float]:
        """Статистика контекстного окна."""
        return {
            "working_count": len(self.working_memory),
            "compressed_count": len(self.compressed_memory),
            "total_tokens": self._total_tokens,
            "max_tokens": self.max_tokens,
            "usage_pct": round(
                self._total_tokens / self.max_tokens * 100, 1
            )
            if self.max_tokens > 0
            else 0.0,
        }

    def _compress_message(self, msg: Message) -> str:
        """Сжать сообщение в краткое саммари."""
        content = msg.content
        # 1-е предложение
        sentences = content.split(".")
        first_sentence = sentences[0].strip() if sentences else content[:100]
        if len(first_sentence) > 120:
            first_sentence = first_sentence[:120] + "…"

        # Ключевые слова > 5 символов
        keywords = [
            w for w in content.split() if len(w) > 5 and w.isalpha()
        ][:5]

        prefix = "Q" if msg.role == "user" else "A"
        if keywords:
            return f"{prefix}: {first_sentence} [{', '.join(keywords)}]"
        return f"{prefix}: {first_sentence}"
