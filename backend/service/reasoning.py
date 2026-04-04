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
        elif intent == "list":
            self._add_list_step(entities)
        else:
            # Для general — проверяем multi-hop (вопросы с несколькими связями)
            if self._is_multi_hop(query):
                self._add_multi_hop_step(query, entities)
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

    def get_search_strategy(self) -> dict:
        """Вернуть стратегию поиска на основе проанализированного intent.
        
        CoT направляет pipeline:
        - calculate → приоритет формул и числовых операций
        - compare → ищем оба объекта, строим сравнительный ответ
        - explain → глубокий поиск, 2-хоповая навигация, каузальные цепочки
        - list → максимум кандидатов, перечисление
        - create → генеративный режим, FormulaLM
        - general → стандартный pipeline
        """
        if not self.steps:
            return {"intent": "general", "max_words": 10, "depth": 1,
                    "use_causal": False, "use_abstract": False, "prefer_generation": False}
        
        # Первый шаг всегда PARSE с intent
        intent = "general"
        entities: list[str] = []
        for step in self.steps:
            if step.step_type == StepType.PARSE and "intent=" in step.result:
                intent = step.result.split("intent=")[1].split(",")[0]
            if step.step_type == StepType.PARSE and "entities=" in step.description:
                # Извлекаем entities из описания
                ent_part = step.description.split("entities=")[1] if "entities=" in step.description else "[]"
                if ent_part.startswith("["):
                    import ast
                    try:
                        entities = ast.literal_eval(ent_part)
                    except Exception:
                        pass
        
        strategy = {
            "intent": intent,
            "entities": entities,
            "max_words": 10,
            "depth": 1,
            "use_causal": False,
            "use_abstract": False,
            "prefer_generation": False,
            "retrieval_top_k": 5,
        }
        
        if intent == "explain":
            strategy["max_words"] = 20
            strategy["depth"] = 2
            strategy["use_causal"] = True
            strategy["use_abstract"] = True
            strategy["retrieval_top_k"] = 8
        elif intent == "compare":
            strategy["max_words"] = 15
            strategy["retrieval_top_k"] = 8
        elif intent == "list":
            strategy["max_words"] = 25
            strategy["retrieval_top_k"] = 10
        elif intent == "create":
            strategy["prefer_generation"] = True
            strategy["max_words"] = 15
        elif intent == "calculate":
            strategy["max_words"] = 5
        
        return strategy

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
        items = entities[:2] if len(entities) >= 2 else entities + ["?", "?"]
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

    def _add_list_step(self, entities: list[str]) -> None:
        """Шаг для перечислений — ищем максимум кандидатов."""
        t = time.monotonic()
        topic = entities[0] if entities else "запрос"
        step = ThinkingStep(
            step_type=StepType.RETRIEVE,
            description=f"Поиск полного списка по теме: {topic}",
            result="pending — расширенный поиск",
            confidence=0.6,
        )
        step.elapsed_ms = (time.monotonic() - t) * 1000
        self.steps.append(step)

    def _is_multi_hop(self, query: str) -> bool:
        """Проверяет, требует ли запрос multi-hop reasoning.

        Multi-hop = вопрос, где нужно связать несколько фактов.
        Примеры:
        - "Какая столица страны, где находится Эйфелева башня?"
        - "Кто написал книгу, по которой сняли фильм 'Матрица'?"
        """
        q_lower = query.lower()

        # Паттерны multi-hop вопросов
        multi_hop_patterns = [
            r"стран[аыуе].*где\b",           # "страна, где..."
            r"город.*где\b",                  # "город, где..."
            r"человек.*котор\b",              # "человек, который..."
            r"книг[ауе].*котор\b",            # "книга, которая..."
            r"фильм.*котор\b",                # "фильм, который..."
            r"кто.*создал.*что\b",            # "кто создал что..."
            r"почему.*потому что\b",          # "почему... потому что..."
            r"если.*то\b",                    # "если... то..."
            r"какой.*из\b",                   # "какой из..."
            r"связ[ья].*между\b",             # "связь между..."
            r"влияет.*на\b",                  # "влияет на..."
            r"следстви[ея].*причин\b",        # "следствие причины..."
        ]

        import re
        for pattern in multi_hop_patterns:
            if re.search(pattern, q_lower):
                return True

        # Вопросы с несколькими сущностями (2+ заглавных слова)
        words = query.split()
        capitalized = [w for w in words if len(w) > 2 and w[0].isupper()]
        if len(capitalized) >= 2:
            return True

        return False

    def _add_multi_hop_step(self, query: str, entities: list[str]) -> None:
        """Multi-hop reasoning — разбиваем на подзадачи."""
        t = time.monotonic()

        # Определяем подзадачи
        sub_tasks = []
        if entities:
            sub_tasks.append(f"1. Найти информацию о: {entities[0]}")
        if len(entities) >= 2:
            sub_tasks.append(f"2. Связать с: {entities[1]}")
        sub_tasks.append(f"3. Сформировать связный ответ")

        step = ThinkingStep(
            step_type=StepType.REASON,
            description=f"Multi-hop рассуждение: {' → '.join(sub_tasks[:2])}",
            result="; ".join(sub_tasks),
            confidence=0.5,
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
