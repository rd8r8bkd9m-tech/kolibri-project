"""
Тесты ArchiverService — Python-обёртка предиктивного компрессора.

Тестирует:
1. Создание сервиса (с/без нативной библиотеки)
2. Zlib fallback (всегда работает)
3. Нативный KPC roundtrip (если .so доступна)
4. compress_text / decompress_text
5. Обучение на текстовом корпусе
6. Статистика
7. Пустые / граничные данные
"""
from __future__ import annotations

import pytest
from backend.service.archiver_service import ArchiverService, CompressResult


# ── Fixtures ──────────────────────────────────────────────────────────

@pytest.fixture
def service() -> ArchiverService:
    """Создаём сервис (может быть native или zlib fallback)."""
    return ArchiverService(evolve_rounds=3)


@pytest.fixture
def zlib_service(monkeypatch: pytest.MonkeyPatch) -> ArchiverService:
    """Сервис с принудительным zlib fallback."""
    import backend.service.archiver_service as mod
    monkeypatch.setattr(mod, "_lib", None)
    svc = ArchiverService.__new__(ArchiverService)
    svc._lib = None
    svc._ctx = None
    svc._evolve_rounds = 3
    svc._trained = False
    svc._use_native = False
    return svc


# ── Базовые тесты ────────────────────────────────────────────────────

class TestArchiverCreation:
    def test_create_service(self, service: ArchiverService) -> None:
        assert service is not None
        stats = service.get_stats()
        assert "native_available" in stats
        assert "method" in stats

    def test_stats_structure(self, service: ArchiverService) -> None:
        stats = service.get_stats()
        assert isinstance(stats["native_available"], bool)
        assert isinstance(stats["trained"], bool)
        assert stats["method"] in ("kpc", "zlib")


# ── Zlib fallback ─────────────────────────────────────────────────────

class TestZlibFallback:
    def test_compress_zlib(self, zlib_service: ArchiverService) -> None:
        result = zlib_service.compress(b"Hello Kolibri! " * 100)
        assert result.success
        assert result.method == "zlib"
        assert result.compressed_size < result.original_size

    def test_roundtrip_zlib(self, zlib_service: ArchiverService) -> None:
        original = b"Kolibri predictive compression test data " * 50
        compressed = zlib_service.compress(original)
        assert compressed.success

        decompressed = zlib_service.decompress(compressed.data)
        assert decompressed.success
        assert decompressed.data == original

    def test_compress_text_zlib(self, zlib_service: ArchiverService) -> None:
        text = "Привет, мир! Это тест сжатия текста." * 20
        compressed = zlib_service.compress_text(text)
        assert compressed.success
        assert compressed.method == "zlib"

        restored = zlib_service.decompress_text(compressed.data)
        assert restored == text

    def test_compress_empty_zlib(self, zlib_service: ArchiverService) -> None:
        result = zlib_service.compress(b"")
        assert not result.success
        assert result.error == "empty input"


# ── Нативный KPC (пропуск если .so нет) ──────────────────────────────

class TestNativeKPC:
    def test_native_detection(self, service: ArchiverService) -> None:
        """Проверяем что метод определён корректно."""
        stats = service.get_stats()
        assert stats["method"] in ("kpc", "zlib")

    @pytest.mark.skipif(
        not ArchiverService().native_available,
        reason="Native KPC library not available",
    )
    def test_train_native(self) -> None:
        svc = ArchiverService(evolve_rounds=3)
        data = b"ABCABCABCABC" * 50
        svc.train(data, rounds=2)
        assert svc._trained

    @pytest.mark.skipif(
        not ArchiverService().native_available,
        reason="Native KPC library not available",
    )
    def test_roundtrip_native(self) -> None:
        svc = ArchiverService(evolve_rounds=3)
        data = b"Kolibri predictive " * 30
        svc.train(data, rounds=3)

        compressed = svc.compress(data)
        assert compressed.success
        # Метод может быть kpc или zlib (fallback если kpc расширяет данные)
        assert compressed.method in ("kpc", "zlib")

        decompressed = svc.decompress(compressed.data)
        assert decompressed.success
        assert decompressed.data == data

    @pytest.mark.skipif(
        not ArchiverService().native_available,
        reason="Native KPC library not available",
    )
    def test_train_on_texts(self) -> None:
        svc = ArchiverService(evolve_rounds=2)
        texts = [
            "Формулы эволюционируют",
            "Предсказание следующего байта",
            "Арифметическое кодирование",
        ]
        svc.train_on_texts(texts, rounds=2)
        assert svc._trained


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
    def test_decompress_empty(self, service: ArchiverService) -> None:
        result = service.decompress(b"")
        assert not result.success

    def test_decompress_garbage(self, service: ArchiverService) -> None:
        result = service.decompress(b"\xff\xff\xff\xff" * 10)
        assert not result.success or result.method == "zlib"

    def test_decompress_text_empty(self, service: ArchiverService) -> None:
        result = service.decompress_text(b"")
        assert result == ""
