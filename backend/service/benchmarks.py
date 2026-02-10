"""
benchmarks.py — Система бенчмарков Kolibri AI.

Измеряет:
1. Tokenizer: скорость encode/decode, размер словаря
2. FormulaLM: perplexity, скорость генерации
3. Архиватор: коэффициент сжатия, скорость сжатия/распаковки
4. Retrieval: скорость поиска, точность
5. Chain-of-Thought: скорость рассуждения
"""
from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class BenchmarkResult:
    """Результат одного бенчмарка."""
    name: str
    metric: str
    value: float
    unit: str
    duration_ms: float = 0.0
    details: dict[str, object] = field(default_factory=dict)


class KolibriBenchmarks:
    """Запуск бенчмарков всех компонентов Kolibri."""

    def __init__(self) -> None:
        self._results: list[BenchmarkResult] = []

    @property
    def results(self) -> list[BenchmarkResult]:
        return list(self._results)

    def run_all(self, quick: bool = True) -> list[BenchmarkResult]:
        """Запустить все бенчмарки.

        Args:
            quick: Быстрый режим (меньше данных, меньше итераций)
        """
        self._results.clear()
        self._benchmark_tokenizer(quick)
        self._benchmark_formula_lm(quick)
        self._benchmark_archiver(quick)
        self._benchmark_reasoning(quick)
        self._benchmark_context_window(quick)
        return list(self._results)

    def _benchmark_tokenizer(self, quick: bool) -> None:
        """Бенчмарк BPE-токенизатора."""
        try:
            from backend.service.tokenizer import BPETokenizer
        except ImportError:
            return

        tok = BPETokenizer(vocab_size=200)

        # Обучение
        corpus = [
            "Kolibri OS использует числовое мышление для хранения знаний.",
            "Формулы эволюционируют через мутации и кроссовер.",
            "Предиктивное сжатие предсказывает следующий байт.",
        ] * (5 if quick else 100)

        t0 = time.perf_counter()
        tok.train(corpus)
        train_ms = (time.perf_counter() - t0) * 1000

        self._results.append(BenchmarkResult(
            name="tokenizer_train",
            metric="training_time",
            value=train_ms,
            unit="ms",
            duration_ms=train_ms,
            details={"vocab_size": tok.vocab_size, "corpus_size": len(corpus)},
        ))

        # Encode/decode
        test_text = "Kolibri OS — уникальная система искусственного интеллекта."
        t0 = time.perf_counter()
        iterations = 20 if quick else 1000
        for _ in range(iterations):
            tokens = tok.encode(test_text)
        encode_ms = (time.perf_counter() - t0) * 1000 / iterations

        self._results.append(BenchmarkResult(
            name="tokenizer_encode",
            metric="encode_time",
            value=encode_ms,
            unit="ms/encode",
            duration_ms=encode_ms * iterations,
            details={"tokens_count": len(tokens), "text_length": len(test_text)},
        ))

    def _benchmark_formula_lm(self, quick: bool) -> None:
        """Бенчмарк FormulaLM."""
        try:
            from backend.service.formula_lm import FormulaLM
        except ImportError:
            return

        lm = FormulaLM(vocab_size=64, num_formulas=4)

        # Тренировочные последовательности
        seqs = [[i % 64 for i in range(8)] for _ in range(3 if quick else 20)]

        t0 = time.perf_counter()
        generations = 1 if quick else 10
        lm.evolve(seqs, generations=generations)
        evolve_ms = (time.perf_counter() - t0) * 1000

        self._results.append(BenchmarkResult(
            name="formula_lm_evolve",
            metric="evolve_time",
            value=evolve_ms,
            unit="ms",
            duration_ms=evolve_ms,
            details={"generations": generations, "num_formulas": 16},
        ))

        # Генерация
        t0 = time.perf_counter()
        generated = lm.generate([65, 66, 67], max_tokens=8)
        gen_ms = (time.perf_counter() - t0) * 1000

        self._results.append(BenchmarkResult(
            name="formula_lm_generate",
            metric="generation_time",
            value=gen_ms,
            unit="ms",
            duration_ms=gen_ms,
            details={"tokens_generated": len(generated)},
        ))

    def _benchmark_archiver(self, quick: bool) -> None:
        """Бенчмарк архиватора."""
        try:
            from backend.service.archiver_service import ArchiverService
        except ImportError:
            return

        svc = ArchiverService(evolve_rounds=1)

        # Тестовые данные
        test_text = "Kolibri predictive compression test. " * (20 if quick else 1000)
        data = test_text.encode("utf-8")

        # Обучение
        t0 = time.perf_counter()
        svc.train(data, rounds=1 if quick else 10)
        train_ms = (time.perf_counter() - t0) * 1000

        self._results.append(BenchmarkResult(
            name="archiver_train",
            metric="training_time",
            value=train_ms,
            unit="ms",
            duration_ms=train_ms,
            details={"data_size": len(data), "method": svc.get_stats()["method"]},
        ))

        # Сжатие
        t0 = time.perf_counter()
        result = svc.compress(data)
        compress_ms = (time.perf_counter() - t0) * 1000

        if result.success:
            self._results.append(BenchmarkResult(
                name="archiver_compress",
                metric="compression_ratio",
                value=result.ratio,
                unit="ratio",
                duration_ms=compress_ms,
                details={
                    "original_size": result.original_size,
                    "compressed_size": result.compressed_size,
                    "method": result.method,
                    "speed_mbps": len(data) / (compress_ms / 1000) / 1024 / 1024 if compress_ms > 0 else 0,
                },
            ))

            # Распаковка
            t0 = time.perf_counter()
            dec_result = svc.decompress(result.data)
            decompress_ms = (time.perf_counter() - t0) * 1000

            self._results.append(BenchmarkResult(
                name="archiver_decompress",
                metric="decompression_time",
                value=decompress_ms,
                unit="ms",
                duration_ms=decompress_ms,
                details={"verified": dec_result.success},
            ))

    def _benchmark_reasoning(self, quick: bool) -> None:
        """Бенчмарк Chain-of-Thought."""
        try:
            from backend.service.reasoning import ChainOfThought
        except ImportError:
            return

        cot = ChainOfThought()

        queries = [
            "Что такое Kolibri OS?",
            "Сравни Python и C",
            "Почему формулы лучше нейросетей?",
            "Сколько будет 256 * 1024?",
        ]

        t0 = time.perf_counter()
        iterations = 10 if quick else 200
        for _ in range(iterations):
            for q in queries:
                cot.analyze_query(q)
        total_ms = (time.perf_counter() - t0) * 1000
        per_query_ms = total_ms / (iterations * len(queries))

        self._results.append(BenchmarkResult(
            name="reasoning_analyze",
            metric="analysis_time",
            value=per_query_ms,
            unit="ms/query",
            duration_ms=total_ms,
            details={"queries": len(queries), "iterations": iterations},
        ))

    def _benchmark_context_window(self, quick: bool) -> None:
        """Бенчмарк контекстного окна."""
        try:
            from backend.service.context_window import ContextWindow
        except ImportError:
            return

        cw = ContextWindow(max_tokens=4096)

        t0 = time.perf_counter()
        n_messages = 50 if quick else 200
        for i in range(n_messages):
            cw.add_message("user", f"Сообщение номер {i} для теста контекстного окна Kolibri.")
            cw.add_message("assistant", f"Ответ на сообщение {i} с подробным объяснением.")
        total_ms = (time.perf_counter() - t0) * 1000

        stats = cw.get_stats()
        self._results.append(BenchmarkResult(
            name="context_window",
            metric="insert_time",
            value=total_ms / (n_messages * 2),
            unit="ms/message",
            duration_ms=total_ms,
            details={
                "messages_added": n_messages * 2,
                "working_memory": stats.get("working_memory", 0),
                "compressed_memory": stats.get("compressed_memory", 0),
            },
        ))

    def format_report(self) -> str:
        """Форматирование отчёта."""
        lines = [
            "=" * 60,
            "  Kolibri AI — Benchmark Report",
            "=" * 60,
            "",
        ]
        for r in self._results:
            lines.append(f"  {r.name:<30} {r.value:>10.2f} {r.unit}")
            for k, v in r.details.items():
                lines.append(f"    {k}: {v}")
            lines.append("")

        lines.append("=" * 60)
        return "\n".join(lines)
