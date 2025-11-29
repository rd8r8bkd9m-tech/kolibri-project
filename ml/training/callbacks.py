"""Training callbacks for logging, checkpointing, and early stopping."""

from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path
from typing import Any, Dict, Optional

from ..utils.logger import get_logger


class Callback(ABC):
    """Base class for training callbacks."""

    def on_train_start(self) -> None:
        """Called at the start of training."""
        pass

    def on_train_end(self) -> None:
        """Called at the end of training."""
        pass

    def on_epoch_start(self, epoch: int) -> None:
        """Called at the start of each epoch."""
        pass

    def on_epoch_end(self, epoch: int, metrics: Dict[str, float]) -> None:
        """Called at the end of each epoch."""
        pass

    def on_batch_start(self, batch_idx: int) -> None:
        """Called at the start of each batch."""
        pass

    def on_batch_end(self, batch_idx: int, metrics: Dict[str, float]) -> None:
        """Called at the end of each batch."""
        pass


class LoggingCallback(Callback):
    """Callback for logging training progress."""

    def __init__(self, log_every: int = 10) -> None:
        """Initialize logging callback.

        Args:
            log_every: Log metrics every N batches.
        """
        self.log_every = log_every
        self.logger = get_logger()
        self._batch_count = 0

    def on_train_start(self) -> None:
        self.logger.info("Training started")

    def on_train_end(self) -> None:
        self.logger.info("Training completed")

    def on_epoch_start(self, epoch: int) -> None:
        self.logger.info(f"Starting epoch {epoch}")
        self._batch_count = 0

    def on_epoch_end(self, epoch: int, metrics: Dict[str, float]) -> None:
        metrics_str = ", ".join(f"{k}={v:.4f}" for k, v in metrics.items())
        self.logger.info(f"Epoch {epoch} completed: {metrics_str}")

    def on_batch_end(self, batch_idx: int, metrics: Dict[str, float]) -> None:
        self._batch_count += 1
        if self._batch_count % self.log_every == 0:
            metrics_str = ", ".join(f"{k}={v:.4f}" for k, v in metrics.items())
            self.logger.debug(f"Batch {batch_idx}: {metrics_str}")


class CheckpointCallback(Callback):
    """Callback for saving model checkpoints."""

    def __init__(
        self,
        checkpoint_dir: Path | str,
        save_every_epoch: int = 1,
        save_best_only: bool = True,
        monitor: str = "loss",
        mode: str = "min",
    ) -> None:
        """Initialize checkpoint callback.

        Args:
            checkpoint_dir: Directory to save checkpoints.
            save_every_epoch: Save checkpoint every N epochs.
            save_best_only: Only save if metric improved.
            monitor: Metric to monitor for improvement.
            mode: "min" or "max" for metric comparison.
        """
        self.checkpoint_dir = Path(checkpoint_dir)
        self.save_every_epoch = save_every_epoch
        self.save_best_only = save_best_only
        self.monitor = monitor
        self.mode = mode
        self.best_value: Optional[float] = None
        self.logger = get_logger()
        self._trainer: Any = None

    def set_trainer(self, trainer: Any) -> None:
        """Set reference to trainer for saving."""
        self._trainer = trainer

    def _is_improvement(self, value: float) -> bool:
        """Check if value is an improvement."""
        if self.best_value is None:
            return True
        if self.mode == "min":
            return value < self.best_value
        return value > self.best_value

    def on_train_start(self) -> None:
        self.checkpoint_dir.mkdir(parents=True, exist_ok=True)

    def on_epoch_end(self, epoch: int, metrics: Dict[str, float]) -> None:
        if self._trainer is None:
            return

        should_save = epoch % self.save_every_epoch == 0
        current_value = metrics.get(self.monitor, 0.0)

        if self.save_best_only:
            if self._is_improvement(current_value):
                self.best_value = current_value
                self._trainer.save_checkpoint("best")
                self.logger.info(f"New best {self.monitor}: {current_value:.4f}")
        elif should_save:
            self._trainer.save_checkpoint(f"epoch_{epoch}")


class EarlyStoppingCallback(Callback):
    """Callback for early stopping when metric stops improving."""

    def __init__(
        self,
        monitor: str = "loss",
        patience: int = 5,
        min_delta: float = 0.0,
        mode: str = "min",
    ) -> None:
        """Initialize early stopping callback.

        Args:
            monitor: Metric to monitor.
            patience: Number of epochs with no improvement before stopping.
            min_delta: Minimum change to qualify as improvement.
            mode: "min" or "max" for metric comparison.
        """
        self.monitor = monitor
        self.patience = patience
        self.min_delta = min_delta
        self.mode = mode
        self.best_value: Optional[float] = None
        self.counter = 0
        self.should_stop = False
        self.logger = get_logger()

    def _is_improvement(self, value: float) -> bool:
        """Check if value is an improvement."""
        if self.best_value is None:
            return True
        if self.mode == "min":
            return value < (self.best_value - self.min_delta)
        return value > (self.best_value + self.min_delta)

    def on_epoch_end(self, epoch: int, metrics: Dict[str, float]) -> None:
        current_value = metrics.get(self.monitor)
        if current_value is None:
            return

        if self._is_improvement(current_value):
            self.best_value = current_value
            self.counter = 0
        else:
            self.counter += 1
            if self.counter >= self.patience:
                self.should_stop = True
                self.logger.info(
                    f"Early stopping triggered after {epoch} epochs. "
                    f"Best {self.monitor}: {self.best_value:.4f}"
                )


class LRSchedulerCallback(Callback):
    """Callback for learning rate scheduling."""

    def __init__(self, scheduler: Any) -> None:
        """Initialize LR scheduler callback.

        Args:
            scheduler: Learning rate scheduler instance.
        """
        self.scheduler = scheduler
        self.logger = get_logger()

    def on_batch_end(self, batch_idx: int, metrics: Dict[str, float]) -> None:
        self.scheduler.step()

    def on_epoch_end(self, epoch: int, metrics: Dict[str, float]) -> None:
        current_lr = self.scheduler.get_lr()
        self.logger.debug(f"Learning rate: {current_lr:.2e}")
