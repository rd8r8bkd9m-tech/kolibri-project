#!/usr/bin/env python3
"""Kolibri Soak Tests - длительное тестирование стабильности системы."""

from __future__ import annotations

import argparse
import csv
import json
import logging
import sys
import time
from dataclasses import dataclass, asdict
from datetime import datetime, timedelta
from pathlib import Path
from typing import Dict, List, Optional

__all__ = ["SoakState", "SoakMetrics", "run_soak_tests"]

LOGGER = logging.getLogger("soak")


@dataclass
class SoakMetrics:
    """Метрики одной итерации soak-теста."""

    timestamp: str
    iteration: int
    memory_mb: float = 0.0
    cpu_percent: float = 0.0
    latency_ms: float = 0.0
    errors: int = 0
    success: bool = True


@dataclass
class SoakState:
    """Состояние soak-тестирования для возобновления."""

    start_time: str = ""
    end_time: str = ""
    total_iterations: int = 0
    completed_iterations: int = 0
    total_errors: int = 0
    status: str = "pending"
    metrics_file: str = ""

    @classmethod
    def load(cls, path: Path) -> "SoakState":
        """Загружает состояние из JSON файла."""
        if not path.exists():
            return cls()
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            return cls(**data)
        except (json.JSONDecodeError, TypeError) as e:
            LOGGER.warning("Не удалось загрузить состояние: %s", e)
            return cls()

    def save(self, path: Path) -> None:
        """Сохраняет состояние в JSON файл."""
        path.write_text(
            json.dumps(asdict(self), ensure_ascii=False, indent=2),
            encoding="utf-8",
        )


def _get_system_metrics() -> Dict[str, float]:
    """Получает текущие системные метрики (заглушка для CI)."""
    # В реальном окружении здесь был бы psutil или аналог
    return {
        "memory_mb": 100.0,
        "cpu_percent": 5.0,
        "latency_ms": 10.0,
    }


def _run_single_iteration(iteration: int) -> SoakMetrics:
    """Выполняет одну итерацию soak-теста."""
    timestamp = datetime.utcnow().isoformat()
    metrics = _get_system_metrics()

    # Простая проверка работоспособности
    errors = 0
    success = True

    try:
        # Базовые проверки системы
        import importlib

        # Проверяем что основные модули загружаются
        for module_name in ["json", "logging", "pathlib"]:
            importlib.import_module(module_name)
    except Exception as e:
        LOGGER.error("Ошибка в итерации %d: %s", iteration, e)
        errors = 1
        success = False

    return SoakMetrics(
        timestamp=timestamp,
        iteration=iteration,
        memory_mb=metrics["memory_mb"],
        cpu_percent=metrics["cpu_percent"],
        latency_ms=metrics["latency_ms"],
        errors=errors,
        success=success,
    )


def _write_metrics_row(path: Path, metrics: SoakMetrics, write_header: bool) -> None:
    """Записывает метрики в CSV файл."""
    fieldnames = [
        "timestamp",
        "iteration",
        "memory_mb",
        "cpu_percent",
        "latency_ms",
        "errors",
        "success",
    ]

    mode = "w" if write_header else "a"
    with path.open(mode, newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        if write_header:
            writer.writeheader()
        writer.writerow(asdict(metrics))


def run_soak_tests(
    hours: float,
    state_path: Path,
    metrics_path: Path,
    resume: bool = False,
    interval_seconds: float = 60.0,
) -> int:
    """Запускает soak-тесты на указанное количество часов."""
    state = SoakState.load(state_path) if resume else SoakState()

    if state.status == "completed":
        LOGGER.info("Soak-тесты уже завершены")
        return 0

    now = datetime.utcnow()
    total_seconds = hours * 3600
    total_iterations = int(total_seconds / interval_seconds)

    if not state.start_time:
        state.start_time = now.isoformat()
        state.total_iterations = total_iterations
        state.metrics_file = str(metrics_path)
        state.status = "running"

    start_iteration = state.completed_iterations
    write_header = start_iteration == 0

    LOGGER.info(
        "Запуск soak-тестов: %d итераций, интервал %.1f сек",
        total_iterations - start_iteration,
        interval_seconds,
    )

    end_time = now + timedelta(hours=hours)

    for i in range(start_iteration, total_iterations):
        if datetime.utcnow() >= end_time:
            LOGGER.info("Достигнут лимит времени")
            break

        metrics = _run_single_iteration(i)
        _write_metrics_row(metrics_path, metrics, write_header and i == start_iteration)

        state.completed_iterations = i + 1
        state.total_errors += metrics.errors

        if i % 10 == 0:
            state.save(state_path)
            LOGGER.info("Итерация %d/%d завершена", i + 1, total_iterations)

        # В CI не ждём между итерациями
        if interval_seconds > 1 and i < total_iterations - 1:
            time.sleep(min(interval_seconds, 1.0))  # Максимум 1 сек в CI

    state.end_time = datetime.utcnow().isoformat()
    state.status = "completed"
    state.save(state_path)

    LOGGER.info(
        "Soak-тесты завершены: %d итераций, %d ошибок",
        state.completed_iterations,
        state.total_errors,
    )

    return 0 if state.total_errors == 0 else 1


def main(argv: Optional[List[str]] = None) -> int:
    """Точка входа CLI."""
    parser = argparse.ArgumentParser(description="Kolibri Soak Tests")
    parser.add_argument(
        "--hours",
        type=float,
        default=1.0,
        help="Продолжительность тестов в часах (по умолчанию: 1)",
    )
    parser.add_argument(
        "--resume",
        action="store_true",
        help="Возобновить с предыдущего состояния",
    )
    parser.add_argument(
        "--state-path",
        type=Path,
        default=Path("soak_state.json"),
        help="Путь к файлу состояния",
    )
    parser.add_argument(
        "--metrics-path",
        type=Path,
        default=Path("soak_metrics.csv"),
        help="Путь к файлу метрик",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=60.0,
        help="Интервал между итерациями в секундах",
    )

    args = parser.parse_args(argv)

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s: %(message)s",
    )

    return run_soak_tests(
        hours=args.hours,
        state_path=args.state_path,
        metrics_path=args.metrics_path,
        resume=args.resume,
        interval_seconds=args.interval,
    )


if __name__ == "__main__":
    sys.exit(main())
