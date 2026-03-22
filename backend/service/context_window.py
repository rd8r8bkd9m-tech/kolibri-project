"""Контекстное окно — долгосрочная и краткосрочная память разговора.

Три уровня:
1. Рабочая память (working_memory) — полные последние сообщения.
2. Сжатая память (compressed_memory) — саммари старых.
3. Ключевые слова — для обогащения запросов контекстом.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime
import re

_FOLLOWUP_HINT_WORDS: frozenset[str] = frozenset({
    "подробнее", "подробней", "подробно", "продолжай", "дальше",
    "еще", "ещё", "раскрой", "уточни", "детальнее", "детальней",
    "проще", "попроще", "короче", "пунктам", "пункты", "списком",
    "пример", "примеры", "простыми", "словами", "почему", "зачем",
    "сравни", "сравнить", "точно", "уверен", "верно", "правильно", "это",
})
_FOLLOWUP_FILLER_WORDS: frozenset[str] = frozenset({"а", "и", "ну", "да", "же", "то", "по", "что", "если", "еще", "ещё", "с"})


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
class FactRecord:
    """Нормализованный факт треда с типом."""

    role: str
    kind: str  # user_fact | assistant_fact | assistant_context | memory_ack
    text: str
    timestamp: datetime = field(default_factory=datetime.now)


@dataclass
class ContextWindow:
    """Управление контекстом разговора и многоходовыми диалогами.

    Автоматически сжимает старые сообщения, сохраняя ключевые слова.
    Обогащает новые запросы контекстом предыдущих.
    """

    max_tokens: int = 8192
    working_memory: list[Message] = field(default_factory=list)
    compressed_memory: list[str] = field(default_factory=list)
    thread_facts: list[str] = field(default_factory=list)
    thread_fact_records: list[FactRecord] = field(default_factory=list)
    _total_tokens: int = 0
    _min_working: int = 4  # Минимум сообщений в рабочей памяти
    _max_thread_facts: int = 240

    def add_message(self, role: str, content: str) -> None:
        """Добавить сообщение. Автосжатие при превышении max_tokens."""
        msg = Message(role=role, content=content)
        self.working_memory.append(msg)
        self._total_tokens += msg.token_count
        self._remember_facts(role=role, content=content)

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
        query_norm = re.sub(r"\s+", " ", (query or "").lower()).strip()
        for fact in self.get_recent_semantic_facts(query=query, limit=8, current_query=query):
            words = [
                w
                for w in re.findall(r"[a-zа-яё0-9]+", fact.lower())
                if len(w) >= 4
            ]
            context_words.extend(words[:6])

        # Приоритетно берём предыдущие пользовательские реплики:
        # они лучше отражают тему и меньше содержат сервисный шум.
        for text in self.get_recent_substantive_user_messages(limit=4, current_query=query):
            normalized = re.sub(r"\s+", " ", (text or "").lower()).strip()
            if not normalized:
                continue

            words = [
                w
                for w in re.findall(r"[a-zа-яё0-9]+", normalized)
                if len(w) >= 4
            ]
            context_words.extend(words[:8])

        unique = list(dict.fromkeys(context_words))  # сохраняем порядок
        if unique:
            return f"{query} (контекст: {', '.join(unique[:24])})"
        return query

    def get_relevant_facts(self, query: str, limit: int = 4) -> list[str]:
        """Вернуть релевантные факты по треду (или последние факты для ссылочного запроса)."""
        records = self._fact_records()
        if not records:
            return []
        q = re.sub(r"\s+", " ", (query or "").lower()).strip()
        q_tokens = {
            t
            for t in re.findall(r"[a-zа-яё0-9]+", q)
            if len(t) >= 3
        }
        referential_tokens = {
            "он", "она", "они", "его", "ее", "её", "их", "ему", "ей", "ним",
            "это", "этот", "эта", "эти", "там", "тогда", "такой",
            "я", "мой", "моя", "моё", "мне", "меня",
            "you", "it", "they", "them", "that", "those",
            "продолжай", "подробнее",
        }
        is_referential = bool(q_tokens & referential_tokens) or q.startswith(
            ("а как", "а что", "а почему", "а где", "а когда")
        )

        scored: list[tuple[float, str]] = []
        recent = list(reversed(records[-120:]))
        for idx, rec in enumerate(recent):
            fact = rec.text
            low = fact.lower()
            if self._is_placeholder_text(low):
                continue
            f_tokens = {
                t
                for t in re.findall(r"[a-zа-яё0-9]+", low)
                if len(t) >= 3
            }
            overlap = len(q_tokens & f_tokens) if q_tokens else 0
            if overlap == 0 and not is_referential:
                continue
            recency_bonus = max(0.0, 0.45 - idx * 0.01)
            kind_bonus = {
                "user_fact": 0.32,
                "memory_ack": 0.26,
                "assistant_fact": 0.18,
                "assistant_context": 0.10,
            }.get(rec.kind, 0.05)
            score = overlap * 1.1 + recency_bonus + kind_bonus
            if is_referential and overlap == 0:
                score += 0.12 if rec.kind in {"user_fact", "memory_ack"} else 0.0
            scored.append((score, fact))

        if not scored and is_referential:
            fallback: list[str] = []
            seen: set[str] = set()
            for rec in recent:
                if rec.kind not in {"user_fact", "memory_ack", "assistant_fact"}:
                    continue
                key = self._normalize_fact_key(rec.text)
                if not key or key in seen:
                    continue
                seen.add(key)
                fallback.append(rec.text)
                if len(fallback) >= max(1, limit):
                    break
            return fallback
        scored.sort(key=lambda x: x[0], reverse=True)

        out: list[str] = []
        seen: set[str] = set()
        for _score, fact in scored:
            key = self._normalize_fact_key(fact)
            if key in seen:
                continue
            seen.add(key)
            out.append(fact)
            if len(out) >= max(1, limit):
                break
        return out

    def get_last_substantive_user_message(self, current_query: str | None = None) -> str | None:
        """Последняя содержательная пользовательская реплика без текущего follow-up."""
        messages = self.get_recent_substantive_user_messages(limit=1, current_query=current_query)
        return messages[0] if messages else None

    def get_recent_substantive_user_messages(
        self,
        limit: int = 3,
        current_query: str | None = None,
    ) -> list[str]:
        """Последние содержательные пользовательские реплики без служебного follow-up."""
        out: list[str] = []
        seen: set[str] = set()
        current_norm = self._normalize_message(current_query)
        skipped_current = False
        for msg in reversed(self.working_memory):
            if str(getattr(msg, "role", "") or "").strip().lower() != "user":
                continue
            text = re.sub(r"\s+", " ", str(getattr(msg, "content", "") or "").strip())
            if len(text) < 6:
                continue
            normalized = self._normalize_message(text)
            if current_norm and normalized == current_norm and not skipped_current:
                skipped_current = True
                continue
            if self._is_placeholder_text(normalized):
                continue
            if self._is_followup_only_text(normalized):
                continue
            key = self._normalize_fact_key(text)
            if not key or key in seen:
                continue
            seen.add(key)
            out.append(text)
            if len(out) >= max(1, limit):
                break
        return out

    def get_recent_semantic_facts(
        self,
        query: str | None = None,
        limit: int = 8,
        current_query: str | None = None,
    ) -> list[str]:
        """Последние 5-10 смысловых фактов треда с учётом релевантности и давности."""
        records = self._fact_records()
        if not records:
            return []
        q = re.sub(r"\s+", " ", (query or "").lower()).strip()
        q_tokens = {
            t for t in re.findall(r"[a-zа-яё0-9]+", q)
            if len(t) >= 3
        }
        current_norm = self._normalize_message(current_query)
        scored: list[tuple[float, str]] = []
        for idx, rec in enumerate(reversed(records[-160:])):
            text = re.sub(r"\s+", " ", str(rec.text or "").strip())
            if len(text) < 8:
                continue
            low = text.lower()
            if self._is_placeholder_text(low):
                continue
            if rec.role == "user" and current_norm and self._normalize_message(text) == current_norm:
                continue
            if self._is_followup_only_text(low):
                continue
            tokens = {
                t for t in re.findall(r"[a-zа-яё0-9]+", low)
                if len(t) >= 3
            }
            overlap = len(q_tokens & tokens) if q_tokens else 0
            if q_tokens and overlap == 0 and idx > 48:
                continue
            recency_bonus = max(0.0, 0.72 - idx * 0.015)
            kind_bonus = {
                "user_fact": 0.36,
                "memory_ack": 0.28,
                "assistant_fact": 0.22,
                "assistant_context": 0.14,
            }.get(rec.kind, 0.08)
            score = recency_bonus + kind_bonus + overlap * 1.15
            scored.append((score, text))

        scored.sort(key=lambda item: item[0], reverse=True)
        out: list[str] = []
        seen: set[str] = set()
        for _score, text in scored:
            key = self._normalize_fact_key(text)
            if not key or key in seen:
                continue
            seen.add(key)
            out.append(text)
            if len(out) >= max(1, limit):
                break
        return out

    def clear(self) -> None:
        """Полный сброс памяти."""
        self.working_memory.clear()
        self.compressed_memory.clear()
        self.thread_facts.clear()
        self.thread_fact_records.clear()
        self._total_tokens = 0

    def get_stats(self) -> dict[str, int | float]:
        """Статистика контекстного окна."""
        return {
            "working_count": len(self.working_memory),
            "compressed_count": len(self.compressed_memory),
            "facts_count": len(self.thread_facts),
            "fact_records_count": len(self.thread_fact_records),
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

    def _normalize_fact_key(self, fact: str) -> str:
        return re.sub(r"[^a-zа-яё0-9]+", " ", (fact or "").lower()).strip()

    def _normalize_message(self, text: str | None) -> str:
        return re.sub(r"\s+", " ", str(text or "").strip().lower())

    def _is_followup_only_text(self, text: str) -> bool:
        low = self._normalize_message(text)
        if not low:
            return True
        tokens = [t for t in re.findall(r"[a-zа-яё0-9]+", low) if t]
        if not tokens:
            return True
        meaningful = [t for t in tokens if t not in _FOLLOWUP_FILLER_WORDS]
        if not meaningful:
            return True
        return all(t in _FOLLOWUP_HINT_WORDS for t in meaningful)

    def _is_placeholder_text(self, text: str) -> bool:
        low = re.sub(r"\s+", " ", (text or "").strip().lower())
        if not low:
            return True
        bad_markers = (
            "в моей локальной базе пока мало",
            "недостаточно локальных знаний",
            "по теме «",
            "добавьте материал",
            "добавьте факт (например",
            "пока нет подтвержденных фактов",
            "пока нет подтверждённых фактов",
        )
        return any(marker in low for marker in bad_markers)

    def _classify_fact_kind(self, role: str, fact: str) -> str:
        low = re.sub(r"\s+", " ", (fact or "").strip().lower())
        if self._is_placeholder_text(low):
            return "placeholder"
        if role == "user":
            return "user_fact"
        if low.startswith("принял. зафиксировал в контексте"):
            return "memory_ack"
        if low.startswith("по контексту текущего диалога"):
            return "assistant_context"
        return "assistant_fact"

    def _fact_records(self) -> list[FactRecord]:
        if self.thread_fact_records:
            return self.thread_fact_records
        # Обратная совместимость со старыми сериализованными окнами.
        return [FactRecord(role="assistant", kind="assistant_fact", text=f) for f in self.thread_facts]

    def _remember_facts(self, role: str, content: str) -> None:
        text = re.sub(r"\s+", " ", (content or "").strip())
        if len(text) < 10:
            return
        if role == "user" and "?" in text:
            return
        facts = self._extract_fact_units(role=role, text=text)
        if not facts:
            return
        for fact in facts:
            kind = self._classify_fact_kind(role, fact)
            if kind == "placeholder":
                continue
            key = self._normalize_fact_key(fact)
            if len(key) < 8:
                continue
            if any(self._normalize_fact_key(existing) == key for existing in self.thread_facts):
                # Обновляем давность факта: переносим в хвост.
                self.thread_facts = [
                    existing
                    for existing in self.thread_facts
                    if self._normalize_fact_key(existing) != key
                ]
                self.thread_fact_records = [
                    existing
                    for existing in self.thread_fact_records
                    if self._normalize_fact_key(existing.text) != key
                ]
            self.thread_facts.append(fact)
            self.thread_fact_records.append(FactRecord(role=role, kind=kind, text=fact))
        if len(self.thread_facts) > self._max_thread_facts:
            self.thread_facts = self.thread_facts[-self._max_thread_facts:]
        if len(self.thread_fact_records) > self._max_thread_facts:
            self.thread_fact_records = self.thread_fact_records[-self._max_thread_facts:]

    def _extract_fact_units(self, role: str, text: str) -> list[str]:
        low_full = text.lower()
        if "в моей локальной базе пока мало" in low_full:
            return []
        if "недостаточно локальных знаний" in low_full:
            return []
        if low_full.startswith("по теме «") and "локальной базе пока мало" in low_full:
            return []
        if low_full.startswith("добавьте материал"):
            return []

        chunks = [c.strip(" \t\n\r-—:;,.!?") for c in re.split(r"[.!?\n]+", text)]
        if not chunks:
            chunks = [text]

        request_prefixes = (
            "напомни", "расскажи", "объясни", "поясни", "подскажи",
            "скажи", "ответь", "перескажи", "сделай", "создай",
            "придумай", "переведи", "напиши", "покажи",
        )
        facts: list[str] = []
        for chunk in chunks:
            if not chunk:
                continue
            low = chunk.lower()
            if len(chunk) < 12 or len(chunk) > 260:
                continue
            if low.endswith("?"):
                continue
            if low.startswith(("что ", "кто ", "где ", "когда ", "почему ", "зачем ", "сколько ")):
                continue
            if low.startswith(request_prefixes):
                continue
            if role == "assistant" and low.startswith("по контексту текущего диалога"):
                parts = chunk.split(":", 1)
                chunk = parts[1].strip() if len(parts) == 2 else chunk
                low = chunk.lower()
            if role == "assistant" and low.startswith("краткая сводка новостей"):
                continue
            if role == "user" and low.startswith(("запомни", "научи:", "обучи:")):
                candidate = re.sub(r"^(запомни|научи:|обучи:)\s*", "", chunk, flags=re.IGNORECASE).strip()
                if len(candidate) >= 10:
                    facts.append(candidate)
                continue
            facts.append(chunk)
        return facts
