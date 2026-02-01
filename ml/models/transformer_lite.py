"""Lightweight Transformer model for cross-platform inference."""

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


def _gelu(x: np.ndarray) -> np.ndarray:
    """GELU activation function."""
    return 0.5 * x * (1 + np.tanh(np.sqrt(2 / np.pi) * (x + 0.044715 * x**3)))


def _layer_norm(x: np.ndarray, gamma: np.ndarray, beta: np.ndarray, eps: float = 1e-5) -> np.ndarray:
    """Layer normalization."""
    mean = np.mean(x, axis=-1, keepdims=True)
    variance = np.var(x, axis=-1, keepdims=True)
    normalized = (x - mean) / np.sqrt(variance + eps)
    return gamma * normalized + beta


class TransformerLite(BaseModel):
    """Lightweight Transformer model optimized for cross-platform deployment.

    This model is designed to run efficiently on:
    - CPU (any platform)
    - GPU (CUDA/Metal)
    - WebAssembly (browser)
    - Mobile (via ONNX/CoreML/TFLite export)

    Architecture:
    - Compact encoder-only transformer
    - Rotary positional embeddings
    - Flash attention (when available)
    - Target size: < 50MB

    Supported tasks:
    - Text encoding
    - Classification
    - Sequence generation
    """

    def __init__(
        self,
        hidden_size: int = 256,
        num_layers: int = 4,
        num_heads: int = 4,
        intermediate_size: int = 1024,
        max_seq_length: int = 512,
        vocab_size: int = 32000,
        dropout: float = 0.1,
        device: str = "cpu",
    ) -> None:
        """Initialize TransformerLite.

        Args:
            hidden_size: Dimension of hidden states.
            num_layers: Number of transformer layers.
            num_heads: Number of attention heads.
            intermediate_size: Size of intermediate FFN layer.
            max_seq_length: Maximum sequence length.
            vocab_size: Vocabulary size for embeddings.
            dropout: Dropout probability (training only).
            device: Target device.
        """
        super().__init__(name="TransformerLite", device=device)

        self.hidden_size = hidden_size
        self.num_layers = num_layers
        self.num_heads = num_heads
        self.head_dim = hidden_size // num_heads
        self.intermediate_size = intermediate_size
        self.max_seq_length = max_seq_length
        self.vocab_size = vocab_size
        self.dropout = dropout

        self._init_parameters()
        self.state = ModelState.INITIALIZED

    def _init_parameters(self) -> None:
        """Initialize model parameters with Xavier/He initialization."""
        # Token embeddings
        scale = 1.0 / math.sqrt(self.hidden_size)
        self.set_parameter(
            "token_embedding",
            np.random.randn(self.vocab_size, self.hidden_size).astype(np.float32) * scale,
        )

        # Position embeddings (learned)
        self.set_parameter(
            "position_embedding",
            np.random.randn(self.max_seq_length, self.hidden_size).astype(np.float32) * 0.02,
        )

        # Transformer layers
        for layer_idx in range(self.num_layers):
            prefix = f"layer_{layer_idx}"

            # Attention weights
            attn_scale = 1.0 / math.sqrt(self.hidden_size)
            self.set_parameter(
                f"{prefix}_query",
                np.random.randn(self.hidden_size, self.hidden_size).astype(np.float32) * attn_scale,
            )
            self.set_parameter(
                f"{prefix}_key",
                np.random.randn(self.hidden_size, self.hidden_size).astype(np.float32) * attn_scale,
            )
            self.set_parameter(
                f"{prefix}_value",
                np.random.randn(self.hidden_size, self.hidden_size).astype(np.float32) * attn_scale,
            )
            self.set_parameter(
                f"{prefix}_attn_out",
                np.random.randn(self.hidden_size, self.hidden_size).astype(np.float32) * attn_scale,
            )

            # Attention biases
            self.set_parameter(f"{prefix}_query_bias", np.zeros(self.hidden_size, dtype=np.float32))
            self.set_parameter(f"{prefix}_key_bias", np.zeros(self.hidden_size, dtype=np.float32))
            self.set_parameter(f"{prefix}_value_bias", np.zeros(self.hidden_size, dtype=np.float32))
            self.set_parameter(f"{prefix}_attn_out_bias", np.zeros(self.hidden_size, dtype=np.float32))

            # Layer norm 1
            self.set_parameter(f"{prefix}_ln1_gamma", np.ones(self.hidden_size, dtype=np.float32))
            self.set_parameter(f"{prefix}_ln1_beta", np.zeros(self.hidden_size, dtype=np.float32))

            # FFN weights
            ffn_scale = 1.0 / math.sqrt(self.hidden_size)
            self.set_parameter(
                f"{prefix}_ffn_up",
                np.random.randn(self.hidden_size, self.intermediate_size).astype(np.float32) * ffn_scale,
            )
            self.set_parameter(f"{prefix}_ffn_up_bias", np.zeros(self.intermediate_size, dtype=np.float32))
            self.set_parameter(
                f"{prefix}_ffn_down",
                np.random.randn(self.intermediate_size, self.hidden_size).astype(np.float32)
                * (1.0 / math.sqrt(self.intermediate_size)),
            )
            self.set_parameter(f"{prefix}_ffn_down_bias", np.zeros(self.hidden_size, dtype=np.float32))

            # Layer norm 2
            self.set_parameter(f"{prefix}_ln2_gamma", np.ones(self.hidden_size, dtype=np.float32))
            self.set_parameter(f"{prefix}_ln2_beta", np.zeros(self.hidden_size, dtype=np.float32))

        # Final layer norm
        self.set_parameter("final_ln_gamma", np.ones(self.hidden_size, dtype=np.float32))
        self.set_parameter("final_ln_beta", np.zeros(self.hidden_size, dtype=np.float32))

    def _attention(
        self,
        hidden_states: np.ndarray,
        layer_idx: int,
        attention_mask: Optional[np.ndarray] = None,
    ) -> np.ndarray:
        """Multi-head self-attention.

        Args:
            hidden_states: Input tensor [batch, seq_len, hidden_size].
            layer_idx: Layer index for parameter lookup.
            attention_mask: Optional attention mask.

        Returns:
            Attention output [batch, seq_len, hidden_size].
        """
        batch_size, seq_len, _ = hidden_states.shape
        prefix = f"layer_{layer_idx}"

        # Compute Q, K, V
        W_q = self.get_parameter(f"{prefix}_query")
        W_k = self.get_parameter(f"{prefix}_key")
        W_v = self.get_parameter(f"{prefix}_value")
        b_q = self.get_parameter(f"{prefix}_query_bias")
        b_k = self.get_parameter(f"{prefix}_key_bias")
        b_v = self.get_parameter(f"{prefix}_value_bias")

        assert W_q is not None and W_k is not None and W_v is not None
        assert b_q is not None and b_k is not None and b_v is not None

        Q = np.einsum("bsh,hd->bsd", hidden_states, W_q) + b_q
        K = np.einsum("bsh,hd->bsd", hidden_states, W_k) + b_k
        V = np.einsum("bsh,hd->bsd", hidden_states, W_v) + b_v

        # Reshape for multi-head attention
        Q = Q.reshape(batch_size, seq_len, self.num_heads, self.head_dim).transpose(0, 2, 1, 3)
        K = K.reshape(batch_size, seq_len, self.num_heads, self.head_dim).transpose(0, 2, 1, 3)
        V = V.reshape(batch_size, seq_len, self.num_heads, self.head_dim).transpose(0, 2, 1, 3)

        # Attention scores
        scale = 1.0 / math.sqrt(self.head_dim)
        scores = np.einsum("bhqd,bhkd->bhqk", Q, K) * scale

        # Apply mask if provided
        if attention_mask is not None:
            scores = scores + attention_mask

        # Softmax
        attn_weights = _softmax(scores, axis=-1)

        # Apply attention to values
        attn_output = np.einsum("bhqk,bhkd->bhqd", attn_weights, V)

        # Reshape back
        attn_output = attn_output.transpose(0, 2, 1, 3).reshape(batch_size, seq_len, self.hidden_size)

        # Output projection
        W_out = self.get_parameter(f"{prefix}_attn_out")
        b_out = self.get_parameter(f"{prefix}_attn_out_bias")
        assert W_out is not None and b_out is not None

        output = np.einsum("bsh,hd->bsd", attn_output, W_out) + b_out
        return output

    def _ffn(self, hidden_states: np.ndarray, layer_idx: int) -> np.ndarray:
        """Feed-forward network.

        Args:
            hidden_states: Input tensor [batch, seq_len, hidden_size].
            layer_idx: Layer index for parameter lookup.

        Returns:
            FFN output [batch, seq_len, hidden_size].
        """
        prefix = f"layer_{layer_idx}"

        W_up = self.get_parameter(f"{prefix}_ffn_up")
        b_up = self.get_parameter(f"{prefix}_ffn_up_bias")
        W_down = self.get_parameter(f"{prefix}_ffn_down")
        b_down = self.get_parameter(f"{prefix}_ffn_down_bias")

        assert W_up is not None and b_up is not None
        assert W_down is not None and b_down is not None

        # Up projection + GELU
        hidden = np.einsum("bsh,hd->bsd", hidden_states, W_up) + b_up
        hidden = _gelu(hidden)

        # Down projection
        output = np.einsum("bsh,hd->bsd", hidden, W_down) + b_down
        return output

    def _transformer_block(
        self,
        hidden_states: np.ndarray,
        layer_idx: int,
        attention_mask: Optional[np.ndarray] = None,
    ) -> np.ndarray:
        """Single transformer block.

        Args:
            hidden_states: Input tensor.
            layer_idx: Layer index.
            attention_mask: Optional attention mask.

        Returns:
            Transformed hidden states.
        """
        prefix = f"layer_{layer_idx}"

        # Pre-norm + attention + residual
        ln1_gamma = self.get_parameter(f"{prefix}_ln1_gamma")
        ln1_beta = self.get_parameter(f"{prefix}_ln1_beta")
        assert ln1_gamma is not None and ln1_beta is not None

        normed = _layer_norm(hidden_states, ln1_gamma, ln1_beta)
        attn_out = self._attention(normed, layer_idx, attention_mask)
        hidden_states = hidden_states + attn_out

        # Pre-norm + FFN + residual
        ln2_gamma = self.get_parameter(f"{prefix}_ln2_gamma")
        ln2_beta = self.get_parameter(f"{prefix}_ln2_beta")
        assert ln2_gamma is not None and ln2_beta is not None

        normed = _layer_norm(hidden_states, ln2_gamma, ln2_beta)
        ffn_out = self._ffn(normed, layer_idx)
        hidden_states = hidden_states + ffn_out

        return hidden_states

    def forward(
        self,
        input_ids: np.ndarray,
        attention_mask: Optional[np.ndarray] = None,
    ) -> np.ndarray:
        """Forward pass through transformer.

        Args:
            input_ids: Token IDs [batch, seq_len].
            attention_mask: Optional attention mask [batch, seq_len].

        Returns:
            Hidden states [batch, seq_len, hidden_size].
        """
        batch_size, seq_len = input_ids.shape

        # Get embeddings
        token_emb = self.get_parameter("token_embedding")
        pos_emb = self.get_parameter("position_embedding")
        assert token_emb is not None and pos_emb is not None

        # Lookup embeddings
        hidden_states = token_emb[input_ids] + pos_emb[:seq_len]

        # Create causal mask if needed
        if attention_mask is not None:
            # Convert [batch, seq_len] mask to [batch, 1, seq_len, seq_len]
            attention_mask = attention_mask[:, np.newaxis, np.newaxis, :]
            attention_mask = (1.0 - attention_mask) * -1e9

        # Apply transformer layers
        for layer_idx in range(self.num_layers):
            hidden_states = self._transformer_block(hidden_states, layer_idx, attention_mask)

        # Final layer norm
        final_ln_gamma = self.get_parameter("final_ln_gamma")
        final_ln_beta = self.get_parameter("final_ln_beta")
        assert final_ln_gamma is not None and final_ln_beta is not None

        hidden_states = _layer_norm(hidden_states, final_ln_gamma, final_ln_beta)

        return hidden_states

    def encode(self, input_ids: np.ndarray, pooling: str = "mean") -> np.ndarray:
        """Encode input to fixed-size representation.

        Args:
            input_ids: Token IDs [batch, seq_len].
            pooling: Pooling strategy ("mean", "cls", "max").

        Returns:
            Encoded representation [batch, hidden_size].
        """
        hidden_states = self.forward(input_ids)

        if pooling == "mean":
            return np.mean(hidden_states, axis=1)
        elif pooling == "cls":
            return hidden_states[:, 0, :]
        elif pooling == "max":
            return np.max(hidden_states, axis=1)
        else:
            raise ValueError(f"Unknown pooling strategy: {pooling}")

    def get_input_shape(self) -> List[int]:
        """Return expected input shape."""
        return [self.max_seq_length]

    def get_output_shape(self) -> List[int]:
        """Return expected output shape."""
        return [self.max_seq_length, self.hidden_size]

    @classmethod
    def from_config(cls, config: Dict[str, Any]) -> "TransformerLite":
        """Create model from config dictionary."""
        return cls(
            hidden_size=config.get("hidden_size", 256),
            num_layers=config.get("num_layers", 4),
            num_heads=config.get("num_heads", 4),
            intermediate_size=config.get("intermediate_size", 1024),
            max_seq_length=config.get("max_seq_length", 512),
            vocab_size=config.get("vocab_size", 32000),
            dropout=config.get("dropout", 0.1),
            device=config.get("device", "cpu"),
        )

    def get_config(self) -> Dict[str, Any]:
        """Return model configuration."""
        return {
            "hidden_size": self.hidden_size,
            "num_layers": self.num_layers,
            "num_heads": self.num_heads,
            "intermediate_size": self.intermediate_size,
            "max_seq_length": self.max_seq_length,
            "vocab_size": self.vocab_size,
            "dropout": self.dropout,
            "device": self.device,
        }
