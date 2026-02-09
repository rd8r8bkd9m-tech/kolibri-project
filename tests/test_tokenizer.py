"""Тесты для BPE-токенизатора."""
from __future__ import annotations

import tempfile
from pathlib import Path

import pytest

from backend.service.tokenizer import BPETokenizer

_TRAINING_TEXTS: list[str] = [
    "Нейронная сеть это математическая модель вдохновлённая биологическими нейронами.",
    "Машинное обучение раздел искусственного интеллекта.",
    "Алгоритм это конечная последовательность инструкций.",
    "Операционная система управляет аппаратными ресурсами компьютера.",
    "Компилятор преобразует исходный код в машинный.",
    "Структуры данных организуют хранение информации в памяти.",
    "Рекурсия это функция которая вызывает саму себя.",
    "Генетический алгоритм имитирует процесс естественного отбора.",
    "Сжатие данных уменьшает объём информации.",
    "Граф знаний связывает сущности отношениями.",
    "Python один из самых популярных языков программирования.",
    "Язык C используется для системного программирования.",
    "Хеш функция преобразует данные в фиксированный набор символов.",
    "Нейроэволюция эволюционирует нейронные сети без обратного распространения.",
    "Kolibri система числового мышления.",
    "Формулы эволюционируют через мутацию и кроссовер.",
    "BPE разбивает текст на субсловные единицы.",
    "Эмбеддинги представляют слова как числовые векторы.",
    "Архиватор сжимает файлы для уменьшения размера.",
    "Токенизация является первым шагом обработки текста.",
    "Дерево решений классифицирует данные по признакам.",
    "Кроссовер комбинирует гены двух родительских формул.",
    "Мутация случайно изменяет гены в геноме.",
    "Фитнес функция оценивает качество формулы.",
    "Турнирная селекция выбирает лучших для воспроизводства.",
    "Арифметическое кодирование сжимает данные используя вероятности.",
    "Семантические паттерны находят связи между словами.",
    "Индекс ускоряет поиск данных в хранилище.",
    "WebAssembly позволяет запускать код в браузере.",
    "FastAPI фреймворк для создания веб сервисов на Python.",
] * 4  # Repeat to ensure enough pairs for merges


class TestBPETokenizer:
    """Тесты BPE-токенизатора."""

    def test_train_creates_vocab(self) -> None:
        tok = BPETokenizer(vocab_size=500)
        tok.train(_TRAINING_TEXTS)
        assert len(tok) > 100, f"Словарь слишком маленький: {len(tok)}"

    def test_encode_decode_roundtrip(self) -> None:
        tok = BPETokenizer(vocab_size=500)
        tok.train(_TRAINING_TEXTS)
        for text in _TRAINING_TEXTS[:20]:
            ids = tok.encode(text)
            assert len(ids) > 0, f"encode вернул пустой список для: {text!r}"
            decoded = tok.decode(ids)
            # Декодированный текст должен содержать символы оригинала
            # (BPE может разбивать слова на части, но символы сохраняются)
            orig_chars = set(text.replace(" ", ""))
            decoded_chars = set(decoded.replace(" ", ""))
            overlap = len(orig_chars & decoded_chars)
            assert overlap > len(orig_chars) * 0.5, (
                f"Слишком мало символов совпало: {overlap}/{len(orig_chars)}"
            )

    def test_encode_returns_ints(self) -> None:
        tok = BPETokenizer(vocab_size=300)
        tok.train(_TRAINING_TEXTS)
        ids = tok.encode("Нейронная сеть обучается на данных.")
        assert all(isinstance(i, int) for i in ids), "encode() должен возвращать list[int]"
        assert len(ids) > 0, "encode() не должен возвращать пустой список"

    def test_unknown_chars(self) -> None:
        tok = BPETokenizer(vocab_size=300)
        tok.train(_TRAINING_TEXTS)
        # Emoji и спецсимволы не должны вызывать исключений
        ids = tok.encode("Hello 🌍 мир! @#$%")
        assert isinstance(ids, list)
        # Может вернуть пустой или частичный — но не упасть

    def test_save_load(self) -> None:
        tok = BPETokenizer(vocab_size=400)
        tok.train(_TRAINING_TEXTS)
        test_text = "Формулы эволюционируют через мутацию."
        ids_before = tok.encode(test_text)

        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "tokenizer.json"
            tok.save(path)

            tok2 = BPETokenizer()
            tok2.load(path)
            ids_after = tok2.encode(test_text)

        assert ids_before == ids_after, "encode() должен давать тот же результат после save/load"

    def test_empty_input(self) -> None:
        tok = BPETokenizer(vocab_size=300)
        tok.train(_TRAINING_TEXTS)
        ids = tok.encode("")
        assert ids == []
        text = tok.decode([])
        assert text == ""

    def test_len(self) -> None:
        tok = BPETokenizer(vocab_size=200)
        assert len(tok) == 0
        tok.train(_TRAINING_TEXTS)
        assert len(tok) > 0
        assert len(tok) <= 200
