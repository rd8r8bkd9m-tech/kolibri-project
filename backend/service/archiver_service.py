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
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional

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

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
SUPER_ARCHIVER_TOOL = PROJECT_ROOT / "tools" / "kolibri-agi.py"
VAULT_ARCHIVER_TOOL = PROJECT_ROOT / "tools" / "kolibri-vault.py"
DEFAULT_ARCHIVE_ROOT = Path(os.environ.get("KOLIBRI_ARCHIVER_OUTPUT_DIR", "/tmp/kolibri_archives"))
DEFAULT_RESTORE_ROOT = Path(os.environ.get("KOLIBRI_ARCHIVER_RESTORE_DIR", "/tmp/kolibri_restores"))
DEFAULT_STORAGE_ROOT = Path(os.environ.get("KOLIBRI_ARCHIVER_STORAGE_DIR", "/tmp/kolibri_archiver_storage"))


def _safe_seed(value: object, fallback: str) -> str:
    """Normalize a user-visible seed into a portable filename stem."""
    raw = str(value or fallback or f"kolibri_{int(time.time())}")
    seed = re.sub(r"[^A-Za-z0-9_.-]+", "_", raw).strip("._-")
    return (seed or f"kolibri_{int(time.time())}")[:96]


def _expanded_path(value: object, default: Path | str | None = None) -> Path:
    raw = value if value not in (None, "") else default
    if raw is None:
        raise ValueError("path is required")
    return Path(str(raw)).expanduser()


def _existing_path(value: object, label: str) -> Path:
    path = _expanded_path(value).resolve()
    if not path.exists():
        raise FileNotFoundError(f"{label} not found: {path}")
    return path


def _artifact(path: Path) -> dict[str, object]:
    return {
        "path": str(path),
        "exists": path.exists(),
        "size": path.stat().st_size if path.exists() else 0,
    }


def _parse_archiver_report(text: str) -> dict[str, object]:
    report: dict[str, object] = {}

    def take_int(key: str, pattern: str) -> None:
        match = re.search(pattern, text, re.MULTILINE)
        if match:
            report[key] = int(match.group(1))

    take_int("original_size", r"(?:Исходный размер|Source bytes):\s+(\d+)")
    take_int("seed_size", r"Seed:\s+(\d+)\s+bytes")
    take_int("bin_size", r"Bin corpus:\s+(\d+)\s+bytes")
    take_int("seed_bin_size", r"Seed\+bin:\s+(\d+)\s+bytes")
    take_int("vault_size", r"Vault sealed: .*?\((\d+)\s+bytes\)")
    take_int("pure_formula_bytes", r"(?:Чистая формула|Pure formula):\s+(\d+)\s+bytes")
    take_int("residual_bytes", r"(?:Lossless residual|Residual):\s+(\d+)\s+bytes")
    ratio = re.search(r"^Коэффициент:\s+([0-9.]+)x", text, re.MULTILINE)
    if ratio is None:
        ratio = re.search(r"^\[\*\]\s+Ratio:\s+([0-9.]+)x", text, re.MULTILINE)
    if ratio:
        report["ratio"] = float(ratio.group(1))

    ru_counts = re.search(
        r"Файлов:\s+(\d+)\s*\nДиректорий:\s+(\d+)\s*\nSymlink:\s+(\d+)",
        text,
        re.MULTILINE,
    )
    slash_counts = re.search(r"Files/dirs/symlinks:\s+(\d+)/(\d+)/(\d+)", text)
    counts = ru_counts or slash_counts
    if counts:
        report["files"] = int(counts.group(1))
        report["dirs"] = int(counts.group(2))
        report["symlinks"] = int(counts.group(3))

    bit_exact = re.search(r"Bit-exact:\s+(yes|no)", text, re.IGNORECASE)
    if bit_exact:
        report["bit_exact"] = bit_exact.group(1).lower() == "yes"
    elif "BIT-EXACT MATCH" in text:
        report["bit_exact"] = True
    elif "BIT-EXACT MISMATCH" in text:
        report["bit_exact"] = False

    return report


def _run_super_tool(
    tool: Path,
    args: list[str],
    seed: str,
    extra_env: dict[str, str] | None = None,
    timeout_sec: int = 3600,
) -> dict[str, object]:
    if not tool.exists():
        raise FileNotFoundError(f"Kolibri tool not found: {tool}")

    env = os.environ.copy()
    env.setdefault("PYTHONUNBUFFERED", "1")
    env.setdefault("KOLIBRI_AGI_STORAGE", str((DEFAULT_STORAGE_ROOT / seed).expanduser()))
    if extra_env:
        env.update(extra_env)

    proc = subprocess.run(
        [sys.executable, str(tool), *args],
        cwd=str(PROJECT_ROOT),
        env=env,
        text=True,
        capture_output=True,
        timeout=timeout_sec,
        check=False,
    )
    combined = "\n".join(part for part in (proc.stdout, proc.stderr) if part)
    return {
        "returncode": proc.returncode,
        "success": proc.returncode == 0,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
        "report": _parse_archiver_report(combined),
    }


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
    "../build/libkolibri_compress.dylib",
    "../build/libkolibri_compress.so",
    "../build/libkolibri_compress.dll",
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
    root = Path(__file__).resolve().parent.parent.parent
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
            "super_hybrid": {
                "seed_bin_available": SUPER_ARCHIVER_TOOL.exists(),
                "vault_available": VAULT_ARCHIVER_TOOL.exists(),
                "seed_bin_tool": str(SUPER_ARCHIVER_TOOL),
                "vault_tool": str(VAULT_ARCHIVER_TOOL),
                "default_archive_root": str(DEFAULT_ARCHIVE_ROOT),
                "default_restore_root": str(DEFAULT_RESTORE_ROOT),
            },
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

    def as_bool(value: object, default: bool) -> bool:
        if value is None:
            return default
        if isinstance(value, bool):
            return value
        return str(value).strip().lower() in {"1", "true", "yes", "y", "да"}

    def as_timeout(payload: dict[str, Any]) -> int:
        try:
            return max(1, int(payload.get("timeout_sec") or 3600))
        except (TypeError, ValueError):
            return 3600

    @router.get("/project/status")
    def project_archiver_status_endpoint() -> dict[str, object]:
        return {
            "success": True,
            "project_root": str(PROJECT_ROOT),
            "seed_bin_available": SUPER_ARCHIVER_TOOL.exists(),
            "vault_available": VAULT_ARCHIVER_TOOL.exists(),
            "seed_bin_tool": str(SUPER_ARCHIVER_TOOL),
            "vault_tool": str(VAULT_ARCHIVER_TOOL),
            "default_archive_root": str(DEFAULT_ARCHIVE_ROOT),
            "default_restore_root": str(DEFAULT_RESTORE_ROOT),
        }

    @router.post("/project/pair")
    def project_pair_endpoint(payload: dict[str, Any] = Body(...)) -> dict[str, object]:
        """Создать переносимую пару name.seed + name.bin, опционально с roundtrip-проверкой."""
        try:
            source_path = _existing_path(payload.get("source_path") or PROJECT_ROOT, "source_path")
            seed = _safe_seed(payload.get("seed"), source_path.stem if source_path.is_file() else source_path.name)
            output_dir = _expanded_path(payload.get("output_dir"), DEFAULT_ARCHIVE_ROOT / seed).resolve()
            restore_dir = _expanded_path(payload.get("restore_dir"), DEFAULT_RESTORE_ROOT / seed).resolve()
            verify = as_bool(payload.get("verify"), True)
            output_dir.mkdir(parents=True, exist_ok=True)
            restore_dir.parent.mkdir(parents=True, exist_ok=True)

            action = "roundtrip" if verify else "pair"
            args = [action, "--path", str(source_path), "--seed", seed, "--out", str(output_dir)]
            if verify:
                args += ["--restore", str(restore_dir)]

            formula_report_path = output_dir / f"{seed}.formula-report.json"
            result = _run_super_tool(
                SUPER_ARCHIVER_TOOL,
                args,
                seed,
                extra_env={"KOLIBRI_AGI_FORMULA_REPORT": str(formula_report_path)},
                timeout_sec=as_timeout(payload),
            )
            seed_path = output_dir / f"{seed}.seed"
            bin_path = output_dir / f"{seed}.bin"
            result.update({
                "source_path": str(source_path),
                "restore_dir": str(restore_dir) if verify else "",
                "artifacts": {
                    "seed": _artifact(seed_path),
                    "bin": _artifact(bin_path),
                    "formula_report": _artifact(formula_report_path),
                },
            })
            return result
        except subprocess.TimeoutExpired:
            return {"success": False, "error": "Kolibri seed+bin operation timed out"}
        except Exception as exc:
            return {"success": False, "error": str(exc)}

    @router.post("/project/restore-pair")
    def project_restore_pair_endpoint(payload: dict[str, Any] = Body(...)) -> dict[str, object]:
        """Восстановить проект из переносимой пары name.seed + name.bin."""
        try:
            seed_path = _existing_path(payload.get("seed_path"), "seed_path")
            bin_path = _existing_path(payload.get("bin_path"), "bin_path")
            seed = _safe_seed(payload.get("seed"), seed_path.stem)
            target_path = _expanded_path(payload.get("target_path"), DEFAULT_RESTORE_ROOT / seed).resolve()
            target_path.parent.mkdir(parents=True, exist_ok=True)

            result = _run_super_tool(
                SUPER_ARCHIVER_TOOL,
                [
                    "restore-pair",
                    "--seed",
                    str(seed_path),
                    "--bin",
                    str(bin_path),
                    "--target",
                    str(target_path),
                ],
                seed,
                timeout_sec=as_timeout(payload),
            )
            result.update({
                "target_path": str(target_path),
                "artifacts": {
                    "seed": _artifact(seed_path),
                    "bin": _artifact(bin_path),
                },
            })
            return result
        except subprocess.TimeoutExpired:
            return {"success": False, "error": "Kolibri restore-pair operation timed out"}
        except Exception as exc:
            return {"success": False, "error": str(exc)}

    @router.post("/project/vault")
    def project_vault_endpoint(payload: dict[str, Any] = Body(...)) -> dict[str, object]:
        """Создать автономный .klb Vault с meta-formula стратегией."""
        try:
            source_path = _existing_path(payload.get("source_path") or PROJECT_ROOT, "source_path")
            seed = _safe_seed(payload.get("seed"), source_path.stem if source_path.is_file() else source_path.name)
            vault_path = _expanded_path(payload.get("vault_path"), DEFAULT_ARCHIVE_ROOT / seed / f"{seed}.klb").resolve()
            vault_path.parent.mkdir(parents=True, exist_ok=True)

            mode = str(payload.get("mode") or "standalone")
            strategy = str(payload.get("strategy") or "auto")
            if mode not in {"standalone", "linked"}:
                return {"success": False, "error": f"unsupported vault mode: {mode}"}
            if strategy not in {"auto", "embedded_world_model", "materialized_atoms"}:
                return {"success": False, "error": f"unsupported vault strategy: {strategy}"}

            result = _run_super_tool(
                VAULT_ARCHIVER_TOOL,
                [
                    "create",
                    str(source_path),
                    str(vault_path),
                    "--seed",
                    seed,
                    "--mode",
                    mode,
                    "--strategy",
                    strategy,
                ],
                seed,
                timeout_sec=as_timeout(payload),
            )

            verify_result: dict[str, object] | None = None
            if result.get("success") and as_bool(payload.get("verify"), True):
                verify_result = _run_super_tool(
                    VAULT_ARCHIVER_TOOL,
                    ["verify", str(vault_path)],
                    seed,
                    timeout_sec=as_timeout(payload),
                )

            result.update({
                "source_path": str(source_path),
                "vault_path": str(vault_path),
                "artifacts": {"vault": _artifact(vault_path)},
                "verify": verify_result,
            })
            return result
        except subprocess.TimeoutExpired:
            return {"success": False, "error": "Kolibri vault operation timed out"}
        except Exception as exc:
            return {"success": False, "error": str(exc)}

    @router.post("/project/extract-vault")
    def project_extract_vault_endpoint(payload: dict[str, Any] = Body(...)) -> dict[str, object]:
        """Восстановить проект из .klb Vault."""
        try:
            vault_path = _existing_path(payload.get("vault_path"), "vault_path")
            seed = _safe_seed(payload.get("seed"), vault_path.stem)
            target_path = _expanded_path(payload.get("target_path"), DEFAULT_RESTORE_ROOT / seed).resolve()
            target_path.parent.mkdir(parents=True, exist_ok=True)

            result = _run_super_tool(
                VAULT_ARCHIVER_TOOL,
                ["extract", str(vault_path), str(target_path)],
                seed,
                timeout_sec=as_timeout(payload),
            )
            result.update({
                "target_path": str(target_path),
                "artifacts": {"vault": _artifact(vault_path)},
            })
            return result
        except subprocess.TimeoutExpired:
            return {"success": False, "error": "Kolibri extract-vault operation timed out"}
        except Exception as exc:
            return {"success": False, "error": str(exc)}

    return router
