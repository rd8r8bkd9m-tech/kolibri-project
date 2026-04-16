"""
Тесты ArchiverService — Python-обёртка РОДНОГО архиватора Kolibri.

Тестирует:
1. Создание сервиса (с/без нативной библиотеки)
2. Kolibri compress roundtrip (9 методов LZ77+RLE+Huffman+...)
3. compress_text / decompress_text
4. Статистику
5. Пустые / граничные данные
"""
from __future__ import annotations

import pytest
from backend.service.archiver_service import (
    ArchiverService,
    CompressResult,
    KOLIBRI_COMPRESS_ALL,
    KOLIBRI_COMPRESS_LZ77,
)


# ── Fixtures ──────────────────────────────────────────────────────────

@pytest.fixture
def service() -> ArchiverService:
    """Создаём сервис с полным набором методов."""
    return ArchiverService()


# ── Базовые тесты ────────────────────────────────────────────────────

class TestArchiverCreation:
    def test_create_service(self, service: ArchiverService) -> None:
        assert service is not None
        stats = service.get_stats()
        assert "native_available" in stats
        assert "engine" in stats

    def test_stats_structure(self, service: ArchiverService) -> None:
        stats = service.get_stats()
        assert isinstance(stats["native_available"], bool)
        assert stats["engine"] == "kolibri"
        assert stats["version"] == "v50.0"
        assert isinstance(stats["methods_description"], list)

    def test_all_methods_listed(self, service: ArchiverService) -> None:
        stats = service.get_stats()
        desc = stats["methods_description"]
        assert "LZ77" in desc
        assert "Huffman" in desc
        assert "RLE" in desc


# ── Нативный Kolibri (пропуск если .so нет) ──────────────────────────

_native = ArchiverService().native_available
_skip_no_native = pytest.mark.skipif(not _native, reason="libkolibri_compress.so not available")


class TestNativeKolibri:
    @_skip_no_native
    def test_native_detection(self, service: ArchiverService) -> None:
        assert service.native_available

    @_skip_no_native
    def test_compress_basic(self, service: ArchiverService) -> None:
        data = b"Hello Kolibri! " * 100
        result = service.compress(data)
        assert result.success
        assert result.method == "kolibri"
        assert result.compressed_size > 0
        assert result.original_size == len(data)

    @_skip_no_native
    def test_roundtrip(self, service: ArchiverService) -> None:
        original = b"Kolibri native compression test data. " * 50
        compressed = service.compress(original)
        assert compressed.success

        decompressed = service.decompress(compressed.data)
        assert decompressed.success
        assert decompressed.data == original

    @_skip_no_native
    def test_roundtrip_large(self, service: ArchiverService) -> None:
        """На больших данных — реальное сжатие."""
        original = b"ABCDEFGHIJ" * 5000  # 50 KB
        compressed = service.compress(original)
        assert compressed.success
        assert compressed.method == "kolibri"

        decompressed = service.decompress(compressed.data)
        assert decompressed.success
        assert decompressed.data == original

    @_skip_no_native
    def test_compress_text_roundtrip(self, service: ArchiverService) -> None:
        text = "Привет, мир! Это тест сжатия текста Kolibri." * 30
        compressed = service.compress_text(text)
        assert compressed.success
        assert compressed.method == "kolibri"

        restored = service.decompress_text(compressed.data)
        assert restored == text

    @_skip_no_native
    def test_train_is_noop(self, service: ArchiverService) -> None:
        """train() не должен ломать сервис."""
        service.train(b"test data", rounds=3)
        service.train_on_texts(["text1", "text2"])
        # Должен по-прежнему работать
        result = service.compress(b"After training")
        assert result.success

    @_skip_no_native
    def test_methods_with_lz77_only(self) -> None:
        """Тест с одним конкретным методом."""
        svc = ArchiverService(methods=KOLIBRI_COMPRESS_LZ77)
        assert svc.native_available
        data = b"AAABBBCCCAAABBBCCC" * 100
        result = svc.compress(data)
        assert result.success


# ── CompressResult ────────────────────────────────────────────────────

class TestCompressResult:
    def test_default_result(self) -> None:
        r = CompressResult()
        assert r.original_size == 0
        assert r.compressed_size == 0
        assert r.data == b""
        assert not r.success

    def test_error_result(self) -> None:
        r = CompressResult(error="test error")
        assert r.error == "test error"
        assert not r.success


# ── Граничные случаи ──────────────────────────────────────────────────

class TestEdgeCases:
    def test_compress_empty(self, service: ArchiverService) -> None:
        result = service.compress(b"")
        assert not result.success
        assert result.error == "empty input"

    def test_decompress_empty(self, service: ArchiverService) -> None:
        result = service.decompress(b"")
        assert not result.success

    @_skip_no_native
    def test_decompress_garbage(self, service: ArchiverService) -> None:
        result = service.decompress(b"\xff\xff\xff\xff" * 10)
        # Мусор — ожидаем ошибку
        assert not result.success

    def test_decompress_text_empty(self, service: ArchiverService) -> None:
        result = service.decompress_text(b"")
        assert result == ""
