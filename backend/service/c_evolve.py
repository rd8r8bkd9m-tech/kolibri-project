"""
c_evolve.py — Python ctypes-обёртка для C генетического алгоритма Kolibri

Автоматически компилирует и подключает libkolibri_evolve.so
для 100x ускорения evolve() и embedding training.

Если .so недоступна — graceful fallback на Python.
"""
from __future__ import annotations

import ctypes
import logging
import math
import os
import subprocess
import time
from ctypes import (
    POINTER,
    c_double,
    c_float,
    c_int,
    c_uint8,
    c_uint32,
    c_uint64,
)
from pathlib import Path
from typing import Optional

import numpy as np

log = logging.getLogger("kolibri.c_evolve")

# --- Пути ---
_PROJECT_ROOT = Path("/workspaces/kolibri-project")
_FFI_SRC = _PROJECT_ROOT / "backend" / "src" / "evolve_ffi.c"
_FFI_LIB = _PROJECT_ROOT / "build" / "libkolibri_evolve.so"

# --- Константы (зеркало C) ---
GENE_SIZE = 4000
POPULATION_SIZE = 16
PATTERN_SIZE = 64


def _compile_ffi() -> bool:
    """Компилировать FFI библиотеку если нужно."""
    if _FFI_LIB.exists():
        # Перекомпилируем только если исходник новее
        src_mtime = _FFI_SRC.stat().st_mtime if _FFI_SRC.exists() else 0
        lib_mtime = _FFI_LIB.stat().st_mtime
        if lib_mtime >= src_mtime:
            return True

    if not _FFI_SRC.exists():
        log.warning("FFI исходник не найден: %s", _FFI_SRC)
        return False

    _FFI_LIB.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        "gcc", "-O3", "-march=native", "-shared", "-fPIC",
        "-fvisibility=hidden",
        "-o", str(_FFI_LIB), str(_FFI_SRC), "-lm",
    ]
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=30,
        )
        if result.returncode != 0:
            log.error("Ошибка компиляции FFI: %s", result.stderr)
            return False
        log.info("FFI библиотека скомпилирована: %s", _FFI_LIB)
        return True
    except Exception as e:
        log.error("Не удалось скомпилировать FFI: %s", e)
        return False


class CEvolveBridge:
    """
    Мост к C-реализации генетического алгоритма через ctypes.

    Предоставляет 100x ускорение для:
    - evolve() — эволюция формул
    - train_embeddings() — Word2Vec skip-gram
    - word_to_pattern() — DJB2 + LCG каскад

    При недоступности C — graceful fallback на Python.
    """

    def __init__(self) -> None:
        self._lib: Optional[ctypes.CDLL] = None
        self._available = False
        self._load()

    def _load(self) -> None:
        """Загрузить C библиотеку."""
        if not _compile_ffi():
            log.info("CEvolveBridge: C-ускорение недоступно, используем Python")
            return

        try:
            self._lib = ctypes.CDLL(str(_FFI_LIB))
            self._setup_signatures()
            version = self._lib.ffi_version()
            log.info(
                "CEvolveBridge: загружена v%d (GENE=%d, POP=%d)",
                version,
                self._lib.ffi_gene_size(),
                self._lib.ffi_population_size(),
            )
            self._available = True
        except Exception as e:
            log.warning("CEvolveBridge: ошибка загрузки: %s", e)
            self._lib = None

    def _setup_signatures(self) -> None:
        """Настроить типы для ctypes."""
        lib = self._lib
        assert lib is not None

        # ffi_evolve
        lib.ffi_evolve.restype = c_double
        lib.ffi_evolve.argtypes = [
            POINTER(c_uint8),   # genomes
            POINTER(c_double),  # fitnesses
            POINTER(c_uint8),   # src_patterns
            POINTER(c_uint8),   # tgt_patterns
            c_int,              # n_pairs
            c_int,              # generations
            c_uint64,           # seed
        ]

        # ffi_train_embeddings
        lib.ffi_train_embeddings.restype = c_double
        lib.ffi_train_embeddings.argtypes = [
            POINTER(c_double),  # vectors
            POINTER(c_int),     # edge_src
            POINTER(c_int),     # edge_tgt
            POINTER(c_float),   # edge_weight
            c_int,              # n_edges
            c_int,              # vocab_size
            c_int,              # dim
            c_int,              # epochs
            c_double,           # lr
            c_int,              # neg_samples
        ]

        # ffi_word_to_pattern
        lib.ffi_word_to_pattern.restype = None
        lib.ffi_word_to_pattern.argtypes = [
            ctypes.c_char_p,    # word
            POINTER(c_uint8),   # out_pattern
        ]

        # ffi_djb2_hash
        lib.ffi_djb2_hash.restype = c_uint32
        lib.ffi_djb2_hash.argtypes = [ctypes.c_char_p]

        # info
        lib.ffi_version.restype = c_int
        lib.ffi_gene_size.restype = c_int
        lib.ffi_population_size.restype = c_int
        lib.ffi_pattern_size.restype = c_int

    @property
    def available(self) -> bool:
        return self._available

    def evolve(
        self,
        genomes: list[list[int]],
        fitnesses: list[float],
        semantic_pairs: list[tuple[list[int], list[int]]],
        generations: int = 10,
    ) -> tuple[list[list[int]], list[float], float]:
        """
        Эволюция формул через C.

        Args:
            genomes: [POPULATION][GENE_SIZE] — геномы формул
            fitnesses: [POPULATION] — текущие fitness
            semantic_pairs: [(src_pattern, tgt_pattern), ...] — обучающие пары
            generations: кол-во поколений

        Returns:
            (updated_genomes, updated_fitnesses, best_fitness)
        """
        if not self._available or not self._lib:
            raise RuntimeError("C библиотека недоступна")

        n_pop = len(genomes)
        n_pairs = len(semantic_pairs)

        # Упаковка геномов в плоский массив
        genome_arr = (c_uint8 * (n_pop * GENE_SIZE))()
        for i in range(n_pop):
            for j in range(min(len(genomes[i]), GENE_SIZE)):
                genome_arr[i * GENE_SIZE + j] = genomes[i][j] & 0xFF

        fit_arr = (c_double * n_pop)(*fitnesses)

        # Упаковка семантических пар
        src_arr = (c_uint8 * (n_pairs * PATTERN_SIZE))()
        tgt_arr = (c_uint8 * (n_pairs * PATTERN_SIZE))()
        for i, (src, tgt) in enumerate(semantic_pairs):
            for j in range(min(len(src), PATTERN_SIZE)):
                src_arr[i * PATTERN_SIZE + j] = src[j] & 0xFF
            for j in range(min(len(tgt), PATTERN_SIZE)):
                tgt_arr[i * PATTERN_SIZE + j] = tgt[j] & 0xFF

        seed = int(time.time() * 1000) & 0xFFFFFFFFFFFFFFFF

        # Вызов C
        best_fitness = self._lib.ffi_evolve(
            genome_arr, fit_arr, src_arr, tgt_arr,
            n_pairs, generations, seed,
        )

        # Распаковка
        new_genomes = []
        for i in range(n_pop):
            gene = [int(genome_arr[i * GENE_SIZE + j]) for j in range(GENE_SIZE)]
            new_genomes.append(gene)
        new_fitnesses = [float(fit_arr[i]) for i in range(n_pop)]

        return new_genomes, new_fitnesses, best_fitness

    def train_embeddings(
        self,
        vectors: dict[int, list[float]],
        edges: list[tuple[int, int, float]],
        dim: int = 64,
        epochs: int = 5,
        lr: float = 0.025,
        neg_samples: int = 5,
    ) -> tuple[dict[int, list[float]], float]:
        """
        Обучение эмбеддингов через C (Word2Vec Skip-gram).

        Args:
            vectors: {hash: [float]*dim} — таблица эмбеддингов
            edges: [(src_idx, tgt_idx, weight), ...] — рёбра графа
            dim: размерность вектора
            epochs: количество эпох
            lr: learning rate
            neg_samples: кол-во негативных примеров

        Returns:
            (updated_vectors, avg_loss)
        """
        if not self._available or not self._lib:
            raise RuntimeError("C библиотека недоступна")

        # Создаём маппинг hash → index
        hash_list = sorted(vectors.keys())
        hash_to_idx = {h: i for i, h in enumerate(hash_list)}
        vocab_size = len(hash_list)

        # Плоский массив векторов
        vec_arr = (c_double * (vocab_size * dim))()
        for i, h in enumerate(hash_list):
            v = vectors[h]
            for j in range(min(len(v), dim)):
                vec_arr[i * dim + j] = v[j]

        # Рёбра с перемаппленными индексами
        n_edges = len(edges)
        src_arr = (c_int * n_edges)()
        tgt_arr = (c_int * n_edges)()
        wgt_arr = (c_float * n_edges)()
        valid = 0
        for src_h, tgt_h, w in edges:
            if src_h in hash_to_idx and tgt_h in hash_to_idx:
                src_arr[valid] = hash_to_idx[src_h]
                tgt_arr[valid] = hash_to_idx[tgt_h]
                wgt_arr[valid] = w
                valid += 1

        if valid == 0:
            return vectors, 0.0

        avg_loss = self._lib.ffi_train_embeddings(
            vec_arr, src_arr, tgt_arr, wgt_arr,
            valid, vocab_size, dim, epochs, lr, neg_samples,
        )

        # Распаковка обратно
        updated = {}
        for i, h in enumerate(hash_list):
            updated[h] = [float(vec_arr[i * dim + j]) for j in range(dim)]

        return updated, float(avg_loss)

    def word_to_pattern(self, word: str) -> list[int]:
        """Числовой паттерн слова через C (DJB2 + LCG)."""
        if not self._available or not self._lib:
            raise RuntimeError("C библиотека недоступна")

        out = (c_uint8 * PATTERN_SIZE)()
        self._lib.ffi_word_to_pattern(word.encode("utf-8"), out)
        return [int(out[i]) for i in range(PATTERN_SIZE)]


# --- Глобальный singleton ---
_bridge: Optional[CEvolveBridge] = None


def get_c_evolve_bridge() -> CEvolveBridge:
    """Получить глобальный экземпляр CEvolveBridge."""
    global _bridge
    if _bridge is None:
        _bridge = CEvolveBridge()
    return _bridge
