"""Universal classifier model for various classification tasks."""

from __future__ import annotations

import math
from typing import Any, Dict, List, Optional

import numpy as np

from .base_model import BaseModel, ModelState


def _softmax(x: np.ndarray, axis: int = -1) -> np.ndarray:
    """Numerically stable softmax."""
    x_max = np.max(x, axis=axis, keepdims=True)
    exp_x = np.exp(x - x_max)
    return exp_x / np.sum(exp_x, axis=axis, keepdims=True)


class Classifier(BaseModel):
    """Universal classifier for text and sequence classification.

    Supports:
    - Binary and multi-class classification
    - Multi-label classification
    - Sequence classification
    """

    def __init__(
        self,
        input_dim: int = 256,
        hidden_dims: Optional[List[int]] = None,
        num_classes: int = 2,
        multi_label: bool = False,
        dropout: float = 0.1,
        device: str = "cpu",
    ) -> None:
        """Initialize Classifier.

        Args:
            input_dim: Input feature dimension.
            hidden_dims: List of hidden layer dimensions.
            num_classes: Number of output classes.
            multi_label: Whether this is multi-label classification.
            dropout: Dropout probability.
            device: Target device.
        """
        super().__init__(name="Classifier", device=device)

        self.input_dim = input_dim
        self.hidden_dims = hidden_dims or [128, 64]
        self.num_classes = num_classes
        self.multi_label = multi_label
        self.dropout = dropout

        self._init_parameters()
        self.state = ModelState.INITIALIZED

    def _init_parameters(self) -> None:
        """Initialize classifier parameters."""
        dims = [self.input_dim] + self.hidden_dims + [self.num_classes]

        for i in range(len(dims) - 1):
            scale = 1.0 / math.sqrt(dims[i])
            self.set_parameter(
                f"layer_{i}_weight",
                np.random.randn(dims[i], dims[i + 1]).astype(np.float32) * scale,
            )
            self.set_parameter(
                f"layer_{i}_bias",
                np.zeros(dims[i + 1], dtype=np.float32),
            )

    def forward(self, x: np.ndarray) -> np.ndarray:
        """Forward pass through classifier.

        Args:
            x: Input features [batch, input_dim].

        Returns:
            Logits [batch, num_classes].
        """
        num_layers = len(self.hidden_dims) + 1

        for i in range(num_layers):
            W = self.get_parameter(f"layer_{i}_weight")
            b = self.get_parameter(f"layer_{i}_bias")
            assert W is not None and b is not None

            x = x @ W + b

            # Apply ReLU for hidden layers
            if i < num_layers - 1:
                x = np.maximum(0, x)

        return x

    def predict(self, x: np.ndarray) -> np.ndarray:
        """Get class predictions.

        Args:
            x: Input features [batch, input_dim].

        Returns:
            Predicted classes [batch] or probabilities [batch, num_classes].
        """
        logits = self.forward(x)

        if self.multi_label:
            # Sigmoid for multi-label
            return 1.0 / (1.0 + np.exp(-logits))
        else:
            # Softmax for multi-class
            return _softmax(logits)

    def predict_classes(self, x: np.ndarray, threshold: float = 0.5) -> np.ndarray:
        """Get predicted class labels.

        Args:
            x: Input features.
            threshold: Threshold for multi-label classification.

        Returns:
            Predicted class indices.
        """
        probs = self.predict(x)

        if self.multi_label:
            return (probs > threshold).astype(np.int32)
        else:
            return np.argmax(probs, axis=-1)

    def get_input_shape(self) -> List[int]:
        """Return expected input shape."""
        return [self.input_dim]

    def get_output_shape(self) -> List[int]:
        """Return expected output shape."""
        return [self.num_classes]

    def get_config(self) -> Dict[str, Any]:
        """Return model configuration."""
        return {
            "input_dim": self.input_dim,
            "hidden_dims": self.hidden_dims,
            "num_classes": self.num_classes,
            "multi_label": self.multi_label,
            "dropout": self.dropout,
            "device": self.device,
        }

    @classmethod
    def from_config(cls, config: Dict[str, Any]) -> "Classifier":
        """Create model from config dictionary."""
        return cls(
            input_dim=config.get("input_dim", 256),
            hidden_dims=config.get("hidden_dims", [128, 64]),
            num_classes=config.get("num_classes", 2),
            multi_label=config.get("multi_label", False),
            dropout=config.get("dropout", 0.1),
            device=config.get("device", "cpu"),
        )
