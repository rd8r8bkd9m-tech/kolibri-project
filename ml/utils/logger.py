"""ML-specific logging utilities for Kolibri ML system."""

from __future__ import annotations

import logging
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional


@dataclass
class TrainingMetrics:
    """Container for training metrics."""

    epoch: int = 0
    step: int = 0
    loss: float = 0.0
    learning_rate: float = 0.0
    throughput: float = 0.0  # samples/sec
    extra: Dict[str, float] = field(default_factory=dict)


class MLLogger:
    """Structured logger for ML training and inference."""

    def __init__(
        self,
        name: str = "kolibri.ml",
        level: int = logging.INFO,
        log_file: Optional[Path | str] = None,
    ) -> None:
        """Initialize ML logger.

        Args:
            name: Logger name.
            level: Logging level.
            log_file: Optional file path for log output.
        """
        self.logger = logging.getLogger(name)
        self.logger.setLevel(level)

        if not self.logger.handlers:
            # Console handler
            console_handler = logging.StreamHandler(sys.stdout)
            console_handler.setLevel(level)
            formatter = logging.Formatter(
                "%(asctime)s - %(name)s - %(levelname)s - %(message)s",
                datefmt="%Y-%m-%d %H:%M:%S",
            )
            console_handler.setFormatter(formatter)
            self.logger.addHandler(console_handler)

            # File handler if specified
            if log_file:
                log_path = Path(log_file)
                log_path.parent.mkdir(parents=True, exist_ok=True)
                file_handler = logging.FileHandler(log_path, encoding="utf-8")
                file_handler.setLevel(level)
                file_handler.setFormatter(formatter)
                self.logger.addHandler(file_handler)

        self._training_history: List[TrainingMetrics] = []
        self._start_time: Optional[float] = None

    def info(self, msg: str, **kwargs: Any) -> None:
        """Log info message with optional structured data."""
        if kwargs:
            extra_str = " ".join(f"{k}={v}" for k, v in kwargs.items())
            msg = f"{msg} [{extra_str}]"
        self.logger.info(msg)

    def debug(self, msg: str, **kwargs: Any) -> None:
        """Log debug message with optional structured data."""
        if kwargs:
            extra_str = " ".join(f"{k}={v}" for k, v in kwargs.items())
            msg = f"{msg} [{extra_str}]"
        self.logger.debug(msg)

    def warning(self, msg: str, **kwargs: Any) -> None:
        """Log warning message with optional structured data."""
        if kwargs:
            extra_str = " ".join(f"{k}={v}" for k, v in kwargs.items())
            msg = f"{msg} [{extra_str}]"
        self.logger.warning(msg)

    def error(self, msg: str, **kwargs: Any) -> None:
        """Log error message with optional structured data."""
        if kwargs:
            extra_str = " ".join(f"{k}={v}" for k, v in kwargs.items())
            msg = f"{msg} [{extra_str}]"
        self.logger.error(msg)

    def log_training_step(self, metrics: TrainingMetrics) -> None:
        """Log training metrics for a single step."""
        self._training_history.append(metrics)
        extra_str = " ".join(f"{k}={v:.4f}" for k, v in metrics.extra.items())
        self.logger.info(
            f"Epoch {metrics.epoch} Step {metrics.step}: "
            f"loss={metrics.loss:.4f} lr={metrics.learning_rate:.2e} "
            f"throughput={metrics.throughput:.1f} samples/sec "
            f"{extra_str}"
        )

    def log_inference(
        self,
        model_name: str,
        batch_size: int,
        latency_ms: float,
        device: str,
    ) -> None:
        """Log inference metrics."""
        throughput = (batch_size / latency_ms) * 1000 if latency_ms > 0 else 0
        self.logger.info(
            f"Inference [{model_name}]: batch={batch_size} latency={latency_ms:.2f}ms "
            f"throughput={throughput:.1f} samples/sec device={device}"
        )

    def start_timer(self) -> None:
        """Start timing for duration tracking."""
        self._start_time = time.perf_counter()

    def elapsed_time(self) -> float:
        """Get elapsed time since start_timer in seconds."""
        if self._start_time is None:
            return 0.0
        return time.perf_counter() - self._start_time

    def get_training_history(self) -> List[TrainingMetrics]:
        """Return all logged training metrics."""
        return self._training_history.copy()


_default_logger: Optional[MLLogger] = None


def get_logger(name: str = "kolibri.ml") -> MLLogger:
    """Get or create the default ML logger.

    Args:
        name: Logger name (ignored if default already exists).

    Returns:
        MLLogger instance.
    """
    global _default_logger
    if _default_logger is None:
        _default_logger = MLLogger(name)
    return _default_logger
