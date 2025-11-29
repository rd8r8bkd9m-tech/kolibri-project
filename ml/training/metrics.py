"""Training metrics for Kolibri ML models."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

import numpy as np


@dataclass
class Metrics:
    """Container for training and evaluation metrics."""

    loss: float = 0.0
    accuracy: float = 0.0
    precision: float = 0.0
    recall: float = 0.0
    f1: float = 0.0
    compression_ratio: float = 0.0
    throughput: float = 0.0
    extra: Dict[str, float] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, float]:
        """Convert to dictionary."""
        result = {
            "loss": self.loss,
            "accuracy": self.accuracy,
            "precision": self.precision,
            "recall": self.recall,
            "f1": self.f1,
            "compression_ratio": self.compression_ratio,
            "throughput": self.throughput,
        }
        result.update(self.extra)
        return result


def compute_loss(
    predictions: np.ndarray,
    targets: np.ndarray,
    loss_type: str = "cross_entropy",
) -> float:
    """Compute loss between predictions and targets.

    Args:
        predictions: Model predictions (logits or probabilities).
        targets: Ground truth targets.
        loss_type: Type of loss ("cross_entropy", "mse", "mae").

    Returns:
        Scalar loss value.
    """
    if loss_type == "cross_entropy":
        return _cross_entropy_loss(predictions, targets)
    elif loss_type == "mse":
        return _mse_loss(predictions, targets)
    elif loss_type == "mae":
        return _mae_loss(predictions, targets)
    else:
        raise ValueError(f"Unknown loss type: {loss_type}")


def _cross_entropy_loss(logits: np.ndarray, targets: np.ndarray) -> float:
    """Cross-entropy loss for classification."""
    # Handle different target shapes
    if targets.ndim == logits.ndim:
        # Soft targets (probabilities)
        log_probs = logits - np.log(np.sum(np.exp(logits), axis=-1, keepdims=True))
        return float(-np.mean(np.sum(targets * log_probs, axis=-1)))
    else:
        # Hard targets (indices)
        # Compute log softmax
        max_logits = np.max(logits, axis=-1, keepdims=True)
        log_probs = logits - max_logits - np.log(np.sum(np.exp(logits - max_logits), axis=-1, keepdims=True))

        # Gather log probs for targets
        batch_size = targets.shape[0]
        if logits.ndim == 3:
            # Sequence classification [batch, seq, vocab]
            seq_len = targets.shape[1]
            total_loss = 0.0
            for b in range(batch_size):
                for s in range(seq_len):
                    total_loss -= log_probs[b, s, targets[b, s]]
            return float(total_loss / (batch_size * seq_len))
        else:
            # Simple classification [batch, classes]
            total_loss = 0.0
            for b in range(batch_size):
                total_loss -= log_probs[b, targets[b]]
            return float(total_loss / batch_size)


def _mse_loss(predictions: np.ndarray, targets: np.ndarray) -> float:
    """Mean squared error loss."""
    return float(np.mean((predictions - targets) ** 2))


def _mae_loss(predictions: np.ndarray, targets: np.ndarray) -> float:
    """Mean absolute error loss."""
    return float(np.mean(np.abs(predictions - targets)))


def compute_accuracy(
    predictions: np.ndarray,
    targets: np.ndarray,
    top_k: int = 1,
) -> float:
    """Compute classification accuracy.

    Args:
        predictions: Model predictions (logits or probabilities).
        targets: Ground truth labels.
        top_k: Consider correct if target in top-k predictions.

    Returns:
        Accuracy as float between 0 and 1.
    """
    if predictions.ndim == 1:
        # Binary predictions
        pred_classes = (predictions > 0.5).astype(np.int32)
        return float(np.mean(pred_classes == targets))

    if top_k == 1:
        pred_classes = np.argmax(predictions, axis=-1)
        if targets.ndim == predictions.ndim:
            targets = np.argmax(targets, axis=-1)
        return float(np.mean(pred_classes == targets))

    # Top-k accuracy
    top_k_preds = np.argsort(predictions, axis=-1)[:, -top_k:]
    correct = 0
    for i, target in enumerate(targets):
        if target in top_k_preds[i]:
            correct += 1
    return correct / len(targets)


def compute_precision_recall_f1(
    predictions: np.ndarray,
    targets: np.ndarray,
    threshold: float = 0.5,
) -> Dict[str, float]:
    """Compute precision, recall, and F1 score.

    Args:
        predictions: Model predictions.
        targets: Ground truth labels.
        threshold: Classification threshold.

    Returns:
        Dictionary with precision, recall, and f1.
    """
    pred_classes = (predictions > threshold).astype(np.int32)
    if pred_classes.ndim > 1:
        pred_classes = np.argmax(predictions, axis=-1)

    true_positives = np.sum((pred_classes == 1) & (targets == 1))
    false_positives = np.sum((pred_classes == 1) & (targets == 0))
    false_negatives = np.sum((pred_classes == 0) & (targets == 1))

    precision = true_positives / (true_positives + false_positives + 1e-8)
    recall = true_positives / (true_positives + false_negatives + 1e-8)
    f1 = 2 * precision * recall / (precision + recall + 1e-8)

    return {
        "precision": float(precision),
        "recall": float(recall),
        "f1": float(f1),
    }


def compute_compression_ratio(original_size: int, compressed_size: int) -> float:
    """Compute compression ratio.

    Args:
        original_size: Original data size in bytes.
        compressed_size: Compressed data size in bytes.

    Returns:
        Compression ratio (higher is better).
    """
    if compressed_size <= 0:
        return float("inf")
    return original_size / compressed_size


def compute_bits_per_byte(log_probs: np.ndarray) -> float:
    """Compute bits per byte for compression evaluation.

    Args:
        log_probs: Log probabilities from model predictions.

    Returns:
        Average bits per byte.
    """
    return float(-np.mean(log_probs) / np.log(2))


class MetricTracker:
    """Tracks metrics over training steps."""

    def __init__(self, window_size: int = 100) -> None:
        """Initialize metric tracker.

        Args:
            window_size: Size of moving average window.
        """
        self.window_size = window_size
        self._history: Dict[str, List[float]] = {}

    def update(self, metrics: Dict[str, float]) -> None:
        """Update metrics with new values."""
        for name, value in metrics.items():
            if name not in self._history:
                self._history[name] = []
            self._history[name].append(value)

    def get_average(self, metric_name: str) -> float:
        """Get moving average for a metric."""
        if metric_name not in self._history:
            return 0.0
        values = self._history[metric_name][-self.window_size :]
        return float(np.mean(values)) if values else 0.0

    def get_latest(self, metric_name: str) -> float:
        """Get latest value for a metric."""
        if metric_name not in self._history or not self._history[metric_name]:
            return 0.0
        return self._history[metric_name][-1]

    def get_history(self, metric_name: str) -> List[float]:
        """Get full history for a metric."""
        return self._history.get(metric_name, [])

    def reset(self) -> None:
        """Clear all metrics."""
        self._history.clear()
