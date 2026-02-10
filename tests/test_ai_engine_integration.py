"""Интеграционные тесты: AIEngine + CoT + ContextWindow + FormulaLM."""

from __future__ import annotations

import pytest


@pytest.fixture(scope="module")
def engine():
    """Создаём KolibriAIEngine один раз для модуля."""
    from backend.service.ai_engine import KolibriAIEngine

    eng = KolibriAIEngine()
    return eng


@pytest.fixture(scope="module")
def greeting_result(engine):
    """Один вызов chat() для приветствия — переиспользуем."""
    return engine.chat("Привет, как дела?")


@pytest.fixture(scope="module")
def knowledge_result(engine):
    """Один вызов chat() для обычного запроса."""
    return engine.chat("Что такое нейронная сеть")


# ---- Chat: базовый вызов возвращает новые поля ----

def test_chat_returns_thinking(greeting_result):
    """chat() должен вернуть поле thinking (строка)."""
    assert "thinking" in greeting_result
    assert isinstance(greeting_result["thinking"], str)


def test_chat_returns_thinking_steps(greeting_result):
    """chat() должен вернуть список thinking_steps."""
    assert "thinking_steps" in greeting_result
    assert isinstance(greeting_result["thinking_steps"], list)


def test_chat_returns_generation_used(greeting_result):
    """chat() должен вернуть generation_used (bool)."""
    assert "generation_used" in greeting_result
    assert isinstance(greeting_result["generation_used"], bool)


def test_chat_returns_context_stats(greeting_result):
    """chat() должен вернуть context_stats (dict)."""
    assert "context_stats" in greeting_result
    stats = greeting_result["context_stats"]
    assert "working_count" in stats
    assert stats["working_count"] >= 1


# ---- Обычный запрос возвращает thinking_steps ----

def test_knowledge_returns_thinking_steps(knowledge_result):
    """Обычный запрос → thinking_steps с type и content."""
    assert "thinking_steps" in knowledge_result
    steps = knowledge_result["thinking_steps"]
    assert isinstance(steps, list)
    if steps:
        step = steps[0]
        assert "type" in step
        assert "content" in step


# ---- Основная структура ответа сохранена ----

def test_chat_preserves_existing_fields(knowledge_result):
    """Новые поля не ломают старые."""
    for key in ("response", "confidence", "sources", "method",
                "duration_ms", "formula_data", "graph_stats"):
        assert key in knowledge_result, f"Отсутствует ключ: {key}"


def test_chat_confidence_is_float(knowledge_result):
    """confidence — число от 0 до 1."""
    assert 0.0 <= knowledge_result["confidence"] <= 1.0


def test_chat_duration_positive(knowledge_result):
    """duration_ms > 0."""
    assert knowledge_result["duration_ms"] > 0


# ---- FormulaLM: атрибуты на месте ----

def test_lm_attributes_exist(engine):
    """LM-атрибуты должны быть инициализированы."""
    assert hasattr(engine, '_bpe_tokenizer')
    assert hasattr(engine, '_formula_lm')
    assert hasattr(engine, '_lm_trained')
    assert hasattr(engine, '_lm_generation')
    assert hasattr(engine, '_chain_of_thought')
    assert hasattr(engine, '_context_window')


def test_generate_text_returns_string(engine):
    """_generate_text() возвращает строку (пусть пустую)."""
    result = engine._generate_text("тест")
    assert isinstance(result, str)
