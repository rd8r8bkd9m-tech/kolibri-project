"""Тесты для модуля бенчмарков Kolibri."""
from __future__ import annotations

import pytest
from backend.service.benchmarks import BenchmarkResult, KolibriBenchmarks


class TestBenchmarkResult:
    def test_creation(self) -> None:
        r = BenchmarkResult(
            name="test",
            metric="speed",
            value=42.0,
            unit="ms",
        )
        assert r.name == "test"
        assert r.value == 42.0
        assert r.duration_ms == 0.0
        assert r.details == {}

    def test_with_details(self) -> None:
        r = BenchmarkResult(
            name="compress",
            metric="ratio",
            value=3.14,
            unit="x",
            duration_ms=100.0,
            details={"method": "zlib"},
        )
        assert r.details["method"] == "zlib"
        assert r.duration_ms == 100.0


class TestKolibriBenchmarks:
    def test_creation(self) -> None:
        bench = KolibriBenchmarks()
        assert bench.results == []

    def test_run_all_quick(self) -> None:
        bench = KolibriBenchmarks()
        results = bench.run_all(quick=True)
        assert len(results) > 0
        for r in results:
            assert isinstance(r, BenchmarkResult)
            assert r.name != ""
            assert r.unit != ""

    def test_tokenizer_benchmark(self) -> None:
        bench = KolibriBenchmarks()
        bench._benchmark_tokenizer(quick=True)
        names = [r.name for r in bench.results]
        assert "tokenizer_train" in names
        assert "tokenizer_encode" in names

    def test_formula_lm_benchmark(self) -> None:
        bench = KolibriBenchmarks()
        bench._benchmark_formula_lm(quick=True)
        names = [r.name for r in bench.results]
        assert "formula_lm_evolve" in names
        assert "formula_lm_generate" in names

    def test_archiver_benchmark(self) -> None:
        bench = KolibriBenchmarks()
        bench._benchmark_archiver(quick=True)
        names = [r.name for r in bench.results]
        # Должен быть как минимум train
        assert "archiver_train" in names

    def test_reasoning_benchmark(self) -> None:
        bench = KolibriBenchmarks()
        bench._benchmark_reasoning(quick=True)
        assert len(bench.results) == 1
        assert bench.results[0].name == "reasoning_analyze"
        assert bench.results[0].value > 0

    def test_context_window_benchmark(self) -> None:
        bench = KolibriBenchmarks()
        bench._benchmark_context_window(quick=True)
        assert len(bench.results) == 1
        assert bench.results[0].name == "context_window"

    def test_format_report(self) -> None:
        bench = KolibriBenchmarks()
        bench.run_all(quick=True)
        report = bench.format_report()
        assert "Kolibri AI" in report
        assert "Benchmark Report" in report
        assert len(report) > 100

    def test_results_immutable_copy(self) -> None:
        bench = KolibriBenchmarks()
        bench.run_all(quick=True)
        r1 = bench.results
        r2 = bench.results
        assert r1 is not r2
        assert len(r1) == len(r2)

    def test_positive_values(self) -> None:
        bench = KolibriBenchmarks()
        bench.run_all(quick=True)
        for r in bench.results:
            assert r.value >= 0, f"{r.name} has negative value: {r.value}"
            assert r.duration_ms >= 0, f"{r.name} has negative duration"

    def test_run_all_clears_previous(self) -> None:
        bench = KolibriBenchmarks()
        bench.run_all(quick=True)
        n1 = len(bench.results)
        bench.run_all(quick=True)
        n2 = len(bench.results)
        assert n1 == n2  # Повторный запуск, а не накопление
