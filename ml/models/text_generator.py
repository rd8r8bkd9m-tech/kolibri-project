"""Text generation model for sequence generation tasks."""

from __future__ import annotations

import math
from typing import Any, Dict, List, Optional, Tuple

import numpy as np

from .base_model import BaseModel, ModelState


def _softmax(x: np.ndarray, axis: int = -1) -> np.ndarray:
    """Numerically stable softmax."""
    x_max = np.max(x, axis=axis, keepdims=True)
    exp_x = np.exp(x - x_max)
    return exp_x / np.sum(exp_x, axis=axis, keepdims=True)


class TextGenerator(BaseModel):
    """Text generation model using autoregressive decoding.

    Supports:
    - Greedy decoding
    - Top-k sampling
    - Top-p (nucleus) sampling
    - Temperature scaling
    """

    def __init__(
        self,
        vocab_size: int = 32000,
        hidden_size: int = 256,
        num_layers: int = 4,
        max_length: int = 512,
        device: str = "cpu",
    ) -> None:
        """Initialize TextGenerator.

        Args:
            vocab_size: Vocabulary size.
            hidden_size: Hidden state dimension.
            num_layers: Number of decoder layers.
            max_length: Maximum generation length.
            device: Target device.
        """
        super().__init__(name="TextGenerator", device=device)

        self.vocab_size = vocab_size
        self.hidden_size = hidden_size
        self.num_layers = num_layers
        self.max_length = max_length

        self._init_parameters()
        self.state = ModelState.INITIALIZED

    def _init_parameters(self) -> None:
        """Initialize decoder parameters."""
        scale = 1.0 / math.sqrt(self.hidden_size)

        # Token embedding
        self.set_parameter(
            "token_embedding",
            np.random.randn(self.vocab_size, self.hidden_size).astype(np.float32) * scale,
        )

        # Position embedding
        self.set_parameter(
            "position_embedding",
            np.random.randn(self.max_length, self.hidden_size).astype(np.float32) * 0.02,
        )

        # Decoder layers
        for layer_idx in range(self.num_layers):
            layer_scale = 1.0 / math.sqrt(self.hidden_size)
            self.set_parameter(
                f"decoder_{layer_idx}_w1",
                np.random.randn(self.hidden_size, self.hidden_size * 4).astype(np.float32) * layer_scale,
            )
            self.set_parameter(
                f"decoder_{layer_idx}_b1",
                np.zeros(self.hidden_size * 4, dtype=np.float32),
            )
            self.set_parameter(
                f"decoder_{layer_idx}_w2",
                np.random.randn(self.hidden_size * 4, self.hidden_size).astype(np.float32)
                * (1.0 / math.sqrt(self.hidden_size * 4)),
            )
            self.set_parameter(
                f"decoder_{layer_idx}_b2",
                np.zeros(self.hidden_size, dtype=np.float32),
            )

        # Output projection (tied with embedding)
        self.set_parameter(
            "output_proj",
            np.random.randn(self.hidden_size, self.vocab_size).astype(np.float32) * scale,
        )

    def _decoder_layer(self, x: np.ndarray, layer_idx: int) -> np.ndarray:
        """Single decoder layer with residual connection."""
        w1 = self.get_parameter(f"decoder_{layer_idx}_w1")
        b1 = self.get_parameter(f"decoder_{layer_idx}_b1")
        w2 = self.get_parameter(f"decoder_{layer_idx}_w2")
        b2 = self.get_parameter(f"decoder_{layer_idx}_b2")

        assert w1 is not None and b1 is not None
        assert w2 is not None and b2 is not None

        hidden = np.maximum(0, x @ w1 + b1)
        output = hidden @ w2 + b2
        return x + output

    def forward(self, input_ids: np.ndarray) -> np.ndarray:
        """Forward pass to compute logits.

        Args:
            input_ids: Token IDs [batch, seq_len].

        Returns:
            Logits [batch, seq_len, vocab_size].
        """
        batch_size, seq_len = input_ids.shape
        seq_len = min(seq_len, self.max_length)

        # Embedding lookup
        token_emb = self.get_parameter("token_embedding")
        pos_emb = self.get_parameter("position_embedding")
        assert token_emb is not None and pos_emb is not None

        x = token_emb[input_ids[:, :seq_len]] + pos_emb[:seq_len]

        # Decoder layers
        for layer_idx in range(self.num_layers):
            x = self._decoder_layer(x, layer_idx)

        # Output projection
        W_out = self.get_parameter("output_proj")
        assert W_out is not None

        logits = x @ W_out

        return logits

    def generate(
        self,
        input_ids: np.ndarray,
        max_new_tokens: int = 100,
        temperature: float = 1.0,
        top_k: Optional[int] = None,
        top_p: Optional[float] = None,
        eos_token_id: Optional[int] = None,
    ) -> np.ndarray:
        """Generate text autoregressively.

        Args:
            input_ids: Starting token IDs [1, seq_len].
            max_new_tokens: Maximum number of tokens to generate.
            temperature: Sampling temperature.
            top_k: Top-k sampling parameter.
            top_p: Top-p (nucleus) sampling parameter.
            eos_token_id: End of sequence token ID.

        Returns:
            Generated token IDs [1, seq_len + generated].
        """
        generated = input_ids.copy()

        for _ in range(max_new_tokens):
            if generated.shape[1] >= self.max_length:
                break

            # Get logits for last position
            logits = self.forward(generated)
            next_logits = logits[:, -1, :] / temperature

            # Apply top-k filtering
            if top_k is not None:
                indices_to_remove = np.argsort(next_logits, axis=-1)[:, :-top_k]
                for batch_idx in range(next_logits.shape[0]):
                    next_logits[batch_idx, indices_to_remove[batch_idx]] = float("-inf")

            # Apply top-p (nucleus) filtering
            if top_p is not None:
                sorted_indices = np.argsort(next_logits, axis=-1)[:, ::-1]
                sorted_logits = np.take_along_axis(next_logits, sorted_indices, axis=-1)
                cumulative_probs = np.cumsum(_softmax(sorted_logits), axis=-1)
                sorted_indices_to_remove = cumulative_probs > top_p
                sorted_indices_to_remove[:, 1:] = sorted_indices_to_remove[:, :-1]
                sorted_indices_to_remove[:, 0] = False
                for batch_idx in range(next_logits.shape[0]):
                    indices_to_remove = sorted_indices[batch_idx, sorted_indices_to_remove[batch_idx]]
                    next_logits[batch_idx, indices_to_remove] = float("-inf")

            # Sample from distribution
            probs = _softmax(next_logits)
            next_token = np.array(
                [[np.random.choice(self.vocab_size, p=probs[0])]], dtype=np.int64
            )

            generated = np.concatenate([generated, next_token], axis=1)

            # Check for EOS
            if eos_token_id is not None and next_token[0, 0] == eos_token_id:
                break

        return generated

    def get_input_shape(self) -> List[int]:
        """Return expected input shape."""
        return [self.max_length]

    def get_output_shape(self) -> List[int]:
        """Return expected output shape."""
        return [self.max_length, self.vocab_size]

    def get_config(self) -> Dict[str, Any]:
        """Return model configuration."""
        return {
            "vocab_size": self.vocab_size,
            "hidden_size": self.hidden_size,
            "num_layers": self.num_layers,
            "max_length": self.max_length,
            "device": self.device,
        }

    @classmethod
    def from_config(cls, config: Dict[str, Any]) -> "TextGenerator":
        """Create model from config dictionary."""
        return cls(
            vocab_size=config.get("vocab_size", 32000),
            hidden_size=config.get("hidden_size", 256),
            num_layers=config.get("num_layers", 4),
            max_length=config.get("max_length", 512),
            device=config.get("device", "cpu"),
        )
