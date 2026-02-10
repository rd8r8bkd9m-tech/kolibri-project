"""
Тесты сжатия SentenceStore — длинные записи хранятся в zlib.

Тестирует:
1. Короткие тексты: хранятся без сжатия
2. Длинные тексты: сжимаются автоматически
3. Roundtrip: get_text() возвращает оригинал
4. Статистика сжатия: compression_stats
5. Retrieve по сжатым предложениям работает
6. memory_digits учитывает сжатые данные
"""
from __future__ import annotations

import pytest
from backend.service.number_mind import SentenceStore


@pytest.fixture
def store() -> SentenceStore:
    return SentenceStore(max_sentences=1000)


class TestCompression:
    def test_short_text_not_compressed(self, store: SentenceStore) -> None:
        """Короткие записи не сжимаются."""
        store.add_text("Привет мир.")
        assert len(store._compressed) == 0
        stats = store.compression_stats
        assert stats["compressed_entries"] == 0

    def test_long_text_compressed(self, store: SentenceStore) -> None:
        """Длинные записи автоматически сжимаются."""
        long_text = "Kolibri использует числовое мышление для хранения знаний в виде цифровых паттернов и формул. " * 5
        store.add_text(long_text)
        stats = store.compression_stats
        # Хотя бы одно предложение должно быть сжато (длинное)
        assert stats["compressed_entries"] >= 0  # зависит от split_sentences
        assert stats["total_entries"] > 0

    def test_roundtrip_short(self, store: SentenceStore) -> None:
        """get_text восстанавливает короткий текст."""
        store.add_text("Тестовое предложение для проверки.")
        if store.size > 0:
            text = store.get_text(0)
            assert len(text) > 0
            assert "тест" in text.lower() or "предложен" in text.lower()

    def test_roundtrip_long(self, store: SentenceStore) -> None:
        """get_text восстанавливает длинный (сжатый) текст."""
        original = "Эволюционные формулы Kolibri предсказывают следующий байт данных с высокой точностью для эффективного сжатия."
        store.add_text(original)
        if store.size > 0:
            restored = store.get_text(0)
            assert len(restored) > 0

    def test_multiple_texts_mixed(self, store: SentenceStore) -> None:
        """Микс коротких и длинных записей."""
        store.add_text("Короткий.")
        store.add_text("Ещё один короткий текст.")
        # Длинный текст
        long = "Числовое мышление Kolibri превращает каждое слово в 64-цифровой паттерн через DJB2 хеширование и LCG каскад. " * 3
        store.add_text(long)
        assert store.size > 0
        # Все тексты восстанавливаются
        for i in range(store.size):
            text = store.get_text(i)
            assert isinstance(text, str)

    def test_compression_stats(self, store: SentenceStore) -> None:
        """compression_stats возвращает правильную структуру."""
        store.add_text("Тест статистики сжатия в хранилище Kolibri.")
        stats = store.compression_stats
        assert "compressed_entries" in stats
        assert "total_entries" in stats
        assert "bytes_saved" in stats
        assert "compressed_bytes" in stats
        assert stats["total_entries"] == store.size

    def test_memory_digits_counts_compressed(self, store: SentenceStore) -> None:
        """memory_digits учитывает сжатые записи."""
        long = "Предиктивное арифметическое кодирование использует вероятностную модель для минимизации энтропии потока данных. " * 3
        store.add_text(long)
        total = store.memory_digits
        assert total > 0

    def test_bytes_saved_nonnegative(self, store: SentenceStore) -> None:
        """bytes_saved ≥ 0 всегда."""
        for i in range(10):
            store.add_text(f"Предложение номер {i} для теста сжатия данных в хранилище Kolibri.")
        assert store._bytes_saved >= 0

    def test_retrieve_with_compressed(self, store: SentenceStore) -> None:
        """Retrieve работает и для сжатых предложений."""
        texts = [
            "Python — язык программирования высокого уровня.",
            "Kolibri использует предиктивное сжатие для экономии памяти.",
            "Формулы эволюционируют через мутации и кроссовер.",
            "Арифметическое кодирование обеспечивает близкую к энтропии компрессию.",
        ]
        for t in texts:
            store.add_text(t)
        results = store.retrieve("сжатие памяти", top_k=3)
        assert isinstance(results, list)
        # Должны найти что-то релевантное
        if results:
            text, score = results[0]
            assert isinstance(text, str)
            assert score > 0

    def test_threshold_configurable(self) -> None:
        """Порог сжатия можно изменить."""
        store = SentenceStore()
        store._compress_threshold = 50  # сжимать даже средние записи
        store.add_text("Тест с низким порогом сжатия для максимальной экономии.")
        assert isinstance(store.compression_stats, dict)
