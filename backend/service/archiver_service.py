"""
archiver_service.py — Python-обёртка предиктивного компрессора Kolibri (KPC)

Обеспечивает:
1. ctypes-биндинг к libkolibri_kpc.so (MLP-предсказатель + арифм. кодирование)
2. Fallback на чистый Python (zlib) если .so недоступна
3. FastAPI-роутер /api/archiver/* для REST-доступа
4. Высокоуровневый API: compress_text(), decompress_text(), train_on_corpus()
"""
from __future__ import annotations

import ctypes
import os
import struct
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# ──────────────────────────────────────────────────────────────────────
#  ctypes-биндинг к libkolibri_kpc.so
# ──────────────────────────────────────────────────────────────────────

_lib: Optional[ctypes.CDLL] = None
_LIB_NAMES = [
    "build/libkolibri_kpc.so",
    "libkolibri_kpc.so",
    "../build/libkolibri_kpc.so",
]


def _find_lib() -> Optional[ctypes.CDLL]:
    """Ищем shared library в стандартных путях."""
    root = Path(__file__).resolve().parent.parent.parent  # project root
    for name in _LIB_NAMES:
        path = root / name
        if path.exists():
            try:
                lib = ctypes.CDLL(str(path))
                return lib
            except OSError:
                continue
    return None


def _init_lib() -> Optional[ctypes.CDLL]:
    """Инициализация ctypes сигнатур."""
    global _lib
    if _lib is not None:
        return _lib

    lib = _find_lib()
    if lib is None:
        return None

    # kpc_create() -> KPCContext*
    lib.kpc_create.restype = ctypes.c_void_p
    lib.kpc_create.argtypes = []

    # kpc_destroy(ctx)
    lib.kpc_destroy.restype = None
    lib.kpc_destroy.argtypes = [ctypes.c_void_p]

    # kpc_train(ctx, data, size, rounds)
    lib.kpc_train.restype = None
    lib.kpc_train.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_uint8),
        ctypes.c_size_t,
        ctypes.c_int,
    ]

    # kpc_compress(ctx, input, input_size, &output, &output_size) -> int
    lib.kpc_compress.restype = ctypes.c_int
    lib.kpc_compress.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_uint8),
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
        ctypes.POINTER(ctypes.c_size_t),
    ]

    # kpc_decompress(input, input_size, &output, &output_size) -> int
    lib.kpc_decompress.restype = ctypes.c_int
    lib.kpc_decompress.argtypes = [
        ctypes.POINTER(ctypes.c_uint8),
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
        ctypes.POINTER(ctypes.c_size_t),
    ]

    _lib = lib
    return lib


# ──────────────────────────────────────────────────────────────────────
#  Результат сжатия
# ──────────────────────────────────────────────────────────────────────

@dataclass
class CompressResult:
    """Результат операции сжатия / распаковки."""
    original_size: int = 0
    compressed_size: int = 0
    ratio: float = 0.0
    data: bytes = b""
    method: str = "none"  # "kpc" | "zlib" | "none"
    success: bool = False
    error: str = ""


# ──────────────────────────────────────────────────────────────────────
#  Основной класс ArchiverService
# ──────────────────────────────────────────────────────────────────────

class ArchiverService:
    """Высокоуровневый сервис сжатия Kolibri.

    Использует нативный KPC (ctypes) если доступен, иначе fallback на zlib.
    """

    def __init__(self, evolve_rounds: int = 5) -> None:
        self._lib = _init_lib()
        self._ctx: Optional[int] = None  # KPCContext pointer (as int)
        self._evolve_rounds = evolve_rounds
        self._trained = False
        self._use_native = self._lib is not None

        if self._use_native:
            ptr = self._lib.kpc_create()  # type: ignore[union-attr]
            if ptr:
                self._ctx = ptr
            else:
                self._use_native = False

    def __del__(self) -> None:
        if self._ctx and self._lib:
            self._lib.kpc_destroy(ctypes.c_void_p(self._ctx))
            self._ctx = None

    @property
    def native_available(self) -> bool:
        """True если нативная KPC библиотека загружена."""
        return self._use_native

    # ── Обучение ──────────────────────────────────────────────────────

    def train(self, data: bytes, rounds: int = 0) -> None:
        """Адаптировать модель к данным."""
        if rounds <= 0:
            rounds = self._evolve_rounds

        if self._use_native and self._ctx:
            buf = (ctypes.c_uint8 * len(data))(*data)
            self._lib.kpc_train(  # type: ignore[union-attr]
                ctypes.c_void_p(self._ctx),
                buf,
                ctypes.c_size_t(len(data)),
                ctypes.c_int(rounds),
            )
            self._trained = True
        # Для zlib fallback обучение не нужно

    def train_on_texts(self, texts: list[str], rounds: int = 0) -> None:
        """Обучить на массиве текстов (конкатенация в bytes)."""
        combined = "\n".join(texts).encode("utf-8")
        self.train(combined, rounds)

    # ── Сжатие ────────────────────────────────────────────────────────

    def compress(self, data: bytes) -> CompressResult:
        """Сжать произвольные байты."""
        if not data:
            return CompressResult(error="empty input")

        if self._use_native and self._ctx:
            return self._compress_native(data)
        return self._compress_zlib(data)

    def compress_text(self, text: str) -> CompressResult:
        """Сжать текст (UTF-8 → bytes → compress)."""
        return self.compress(text.encode("utf-8"))

    def _compress_native(self, data: bytes) -> CompressResult:
        """Сжатие через нативную KPC библиотеку."""
        inp = (ctypes.c_uint8 * len(data))(*data)
        out_ptr = ctypes.POINTER(ctypes.c_uint8)()
        out_size = ctypes.c_size_t(0)

        rc = self._lib.kpc_compress(  # type: ignore[union-attr]
            ctypes.c_void_p(self._ctx),
            inp,
            ctypes.c_size_t(len(data)),
            ctypes.byref(out_ptr),
            ctypes.byref(out_size),
        )

        if rc != 0:
            return CompressResult(
                original_size=len(data),
                error=f"kpc_compress returned {rc}",
            )

        sz = out_size.value
        result_bytes = bytes(out_ptr[:sz])

        # Освобождаем C-память
        libc = ctypes.CDLL("libc.so.6")
        libc.free(out_ptr)

        return CompressResult(
            original_size=len(data),
            compressed_size=sz,
            ratio=sz / len(data) if len(data) > 0 else 0.0,
            data=result_bytes,
            method="kpc",
            success=True,
        )

    def _compress_zlib(self, data: bytes) -> CompressResult:
        """Fallback: сжатие через zlib."""
        compressed = zlib.compress(data, level=9)
        # Добавляем 4-байт заголовок с оригинальным размером
        header = struct.pack("<I", len(data))
        result = header + compressed

        return CompressResult(
            original_size=len(data),
            compressed_size=len(result),
            ratio=len(result) / len(data) if len(data) > 0 else 0.0,
            data=result,
            method="zlib",
            success=True,
        )

    # ── Распаковка ────────────────────────────────────────────────────

    def decompress(self, data: bytes) -> CompressResult:
        """Распаковать данные (авто-определение формата)."""
        if not data:
            return CompressResult(error="empty input")

        # Проверяем KPC magic (0x4B504300 = "KPC\0")
        if len(data) >= 4:
            magic = struct.unpack("<I", data[:4])[0]
            if magic == 0x4B504300:
                return self._decompress_native(data)

        # Иначе — zlib формат
        return self._decompress_zlib(data)

    def decompress_text(self, data: bytes) -> str:
        """Распаковать и вернуть текст."""
        result = self.decompress(data)
        if result.success:
            return result.data.decode("utf-8", errors="replace")
        return ""

    def _decompress_native(self, data: bytes) -> CompressResult:
        """Распаковка через нативную KPC библиотеку."""
        if not self._lib:
            return CompressResult(error="native library not available")

        inp = (ctypes.c_uint8 * len(data))(*data)
        out_ptr = ctypes.POINTER(ctypes.c_uint8)()
        out_size = ctypes.c_size_t(0)

        rc = self._lib.kpc_decompress(
            inp,
            ctypes.c_size_t(len(data)),
            ctypes.byref(out_ptr),
            ctypes.byref(out_size),
        )

        if rc != 0:
            return CompressResult(
                compressed_size=len(data),
                error=f"kpc_decompress returned {rc}",
            )

        sz = out_size.value
        result_bytes = bytes(out_ptr[:sz])

        libc = ctypes.CDLL("libc.so.6")
        libc.free(out_ptr)

        return CompressResult(
            original_size=sz,
            compressed_size=len(data),
            ratio=len(data) / sz if sz > 0 else 0.0,
            data=result_bytes,
            method="kpc",
            success=True,
        )

    def _decompress_zlib(self, data: bytes) -> CompressResult:
        """Fallback: распаковка через zlib."""
        if len(data) < 4:
            return CompressResult(error="data too short for zlib format")

        original_size = struct.unpack("<I", data[:4])[0]
        try:
            decompressed = zlib.decompress(data[4:])
        except zlib.error as e:
            return CompressResult(error=f"zlib error: {e}")

        return CompressResult(
            original_size=len(decompressed),
            compressed_size=len(data),
            ratio=len(data) / len(decompressed) if len(decompressed) > 0 else 0.0,
            data=decompressed,
            method="zlib",
            success=True,
        )

    # ── Статистика ────────────────────────────────────────────────────

    def get_stats(self) -> dict[str, object]:
        """Информация о состоянии сервиса."""
        return {
            "native_available": self._use_native,
            "trained": self._trained,
            "method": "kpc" if self._use_native else "zlib",
            "evolve_rounds": self._evolve_rounds,
        }


# ──────────────────────────────────────────────────────────────────────
#  FastAPI роутер
# ──────────────────────────────────────────────────────────────────────

def create_archiver_router() -> object:
    """Создать FastAPI роутер для архиватора.

    Returns:
        APIRouter с маршрутами /compress, /decompress, /stats
    """
    try:
        from fastapi import APIRouter
        from pydantic import BaseModel
    except ImportError:
        return None

    router = APIRouter(prefix="/api/archiver", tags=["archiver"])
    _service = ArchiverService()

    class CompressRequest(BaseModel):
        text: str
        train_rounds: int = 5

    class CompressResponse(BaseModel):
        original_size: int
        compressed_size: int
        ratio: float
        method: str
        success: bool
        error: str = ""

    @router.post("/compress", response_model=CompressResponse)
    def compress_endpoint(req: CompressRequest) -> CompressResponse:
        data = req.text.encode("utf-8")
        if req.train_rounds > 0:
            _service.train(data, req.train_rounds)
        result = _service.compress(data)
        return CompressResponse(
            original_size=result.original_size,
            compressed_size=result.compressed_size,
            ratio=result.ratio,
            method=result.method,
            success=result.success,
            error=result.error,
        )

    @router.get("/stats")
    def stats_endpoint() -> dict[str, object]:
        return _service.get_stats()

    return router
