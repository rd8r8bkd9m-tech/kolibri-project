"""Тесты для контекстного окна (Context Window)."""
from __future__ import annotations

from backend.service.context_window import ContextWindow


class TestContextWindow:
    def test_add_message(self) -> None:
        cw = ContextWindow()
        cw.add_message("user", "Привет")
        cw.add_message("assistant", "Здравствуйте")
        cw.add_message("user", "Как дела?")
        cw.add_message("assistant", "Отлично, спасибо")
        cw.add_message("user", "Расскажи про AI")
        assert len(cw.working_memory) == 5

    def test_auto_compression(self) -> None:
        cw = ContextWindow(max_tokens=50)
        for i in range(100):
            cw.add_message("user", f"Сообщение номер {i} с достаточным количеством слов для заполнения")
        assert len(cw.working_memory) <= 10
        assert len(cw.compressed_memory) > 0

    def test_get_context_format(self) -> None:
        cw = ContextWindow()
        cw.add_message("user", "Что такое Kolibri?")
        cw.add_message("assistant", "AI-платформа на числовом мышлении")
        text = cw.get_context()
        assert "=== Текущий диалог ===" in text
        assert "Пользователь:" in text
        assert "Kolibri:" in text

    def test_get_context_with_compressed(self) -> None:
        cw = ContextWindow(max_tokens=20)
        for i in range(20):
            cw.add_message("user", f"Длинное сообщение номер {i} про тему")
        text = cw.get_context()
        assert "=== Предыдущий контекст ===" in text
        assert "=== Текущий диалог ===" in text

    def test_query_enrichment(self) -> None:
        cw = ContextWindow()
        cw.add_message("user", "Расскажи про нейронные сети и машинное обучение")
        cw.add_message("assistant", "Нейронные сети — математические модели обработки информации")
        enriched = cw.get_query_with_context("Какие ещё есть подходы?")
        assert "(контекст:" in enriched

    def test_query_enrichment_empty(self) -> None:
        cw = ContextWindow()
        enriched = cw.get_query_with_context("Привет")
        assert enriched == "Привет"  # Нет контекста

    def test_clear(self) -> None:
        cw = ContextWindow()
        cw.add_message("user", "Тест")
        cw.add_message("assistant", "Ответ")
        cw.clear()
        assert len(cw.working_memory) == 0
        assert len(cw.compressed_memory) == 0
        assert cw._total_tokens == 0

    def test_get_stats(self) -> None:
        cw = ContextWindow(max_tokens=1000)
        cw.add_message("user", "Первый вопрос")
        cw.add_message("assistant", "Первый ответ")
        stats = cw.get_stats()
        assert stats["working_count"] == 2
        assert stats["compressed_count"] == 0
        assert stats["total_tokens"] > 0
        assert stats["max_tokens"] == 1000
        assert 0.0 <= stats["usage_pct"] <= 100.0

    def test_min_working_preserved(self) -> None:
        """При сжатии должны оставаться минимум _min_working сообщений."""
        cw = ContextWindow(max_tokens=10)
        for i in range(50):
            cw.add_message("user", f"Msg {i} words words words")
        assert len(cw.working_memory) >= cw._min_working

    def test_compress_message_format(self) -> None:
        from backend.service.context_window import Message

        cw = ContextWindow()
        msg = Message(role="user", content="Расскажи про искусственный интеллект и нейронные сети")
        summary = cw._compress_message(msg)
        assert summary.startswith("Q:")
        assert len(summary) > 10

    def test_compress_message_assistant(self) -> None:
        from backend.service.context_window import Message

        cw = ContextWindow()
        msg = Message(role="assistant", content="Искусственный интеллект обрабатывает данные автономно")
        summary = cw._compress_message(msg)
        assert summary.startswith("A:")
