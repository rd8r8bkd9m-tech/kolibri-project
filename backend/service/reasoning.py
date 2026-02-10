"""Chain-of-Thought — пошаговое рассуждение для Kolibri OS.

Разбивает запрос на шаги анализа и строит цепочку рассуждений
перед формированием ответа (подход «думай шаг за шагом»).
"""
from __future__ import annotations

import re
import time
from dataclasses import dataclass, field
from enum import Enum, auto


class StepType(Enum):
    """Тип шага рассуждения."""
    PARSE = auto()       # Разбор запроса
    RETRIEVE = auto()    # Поиск знаний
    REASON = auto()      # Вывод / логика
    CALCULATE = auto()   # Числовой расчёт
    SYNTHESIZE = auto()  # Синтез ответа
    VERIFY = auto()      # Проверка


@dataclass
class ThinkingStep:
    """Один шаг цепочки рассуждений."""
    step_type: StepType
    description: str
    result: str = ""
    confidence: float = 0.0
    elapsed_ms: float = 0.0


@dataclass
class ChainOfThought:
    """Движок пошагового рассуждения.

    Анализирует запрос, строит цепочку шагов и формирует
    прозрачную «мысль» перед ответом.
    """

    max_steps: int = 8
    steps: list[ThinkingStep] = field(default_factory=list)
    _keywords: dict[str, list[str]] = field(default_factory=dict, repr=False)

    def __post_init__(self) -> None:
        if not self._keywords:
            self._keywords = {
                "calculate": [
                    "сколько", "вычисли", "посчитай", "калькул",
                    "calculate", "compute", "how many", "sum",
                    "разниц", "процент", "плюс", "минус",
                ],
                "compare": [
                    "сравни", "отличие", "разница", "лучше", "хуже",
                    "compare", "difference", "versus", "vs",
                ],
                "explain": [
                    "объясни", "почему", "зачем", "как работает",
                    "explain", "why", "how does", "what is",
                ],
                "list": [
                    "перечисли", "список", "какие", "назови",
                    "list", "enumerate", "which", "name all",
                ],
                "create": [
                    "создай", "напиши", "сгенерируй", "придумай",
                    "create", "write", "generate", "compose",
                ],
            }

    def analyze_query(self, query: str) -> list[ThinkingStep]:
        """Разобрать запрос и построить цепочку рассуждений."""
        self.steps = []
        t0 = time.monotonic()

        # Шаг 1: Разбор
        intent = self._detect_intent(query)
        entities = self._extract_entities(query)
        parse_step = ThinkingStep(
            step_type=StepType.PARSE,
            description=f"Разбор запроса: intent={intent}, entities={entities}",
            result=f"intent={intent}",
            confidence=0.8,
        )
        parse_step.elapsed_ms = (time.monotonic() - t0) * 1000
        self.steps.append(parse_step)

        # Шаг 2: Поиск знаний (всегда)
        t1 = time.monotonic()
        retrieve_step = ThinkingStep(
            step_type=StepType.RETRIEVE,
            description=f"Поиск релевантных знаний по: {entities[:3]}",
            result="pending",
            confidence=0.5,
        )
        retrieve_step.elapsed_ms = (time.monotonic() - t1) * 1000
        self.steps.append(retrieve_step)

        # Шаг 3: Специализированные шаги по intent
        if intent == "calculate":
            self._add_calculate_step(query)
        elif intent == "compare":
            self._add_compare_step(entities)
        elif intent == "explain":
            self._add_explain_step(entities)
        elif intent == "create":
            self._add_create_step(query)
        else:
            self._add_reason_step(query)

        # Шаг N: Синтез
        t_syn = time.monotonic()
        synthesize_step = ThinkingStep(
            step_type=StepType.SYNTHESIZE,
            description="Синтез итогового ответа из промежуточных результатов",
            confidence=0.7,
        )
        synthesize_step.elapsed_ms = (time.monotonic() - t_syn) * 1000
        self.steps.append(synthesize_step)

        # Шаг N+1: Верификация
        t_ver = time.monotonic()
        verify_step = ThinkingStep(
            step_type=StepType.VERIFY,
            description="Проверка непротиворечивости и полноты ответа",
            confidence=0.6,
        )
        verify_step.elapsed_ms = (time.monotonic() - t_ver) * 1000
        self.steps.append(verify_step)

        return self.steps

    def update_step(self, index: int, result: str, confidence: float = 0.0) -> None:
        """Обновить результат шага (например, после retrieval)."""
        if 0 <= index < len(self.steps):
            self.steps[index].result = result
            if confidence > 0:
                self.steps[index].confidence = confidence

    def format_thinking(self, include_timing: bool = False) -> str:
        """Отформатировать цепочку для отображения пользователю."""
        lines: list[str] = []
        for i, step in enumerate(self.steps, 1):
            icon = _step_icon(step.step_type)
            line = f"{icon} Шаг {i}: {step.description}"
            if step.result and step.result != "pending":
                line += f"\n   → {step.result}"
            if include_timing and step.elapsed_ms > 0:
                line += f" [{step.elapsed_ms:.1f}ms]"
            lines.append(line)
        return "\n".join(lines)

    def overall_confidence(self) -> float:
        """Средняя уверенность всех шагов."""
        if not self.steps:
            return 0.0
        return sum(s.confidence for s in self.steps) / len(self.steps)

    def to_dict(self) -> dict:
        """Сериализация в dict."""
        return {
            "steps": [
                {
                    "type": s.step_type.name,
                    "description": s.description,
                    "result": s.result,
                    "confidence": s.confidence,
                    "elapsed_ms": s.elapsed_ms,
                }
                for s in self.steps
            ],
            "overall_confidence": self.overall_confidence(),
        }

    # ---------- Приватные ----------

    def _detect_intent(self, query: str) -> str:
        """Определить intent по ключевым словам."""
        q_lower = query.lower()
        scores: dict[str, int] = {}
        for intent, keywords in self._keywords.items():
            scores[intent] = sum(1 for kw in keywords if kw in q_lower)
        if not scores or max(scores.values()) == 0:
            return "general"
        return max(scores, key=scores.get)  # type: ignore[arg-type]

    def _extract_entities(self, query: str) -> list[str]:
        """Извлечь ключевые сущности (простая эвристика)."""
        # Кавычки
        quoted = re.findall(r'[«"\'](.*?)[»"\']', query)
        # Заглавные слова (исключая первое слово предложения)
        words = query.split()
        capitalized = [w for w in words[1:] if w and w[0].isupper() and len(w) > 2]
        # Числа
        numbers = re.findall(r"\b\d+(?:\.\d+)?\b", query)
        entities = quoted + capitalized + numbers
        return entities[:10]  # ограничение

    def _add_calculate_step(self, query: str) -> None:
        t = time.monotonic()
        step = ThinkingStep(
            step_type=StepType.CALCULATE,
            description=f"Числовой расчёт по запросу: {query[:80]}",
            confidence=0.6,
        )
        step.elapsed_ms = (time.monotonic() - t) * 1000
        self.steps.append(step)

    def _add_compare_step(self, entities: list[str]) -> None:
        t = time.monotonic()
        items = entities[:2] if len(entities) >= 2 else entities + ["?"]
        step = ThinkingStep(
            step_type=StepType.REASON,
            description=f"Сравнительный анализ: {items[0]} vs {items[1]}",
            confidence=0.65,
        )
        step.elapsed_ms = (time.monotonic() - t) * 1000
        self.steps.append(step)

    def _add_explain_step(self, entities: list[str]) -> None:
        t = time.monotonic()
        topic = entities[0] if entities else "запрос"
        step = ThinkingStep(
            step_type=StepType.REASON,
            description=f"Объяснение концепции: {topic}",
            confidence=0.7,
        )
        step.elapsed_ms = (time.monotonic() - t) * 1000
        self.steps.append(step)

    def _add_create_step(self, query: str) -> None:
        t = time.monotonic()
        step = ThinkingStep(
            step_type=StepType.REASON,
            description=f"Генерация контента: {query[:60]}",
            confidence=0.6,
        )
        step.elapsed_ms = (time.monotonic() - t) * 1000
        self.steps.append(step)

    def _add_reason_step(self, query: str) -> None:
        t = time.monotonic()
        step = ThinkingStep(
            step_type=StepType.REASON,
            description=f"Логический вывод по запросу: {query[:60]}",
            confidence=0.55,
        )
        step.elapsed_ms = (time.monotonic() - t) * 1000
        self.steps.append(step)


def _step_icon(step_type: StepType) -> str:
    icons = {
        StepType.PARSE: "🔍",
        StepType.RETRIEVE: "📚",
        StepType.REASON: "🧠",
        StepType.CALCULATE: "🔢",
        StepType.SYNTHESIZE: "✨",
        StepType.VERIFY: "✅",
    }
    return icons.get(step_type, "•")
