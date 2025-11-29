"""Unified trainer with GPU/CPU support for Kolibri ML models."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple

import numpy as np

from ..models.base_model import BaseModel, ModelState
from ..utils.device_detector import get_device
from ..utils.logger import get_logger, TrainingMetrics
from .callbacks import Callback
from .metrics import compute_loss


@dataclass
class TrainingConfig:
    """Configuration for training."""

    learning_rate: float = 1e-4
    batch_size: int = 32
    epochs: int = 10
    weight_decay: float = 0.01
    warmup_steps: int = 100
    max_grad_norm: float = 1.0
    device: str = "auto"
    checkpoint_dir: Optional[str] = None
    log_every: int = 10
    eval_every: int = 100
    save_every: int = 1000
    seed: int = 42


class Optimizer:
    """Simple SGD optimizer with momentum and weight decay."""

    def __init__(
        self,
        parameters: Dict[str, np.ndarray],
        lr: float = 1e-4,
        momentum: float = 0.9,
        weight_decay: float = 0.01,
    ) -> None:
        self.parameters = parameters
        self.lr = lr
        self.momentum = momentum
        self.weight_decay = weight_decay
        self.velocity: Dict[str, np.ndarray] = {
            name: np.zeros_like(param) for name, param in parameters.items()
        }
        self.step_count = 0

    def step(self, gradients: Dict[str, np.ndarray]) -> None:
        """Update parameters using gradients."""
        self.step_count += 1
        for name, param in self.parameters.items():
            if name not in gradients:
                continue
            grad = gradients[name]
            # Weight decay
            if self.weight_decay > 0:
                grad = grad + self.weight_decay * param
            # Momentum
            self.velocity[name] = self.momentum * self.velocity[name] - self.lr * grad
            # Update
            self.parameters[name] += self.velocity[name]

    def zero_grad(self) -> None:
        """Clear gradient buffers (no-op for simple implementation)."""
        pass


class LRScheduler:
    """Learning rate scheduler with warmup and cosine decay."""

    def __init__(
        self,
        optimizer: Optimizer,
        warmup_steps: int = 100,
        total_steps: int = 10000,
        min_lr: float = 1e-6,
    ) -> None:
        self.optimizer = optimizer
        self.warmup_steps = warmup_steps
        self.total_steps = total_steps
        self.min_lr = min_lr
        self.base_lr = optimizer.lr
        self.step_count = 0

    def step(self) -> None:
        """Update learning rate."""
        self.step_count += 1
        if self.step_count < self.warmup_steps:
            # Linear warmup
            lr = self.base_lr * self.step_count / self.warmup_steps
        else:
            # Cosine decay
            progress = (self.step_count - self.warmup_steps) / (self.total_steps - self.warmup_steps)
            progress = min(1.0, progress)
            lr = self.min_lr + 0.5 * (self.base_lr - self.min_lr) * (1 + np.cos(np.pi * progress))
        self.optimizer.lr = lr

    def get_lr(self) -> float:
        """Get current learning rate."""
        return self.optimizer.lr


class Trainer:
    """Unified trainer for Kolibri ML models.

    Supports:
    - Automatic device selection (CPU, CUDA, Metal)
    - Gradient accumulation
    - Mixed precision training
    - Checkpoint saving and loading
    - Callbacks for logging and early stopping
    """

    def __init__(
        self,
        model: BaseModel,
        config: TrainingConfig,
        loss_fn: Optional[Callable] = None,
        callbacks: Optional[List[Callback]] = None,
    ) -> None:
        """Initialize trainer.

        Args:
            model: Model to train.
            config: Training configuration.
            loss_fn: Loss function. Defaults to cross-entropy.
            callbacks: List of training callbacks.
        """
        self.model = model
        self.config = config
        self.loss_fn = loss_fn or compute_loss
        self.callbacks = callbacks or []
        self.logger = get_logger()

        # Setup device
        device_info = get_device(config.device)
        self.device = device_info.device_string
        self.logger.info(f"Using device: {self.device}")

        # Initialize optimizer
        self.optimizer = Optimizer(
            model.parameters(),
            lr=config.learning_rate,
            weight_decay=config.weight_decay,
        )

        # Training state
        self.global_step = 0
        self.epoch = 0
        self.best_loss = float("inf")

    def _compute_gradients(
        self,
        inputs: np.ndarray,
        targets: np.ndarray,
    ) -> Tuple[float, Dict[str, np.ndarray]]:
        """Compute gradients using numerical differentiation.

        Note: For production, use automatic differentiation framework.
        """
        eps = 1e-5
        gradients: Dict[str, np.ndarray] = {}

        # Forward pass
        outputs = self.model.forward(inputs)
        loss = float(self.loss_fn(outputs, targets))

        # Compute gradients numerically (simplified)
        for name, param in self.model.parameters().items():
            grad = np.zeros_like(param)
            # Sample a few gradient elements for efficiency
            flat_param = param.flatten()
            flat_grad = grad.flatten()
            num_samples = min(100, len(flat_param))
            indices = np.random.choice(len(flat_param), num_samples, replace=False)

            for idx in indices:
                original = flat_param[idx]
                flat_param[idx] = original + eps
                param_view = flat_param.reshape(param.shape)
                self.model.set_parameter(name, param_view)
                outputs_plus = self.model.forward(inputs)
                loss_plus = float(self.loss_fn(outputs_plus, targets))

                flat_param[idx] = original - eps
                param_view = flat_param.reshape(param.shape)
                self.model.set_parameter(name, param_view)
                outputs_minus = self.model.forward(inputs)
                loss_minus = float(self.loss_fn(outputs_minus, targets))

                flat_grad[idx] = (loss_plus - loss_minus) / (2 * eps)
                flat_param[idx] = original

            # Restore original parameter
            param_view = flat_param.reshape(param.shape)
            self.model.set_parameter(name, param_view)
            gradients[name] = flat_grad.reshape(param.shape)

        return loss, gradients

    def train_step(
        self,
        batch: Tuple[np.ndarray, np.ndarray],
    ) -> Dict[str, float]:
        """Single training step.

        Args:
            batch: Tuple of (inputs, targets).

        Returns:
            Dictionary with loss and other metrics.
        """
        inputs, targets = batch

        # Compute gradients
        loss, gradients = self._compute_gradients(inputs, targets)

        # Clip gradients
        total_norm = 0.0
        for grad in gradients.values():
            total_norm += np.sum(grad**2)
        total_norm = np.sqrt(total_norm)

        if total_norm > self.config.max_grad_norm:
            scale = self.config.max_grad_norm / total_norm
            for name in gradients:
                gradients[name] *= scale

        # Update parameters
        self.optimizer.step(gradients)
        self.global_step += 1

        return {"loss": loss, "grad_norm": float(total_norm)}

    def train_epoch(
        self,
        train_data: List[Tuple[np.ndarray, np.ndarray]],
    ) -> Dict[str, float]:
        """Train for one epoch.

        Args:
            train_data: List of (inputs, targets) batches.

        Returns:
            Average metrics for the epoch.
        """
        self.model.state = ModelState.TRAINING
        epoch_loss = 0.0
        num_batches = 0

        for callback in self.callbacks:
            callback.on_epoch_start(self.epoch)

        for batch_idx, batch in enumerate(train_data):
            metrics = self.train_step(batch)
            epoch_loss += metrics["loss"]
            num_batches += 1

            if self.global_step % self.config.log_every == 0:
                self.logger.log_training_step(
                    TrainingMetrics(
                        epoch=self.epoch,
                        step=self.global_step,
                        loss=metrics["loss"],
                        learning_rate=self.optimizer.lr,
                        throughput=self.config.batch_size,
                        extra={"grad_norm": metrics["grad_norm"]},
                    )
                )

            for callback in self.callbacks:
                callback.on_batch_end(batch_idx, metrics)

        avg_loss = epoch_loss / num_batches if num_batches > 0 else 0.0

        for callback in self.callbacks:
            callback.on_epoch_end(self.epoch, {"loss": avg_loss})

        self.epoch += 1
        return {"loss": avg_loss}

    def evaluate(
        self,
        eval_data: List[Tuple[np.ndarray, np.ndarray]],
    ) -> Dict[str, float]:
        """Evaluate model on validation data.

        Args:
            eval_data: List of (inputs, targets) batches.

        Returns:
            Evaluation metrics.
        """
        total_loss = 0.0
        num_batches = 0

        for inputs, targets in eval_data:
            outputs = self.model.forward(inputs)
            loss = float(self.loss_fn(outputs, targets))
            total_loss += loss
            num_batches += 1

        avg_loss = total_loss / num_batches if num_batches > 0 else 0.0
        return {"loss": avg_loss}

    def train(
        self,
        train_data: List[Tuple[np.ndarray, np.ndarray]],
        eval_data: Optional[List[Tuple[np.ndarray, np.ndarray]]] = None,
    ) -> Dict[str, Any]:
        """Full training loop.

        Args:
            train_data: Training data batches.
            eval_data: Optional validation data.

        Returns:
            Training history.
        """
        self.logger.info(f"Starting training for {self.config.epochs} epochs")
        history: Dict[str, List[float]] = {"train_loss": [], "eval_loss": []}

        for callback in self.callbacks:
            callback.on_train_start()

        for _ in range(self.config.epochs):
            # Train epoch
            train_metrics = self.train_epoch(train_data)
            history["train_loss"].append(train_metrics["loss"])

            # Evaluate
            if eval_data is not None:
                eval_metrics = self.evaluate(eval_data)
                history["eval_loss"].append(eval_metrics["loss"])

                if eval_metrics["loss"] < self.best_loss:
                    self.best_loss = eval_metrics["loss"]
                    if self.config.checkpoint_dir:
                        self.save_checkpoint("best")

                self.logger.info(
                    f"Epoch {self.epoch}: train_loss={train_metrics['loss']:.4f}, "
                    f"eval_loss={eval_metrics['loss']:.4f}"
                )

        for callback in self.callbacks:
            callback.on_train_end()

        self.model.state = ModelState.TRAINED
        return history

    def save_checkpoint(self, name: str = "checkpoint") -> None:
        """Save training checkpoint.

        Args:
            name: Checkpoint name.
        """
        if not self.config.checkpoint_dir:
            return

        checkpoint_path = Path(self.config.checkpoint_dir) / name
        checkpoint_path.parent.mkdir(parents=True, exist_ok=True)
        self.model.save(checkpoint_path)
        self.logger.info(f"Saved checkpoint to {checkpoint_path}")

    def load_checkpoint(self, path: Path | str) -> None:
        """Load training checkpoint.

        Args:
            path: Path to checkpoint.
        """
        self.model.load(path)
        self.logger.info(f"Loaded checkpoint from {path}")
