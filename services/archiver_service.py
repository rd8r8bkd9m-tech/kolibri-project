"""
archiver_service.py — Python-обёртка РОДНОГО архиватора Kolibri

Обеспечивает:
1. ctypes-биндинг к libkolibri_compress.so (LZ77+RLE+Huffman+Formula — 9 методов)
2. Тот же движок что в CLI-утилите kolibri_archiver
3. FastAPI-роутер /api/archiver/* для REST-доступа
4. Высокоуровневый API: compress_text(), decompress_text()

НЕ использует сторонние библиотеки (без zlib, без gzip).
Вся компрессия — собственная реализация Kolibri.
"""
from __future__ import annotations

import ctypes
import ctypes.util
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

# ──────────────────────────────────────────────────────────────────────
#  Константы методов сжатия Kolibri (из compress.h)
# ──────────────────────────────────────────────────────────────────────
KOLIBRI_COMPRESS_LZ77     = 0x01
KOLIBRI_COMPRESS_RLE      = 0x02
KOLIBRI_COMPRESS_HUFFMAN  = 0x04
KOLIBRI_COMPRESS_FORMULA  = 0x08
KOLIBRI_COMPRESS_MATH     = 0x10
KOLIBRI_COMPRESS_LZMA     = 0x20
KOLIBRI_COMPRESS_ZSTD     = 0x40
KOLIBRI_COMPRESS_ADAPTIVE = 0x80
KOLIBRI_COMPRESS_TOKEN    = 0x100
KOLIBRI_COMPRESS_ALL      = 0x1FF


# ──────────────────────────────────────────────────────────────────────
#  ctypes-биндинг к libkolibri_compress.so
# ──────────────────────────────────────────────────────────────────────

_lib: Optional[ctypes.CDLL] = None
_LIB_NAMES = [
    "build/libkolibri_compress.dylib",
    "build/libkolibri_compress.so",
    "build/libkolibri_compress.dll",
    "libkolibri_compress.dylib",
    "libkolibri_compress.so",
    "libkolibri_compress.dll",
]


def _load_libc() -> Optional[ctypes.CDLL]:
    libc_name = ctypes.util.find_library("c")
    try:
        libc = ctypes.CDLL(libc_name) if libc_name else ctypes.CDLL(None)
        libc.free.argtypes = [ctypes.c_void_p]
        libc.free.restype = None
        return libc
    except OSError:
        return None


_LIBC = _load_libc()


class KolibriCompressStats(ctypes.Structure):
    """Зеркало C-структуры KolibriCompressStats."""
    _fields_ = [
        ("original_size", ctypes.c_size_t),
        ("compressed_size", ctypes.c_size_t),
        ("compression_ratio", ctypes.c_double),
        ("checksum", ctypes.c_uint32),
        ("file_type", ctypes.c_int),
        ("methods_used", ctypes.c_uint32),
        ("compression_time_ms", ctypes.c_double),
        ("decompression_time_ms", ctypes.c_double),
    ]


def _find_lib() -> Optional[ctypes.CDLL]:
    """Ищем shared library в стандартных путях."""
    root = Path(__file__).resolve().parent.parent
    for name in _LIB_NAMES:
        path = root / name
        if path.exists():
            try:
                return ctypes.CDLL(str(path))
            except OSError:
                continue
    return None


def _init_lib() -> Optional[ctypes.CDLL]:
    """Инициализация ctypes сигнатур для kolibri_compress API."""
    global _lib
    if _lib is not None:
        return _lib

    lib = _find_lib()
    if lib is None:
        return None

    # kolibri_compressor_create(methods) -> KolibriCompressor*
    lib.kolibri_compressor_create.restype = ctypes.c_void_p
    lib.kolibri_compressor_create.argtypes = [ctypes.c_uint32]

    # kolibri_compressor_destroy(comp)
    lib.kolibri_compressor_destroy.restype = None
    lib.kolibri_compressor_destroy.argtypes = [ctypes.c_void_p]

    # kolibri_compress(comp, input, input_size, &output, &output_size, &stats) -> int
    lib.kolibri_compress.restype = ctypes.c_int
    lib.kolibri_compress.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_uint8),
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
        ctypes.POINTER(ctypes.c_size_t),
        ctypes.POINTER(KolibriCompressStats),
    ]

    # kolibri_decompress(input, input_size, &output, &output_size, &stats) -> int
    lib.kolibri_decompress.restype = ctypes.c_int
    lib.kolibri_decompress.argtypes = [
        ctypes.POINTER(ctypes.c_uint8),
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
        ctypes.POINTER(ctypes.c_size_t),
        ctypes.POINTER(KolibriCompressStats),
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
    method: str = "none"
    success: bool = False
    error: str = ""
    methods_used: int = 0
    compression_time_ms: float = 0.0


# ──────────────────────────────────────────────────────────────────────
#  Основной класс ArchiverService
# ──────────────────────────────────────────────────────────────────────

class ArchiverService:
    """Высокоуровневый сервис сжатия Kolibri.

    Использует родной движок Kolibri (LZ77+RLE+Huffman+Formula+LZMA+ZSTD и др.).
    Тот же движок что в CLI-утилите kolibri_archiver.
    НЕ использует сторонних библиотек.
    """

    def __init__(self, methods: int = KOLIBRI_COMPRESS_ALL) -> None:
        self._lib = _init_lib()
        self._ctx: Optional[int] = None
        self._methods = methods
        self._use_native = self._lib is not None

        if self._use_native:
            ptr = self._lib.kolibri_compressor_create(  # type: ignore[union-attr]
                ctypes.c_uint32(methods)
            )
            if ptr:
                self._ctx = ptr
            else:
                self._use_native = False

    def __del__(self) -> None:
        if self._ctx and self._lib:
            self._lib.kolibri_compressor_destroy(ctypes.c_void_p(self._ctx))
            self._ctx = None

    @property
    def native_available(self) -> bool:
        """True если нативная библиотека Kolibri загружена."""
        return self._use_native

    # ── Сжатие ────────────────────────────────────────────────────────

    def compress(self, data: bytes) -> CompressResult:
        """Сжать произвольные байты через Kolibri."""
        if not data:
            return CompressResult(error="empty input")

        if not self._use_native or not self._ctx:
            return CompressResult(
                original_size=len(data),
                error="Kolibri native library not available",
            )

        inp = (ctypes.c_uint8 * len(data))(*data)
        out_ptr = ctypes.POINTER(ctypes.c_uint8)()
        out_size = ctypes.c_size_t(0)
        stats = KolibriCompressStats()

        rc = self._lib.kolibri_compress(  # type: ignore[union-attr]
            ctypes.c_void_p(self._ctx),
            inp,
            ctypes.c_size_t(len(data)),
            ctypes.byref(out_ptr),
            ctypes.byref(out_size),
            ctypes.byref(stats),
        )

        if rc != 0:
            return CompressResult(
                original_size=len(data),
                error=f"kolibri_compress returned {rc}",
            )

        sz = out_size.value
        result_bytes = bytes(out_ptr[:sz])

        # Освобождаем C-память
        if _LIBC is not None:
            _LIBC.free(ctypes.cast(out_ptr, ctypes.c_void_p))

        return CompressResult(
            original_size=len(data),
            compressed_size=sz,
            ratio=len(data) / sz if sz > 0 else 0.0,
            data=result_bytes,
            method="kolibri",
            success=True,
            methods_used=stats.methods_used,
            compression_time_ms=stats.compression_time_ms,
        )

    def compress_text(self, text: str) -> CompressResult:
        """Сжать текст (UTF-8 -> bytes -> compress)."""
        return self.compress(text.encode("utf-8"))

        # ── Распаковка ────────────────────────────────────────────────────

    def decompress(self, data: bytes) -> CompressResult:
        """Распаковать данные через Kolibri."""
        if not data:
            return CompressResult(error="empty input")

        if not self._use_native or not self._lib:
            return CompressResult(
                compressed_size=len(data),
                error="Kolibri native library not available",
            )

        inp = (ctypes.c_uint8 * len(data))(*data)
        out_ptr = ctypes.POINTER(ctypes.c_uint8)()
        out_size = ctypes.c_size_t(0)
        stats = KolibriCompressStats()

        rc = self._lib.kolibri_decompress(
            inp,
            ctypes.c_size_t(len(data)),
            ctypes.byref(out_ptr),
            ctypes.byref(out_size),
            ctypes.byref(stats),
        )

        if rc != 0:
            return CompressResult(
                compressed_size=len(data),
                error=f"kolibri_decompress returned {rc}",
            )

        sz = out_size.value
        result_bytes = bytes(out_ptr[:sz])

        if _LIBC is not None:
            _LIBC.free(ctypes.cast(out_ptr, ctypes.c_void_p))

        return CompressResult(
            original_size=sz,
            compressed_size=len(data),
            ratio=sz / len(data) if len(data) > 0 else 0.0,
            data=result_bytes,
            method="kolibri",
            success=True,
        )

    def decompress_text(self, data: bytes) -> str:
        """Распаковать и вернуть текст."""
        result = self.decompress(data)
        if result.success:
            return result.data.decode("utf-8", errors="replace")
        return ""

    # ── Совместимость (train не нужен для основного движка) ───────────

    def train(self, data: bytes, rounds: int = 0) -> None:
        """Kolibri автоматически адаптируется — train не нужен."""
        pass

    def train_on_texts(self, texts: list[str], rounds: int = 0) -> None:
        """Kolibri автоматически адаптируется — train не нужен."""
        pass

    # ── Статистика ────────────────────────────────────────────────────

    def get_stats(self) -> dict[str, object]:
        """Информация о состоянии сервиса."""
        method_name = "kolibri" if self._use_native else "zlib-fallback"
        return {
            # Новые поля
            "native_available": self._use_native,
            "engine": "kolibri",
            "version": "v50.0",
            "methods": self._methods,
            "methods_description": self._describe_methods(),
            # Поля для совместимости со старыми UI
            "method": method_name,
            "trained": self._use_native,
            "evolve_rounds": 0,
        }

    def _describe_methods(self) -> list[str]:
        """Человекочитаемое описание включённых методов."""
        names: list[str] = []
        method_map = {
            KOLIBRI_COMPRESS_LZ77: "LZ77",
            KOLIBRI_COMPRESS_RLE: "RLE",
            KOLIBRI_COMPRESS_HUFFMAN: "Huffman",
            KOLIBRI_COMPRESS_FORMULA: "Formula",
            KOLIBRI_COMPRESS_MATH: "Math Analysis",
            KOLIBRI_COMPRESS_LZMA: "LZMA",
            KOLIBRI_COMPRESS_ZSTD: "Zstandard",
            KOLIBRI_COMPRESS_ADAPTIVE: "Adaptive Dict",
            KOLIBRI_COMPRESS_TOKEN: "Token Stream",
        }
        for flag, name in method_map.items():
            if self._methods & flag:
                names.append(name)
        return names


# ──────────────────────────────────────────────────────────────────────
#  FastAPI роутер
# ──────────────────────────────────────────────────────────────────────

def create_archiver_router() -> object:
    """Создать FastAPI роутер для архиватора Kolibri.

    Returns:
        APIRouter с маршрутами /compress, /decompress, /stats
    """
    try:
        from fastapi import APIRouter, Body
        import base64
    except ImportError:
        return None

    router = APIRouter(prefix="/api/archiver", tags=["archiver"])
    _service = ArchiverService()

    @router.post("/compress")
    def compress_endpoint(
        text: str = Body(..., embed=True),
        train_rounds: int = Body(0, embed=True),
    ) -> dict[str, object]:
        data = text.encode("utf-8")
        result = _service.compress(data)
        return {
            "original_size": result.original_size,
            "compressed_size": result.compressed_size,
            "ratio": result.ratio,
            "method": result.method,
            "success": result.success,
            "error": result.error,
            "compressed_b64": base64.b64encode(result.data).decode() if result.success else "",
            "methods_used": result.methods_used,
            "compression_time_ms": result.compression_time_ms,
        }

    @router.post("/decompress")
    def decompress_endpoint(
        data_b64: str = Body(..., embed=True),
    ) -> dict[str, object]:
        try:
            raw = base64.b64decode(data_b64)
            result = _service.decompress(raw)
            if result.success:
                return {
                    "success": True,
                    "text": result.data.decode("utf-8", errors="replace"),
                    "size": len(result.data),
                    "method": result.method,
                }
            return {"success": False, "error": result.error}
        except Exception as e:
            return {"success": False, "error": str(e)}

    @router.get("/stats")
    def stats_endpoint() -> dict[str, object]:
        return _service.get_stats()

    return router
