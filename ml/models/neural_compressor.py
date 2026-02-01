"""Neural compression model for data compression enhancement."""

from __future__ import annotations

import math
from typing import Any, Dict, List, Optional, Tuple

import numpy as np

from .base_model import BaseModel, ModelState


def _sigmoid(x: np.ndarray) -> np.ndarray:
    """Numerically stable sigmoid."""
    return np.where(x >= 0, 1 / (1 + np.exp(-x)), np.exp(x) / (1 + np.exp(x)))


def _tanh(x: np.ndarray) -> np.ndarray:
    """Tanh activation."""
    return np.tanh(x)


class NeuralCompressor(BaseModel):
    """Neural network for data compression enhancement.

    This model predicts the next byte/symbol in a sequence, enabling:
    - Improved compression ratios through better probability estimation
    - Adaptive encoding based on context
    - Integration with existing archiver for 20%+ compression improvement

    Architecture:
    - LSTM/GRU based context model
    - Byte-level predictions
    - Adaptive probability estimation
    """

    def __init__(
        self,
        context_size: int = 1024,
        hidden_size: int = 128,
        num_layers: int = 2,
        vocab_size: int = 256,  # Byte-level
        prediction_mode: str = "adaptive",
        device: str = "cpu",
    ) -> None:
        """Initialize NeuralCompressor.

        Args:
            context_size: Maximum context window size.
            hidden_size: Hidden state dimension.
            num_layers: Number of recurrent layers.
            vocab_size: Output vocabulary size (256 for bytes).
            prediction_mode: "adaptive" or "static".
            device: Target device.
        """
        super().__init__(name="NeuralCompressor", device=device)

        self.context_size = context_size
        self.hidden_size = hidden_size
        self.num_layers = num_layers
        self.vocab_size = vocab_size
        self.prediction_mode = prediction_mode

        self._init_parameters()
        self.state = ModelState.INITIALIZED

    def _init_parameters(self) -> None:
        """Initialize LSTM parameters."""
        # Input embedding
        scale = 1.0 / math.sqrt(self.hidden_size)
        self.set_parameter(
            "embedding",
            np.random.randn(self.vocab_size, self.hidden_size).astype(np.float32) * scale,
        )

        # LSTM layers
        for layer_idx in range(self.num_layers):
            input_size = self.hidden_size

            # LSTM gates: input, forget, cell, output
            # Combined weight matrices for efficiency
            lstm_scale = 1.0 / math.sqrt(input_size)
            self.set_parameter(
                f"lstm_{layer_idx}_ih",
                np.random.randn(input_size, 4 * self.hidden_size).astype(np.float32) * lstm_scale,
            )
            self.set_parameter(
                f"lstm_{layer_idx}_hh",
                np.random.randn(self.hidden_size, 4 * self.hidden_size).astype(np.float32) * lstm_scale,
            )
            self.set_parameter(
                f"lstm_{layer_idx}_bias",
                np.zeros(4 * self.hidden_size, dtype=np.float32),
            )

        # Output projection
        out_scale = 1.0 / math.sqrt(self.hidden_size)
        self.set_parameter(
            "output_proj",
            np.random.randn(self.hidden_size, self.vocab_size).astype(np.float32) * out_scale,
        )
        self.set_parameter(
            "output_bias",
            np.zeros(self.vocab_size, dtype=np.float32),
        )

    def _lstm_cell(
        self,
        x: np.ndarray,
        h: np.ndarray,
        c: np.ndarray,
        layer_idx: int,
    ) -> Tuple[np.ndarray, np.ndarray]:
        """Single LSTM cell computation.

        Args:
            x: Input [batch, input_size].
            h: Hidden state [batch, hidden_size].
            c: Cell state [batch, hidden_size].
            layer_idx: Layer index.

        Returns:
            New hidden state and cell state.
        """
        W_ih = self.get_parameter(f"lstm_{layer_idx}_ih")
        W_hh = self.get_parameter(f"lstm_{layer_idx}_hh")
        bias = self.get_parameter(f"lstm_{layer_idx}_bias")

        assert W_ih is not None and W_hh is not None and bias is not None

        # Combined gates computation
        gates = x @ W_ih + h @ W_hh + bias

        # Split gates
        i, f, g, o = np.split(gates, 4, axis=-1)

        # Gate activations
        i = _sigmoid(i)  # Input gate
        f = _sigmoid(f)  # Forget gate
        g = _tanh(g)  # Cell candidate
        o = _sigmoid(o)  # Output gate

        # New cell and hidden states
        new_c = f * c + i * g
        new_h = o * _tanh(new_c)

        return new_h, new_c

    def forward(
        self,
        input_ids: np.ndarray,
        hidden_state: Optional[Tuple[List[np.ndarray], List[np.ndarray]]] = None,
    ) -> Tuple[np.ndarray, Tuple[List[np.ndarray], List[np.ndarray]]]:
        """Forward pass through LSTM.

        Args:
            input_ids: Byte/token IDs [batch, seq_len].
            hidden_state: Optional initial hidden state.

        Returns:
            Tuple of (logits, new_hidden_state).
        """
        batch_size, seq_len = input_ids.shape

        # Initialize hidden states if not provided
        if hidden_state is None:
            h_list = [np.zeros((batch_size, self.hidden_size), dtype=np.float32) for _ in range(self.num_layers)]
            c_list = [np.zeros((batch_size, self.hidden_size), dtype=np.float32) for _ in range(self.num_layers)]
        else:
            h_list, c_list = hidden_state
            h_list = [h.copy() for h in h_list]
            c_list = [c.copy() for c in c_list]

        # Embedding lookup
        embedding = self.get_parameter("embedding")
        assert embedding is not None
        x = embedding[input_ids]  # [batch, seq_len, hidden_size]

        # Process sequence
        outputs = []
        for t in range(seq_len):
            layer_input = x[:, t, :]

            for layer_idx in range(self.num_layers):
                h_list[layer_idx], c_list[layer_idx] = self._lstm_cell(
                    layer_input,
                    h_list[layer_idx],
                    c_list[layer_idx],
                    layer_idx,
                )
                layer_input = h_list[layer_idx]

            outputs.append(layer_input)

        # Stack outputs
        output = np.stack(outputs, axis=1)  # [batch, seq_len, hidden_size]

        # Project to vocabulary
        W_out = self.get_parameter("output_proj")
        b_out = self.get_parameter("output_bias")
        assert W_out is not None and b_out is not None

        logits = output @ W_out + b_out  # [batch, seq_len, vocab_size]

        return logits, (h_list, c_list)

    def predict_next_byte(
        self,
        context: np.ndarray,
        hidden_state: Optional[Tuple[List[np.ndarray], List[np.ndarray]]] = None,
        temperature: float = 1.0,
    ) -> Tuple[np.ndarray, Tuple[List[np.ndarray], List[np.ndarray]]]:
        """Predict probability distribution for next byte.

        Args:
            context: Byte sequence [batch, seq_len].
            hidden_state: Optional hidden state for continuation.
            temperature: Sampling temperature.

        Returns:
            Tuple of (probabilities, new_hidden_state).
        """
        logits, new_state = self.forward(context, hidden_state)

        # Get last timestep prediction
        last_logits = logits[:, -1, :] / temperature

        # Convert to probabilities
        probs = np.exp(last_logits - np.max(last_logits, axis=-1, keepdims=True))
        probs = probs / np.sum(probs, axis=-1, keepdims=True)

        return probs, new_state

    def estimate_entropy(self, data: bytes, chunk_size: int = 512) -> float:
        """Estimate entropy of data using model predictions.

        Args:
            data: Input bytes.
            chunk_size: Processing chunk size.

        Returns:
            Estimated bits per byte.
        """
        if len(data) == 0:
            return 0.0

        # Convert to numpy array
        byte_array = np.frombuffer(data, dtype=np.uint8)
        total_log_prob = 0.0
        hidden_state = None

        for start in range(0, len(byte_array) - 1, chunk_size):
            end = min(start + chunk_size, len(byte_array) - 1)
            chunk = byte_array[start : end + 1].reshape(1, -1)

            # Get predictions
            logits, hidden_state = self.forward(chunk[:, :-1], hidden_state)

            # Compute log probabilities for actual next bytes
            targets = chunk[:, 1:]
            log_probs = logits - np.log(np.sum(np.exp(logits), axis=-1, keepdims=True))

            # Gather log probs for targets
            batch_indices = np.zeros((1, targets.shape[1]), dtype=np.int32)
            for i in range(targets.shape[1]):
                total_log_prob += log_probs[0, i, targets[0, i]]

        # Convert to bits per byte (handle edge case of very short data)
        divisor = max(len(byte_array) - 1, 1)
        bits_per_byte = -total_log_prob / divisor / np.log(2)
        return float(bits_per_byte)

    def compress_context(self, data: bytes) -> Dict[str, Any]:
        """Analyze data for compression optimization.

        Args:
            data: Input bytes.

        Returns:
            Dictionary with compression hints.
        """
        entropy = self.estimate_entropy(data)

        # Theoretical minimum compression ratio
        min_ratio = 8.0 / entropy if entropy > 0 else float("inf")

        return {
            "entropy_bits_per_byte": entropy,
            "theoretical_min_ratio": min_ratio,
            "recommended_algorithm": self._recommend_algorithm(entropy),
            "context_length": len(data),
        }

    def _recommend_algorithm(self, entropy: float) -> str:
        """Recommend compression algorithm based on entropy."""
        if entropy < 1.0:
            return "rle"  # Very repetitive data
        elif entropy < 3.0:
            return "dictionary"  # Moderate redundancy
        elif entropy < 6.0:
            return "hybrid"  # Mixed content
        else:
            return "arithmetic"  # High entropy, need sophisticated coding

    def get_input_shape(self) -> List[int]:
        """Return expected input shape."""
        return [self.context_size]

    def get_output_shape(self) -> List[int]:
        """Return expected output shape."""
        return [self.context_size, self.vocab_size]

    def get_config(self) -> Dict[str, Any]:
        """Return model configuration."""
        return {
            "context_size": self.context_size,
            "hidden_size": self.hidden_size,
            "num_layers": self.num_layers,
            "vocab_size": self.vocab_size,
            "prediction_mode": self.prediction_mode,
            "device": self.device,
        }

    @classmethod
    def from_config(cls, config: Dict[str, Any]) -> "NeuralCompressor":
        """Create model from config dictionary."""
        return cls(
            context_size=config.get("context_size", 1024),
            hidden_size=config.get("hidden_size", 128),
            num_layers=config.get("num_layers", 2),
            vocab_size=config.get("vocab_size", 256),
            prediction_mode=config.get("prediction_mode", "adaptive"),
            device=config.get("device", "cpu"),
        )
