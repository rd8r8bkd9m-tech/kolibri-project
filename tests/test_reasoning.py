"""Тесты для Chain-of-Thought reasoning."""
from __future__ import annotations

from backend.service.reasoning import ChainOfThought, StepType


class TestChainOfThought:
    def test_analyze_returns_steps(self) -> None:
        cot = ChainOfThought()
        steps = cot.analyze_query("Объясни как работает Kolibri")
        assert len(steps) >= 4  # parse + retrieve + reason/explain + synthesize + verify
        assert steps[0].step_type == StepType.PARSE
        assert steps[1].step_type == StepType.RETRIEVE

    def test_intent_calculate(self) -> None:
        cot = ChainOfThought()
        steps = cot.analyze_query("Сколько будет 2 + 2?")
        types = [s.step_type for s in steps]
        assert StepType.CALCULATE in types

    def test_intent_compare(self) -> None:
        cot = ChainOfThought()
        steps = cot.analyze_query("Сравни Python и Java")
        # Должен быть шаг REASON (compare → reason)
        types = [s.step_type for s in steps]
        assert StepType.REASON in types

    def test_intent_explain(self) -> None:
        cot = ChainOfThought()
        steps = cot.analyze_query("Почему небо голубое?")
        types = [s.step_type for s in steps]
        assert StepType.REASON in types

    def test_format_thinking(self) -> None:
        cot = ChainOfThought()
        cot.analyze_query("Напиши стихотворение")
        text = cot.format_thinking()
        assert "Шаг 1" in text
        assert "Шаг 2" in text
        assert len(text) > 0

    def test_format_thinking_with_timing(self) -> None:
        cot = ChainOfThought()
        cot.analyze_query("test query")
        text = cot.format_thinking(include_timing=True)
        assert "ms" in text

    def test_overall_confidence(self) -> None:
        cot = ChainOfThought()
        cot.analyze_query("Какие есть языки программирования?")
        conf = cot.overall_confidence()
        assert 0.0 < conf <= 1.0

    def test_update_step(self) -> None:
        cot = ChainOfThought()
        cot.analyze_query("test query")
        cot.update_step(1, "Найдено 5 документов", confidence=0.9)
        assert cot.steps[1].result == "Найдено 5 документов"
        assert cot.steps[1].confidence == 0.9

    def test_to_dict(self) -> None:
        cot = ChainOfThought()
        cot.analyze_query("hello world")
        d = cot.to_dict()
        assert "steps" in d
        assert "overall_confidence" in d
        assert len(d["steps"]) == len(cot.steps)
        assert d["steps"][0]["type"] == "PARSE"

    def test_extract_entities_quoted(self) -> None:
        cot = ChainOfThought()
        cot.analyze_query('Что такое "Kolibri OS" и как работает «архиватор»?')
        # Проверяем через разбор шагов — entities попадают в описание
        parse_desc = cot.steps[0].description
        assert "Kolibri OS" in parse_desc or "архиватор" in parse_desc

    def test_empty_query(self) -> None:
        cot = ChainOfThought()
        steps = cot.analyze_query("")
        assert len(steps) >= 3  # Даже пустой запрос получает PARSE+RETRIEVE+SYNTHESIZE+VERIFY

    def test_max_steps_not_exceeded(self) -> None:
        cot = ChainOfThought(max_steps=8)
        steps = cot.analyze_query("Вычисли сколько будет 100 процентов от 50 плюс разница")
        assert len(steps) <= cot.max_steps + 2  # +2 для synthesize+verify
