"""Semantic encoder for document embeddings and similarity search."""

from __future__ import annotations

import math
from typing import Any, Dict, List, Optional

import numpy as np

from .base_model import BaseModel, ModelState


def _normalize(x: np.ndarray, axis: int = -1, eps: float = 1e-8) -> np.ndarray:
    """L2 normalize vectors."""
    norm = np.linalg.norm(x, axis=axis, keepdims=True)
    return x / (norm + eps)


class SemanticEncoder(BaseModel):
    """Semantic encoder for document embeddings and similarity search.

    This model provides:
    - Fixed-size vector representations of text
    - Cosine similarity for semantic search
    - Integration with FAISS/Annoy for efficient retrieval
    - Cloud-storage semantic search support
    """

    def __init__(
        self,
        embedding_dim: int = 384,
        hidden_size: int = 256,
        num_layers: int = 2,
        vocab_size: int = 32000,
        max_seq_length: int = 512,
        normalize_output: bool = True,
        device: str = "cpu",
    ) -> None:
        """Initialize SemanticEncoder.

        Args:
            embedding_dim: Output embedding dimension.
            hidden_size: Internal hidden size.
            num_layers: Number of encoder layers.
            vocab_size: Vocabulary size.
            max_seq_length: Maximum input sequence length.
            normalize_output: Whether to L2-normalize output embeddings.
            device: Target device.
        """
        super().__init__(name="SemanticEncoder", device=device)

        self.embedding_dim = embedding_dim
        self.hidden_size = hidden_size
        self.num_layers = num_layers
        self.vocab_size = vocab_size
        self.max_seq_length = max_seq_length
        self.normalize_output = normalize_output

        self._init_parameters()
        self.state = ModelState.INITIALIZED

    def _init_parameters(self) -> None:
        """Initialize encoder parameters."""
        scale = 1.0 / math.sqrt(self.hidden_size)

        # Token embedding
        self.set_parameter(
            "token_embedding",
            np.random.randn(self.vocab_size, self.hidden_size).astype(np.float32) * scale,
        )

        # Position embedding
        self.set_parameter(
            "position_embedding",
            np.random.randn(self.max_seq_length, self.hidden_size).astype(np.float32) * 0.02,
        )

        # Encoder layers (simple feed-forward for efficiency)
        for layer_idx in range(self.num_layers):
            layer_scale = 1.0 / math.sqrt(self.hidden_size)
            self.set_parameter(
                f"encoder_{layer_idx}_w1",
                np.random.randn(self.hidden_size, self.hidden_size * 4).astype(np.float32) * layer_scale,
            )
            self.set_parameter(
                f"encoder_{layer_idx}_b1",
                np.zeros(self.hidden_size * 4, dtype=np.float32),
            )
            self.set_parameter(
                f"encoder_{layer_idx}_w2",
                np.random.randn(self.hidden_size * 4, self.hidden_size).astype(np.float32)
                * (1.0 / math.sqrt(self.hidden_size * 4)),
            )
            self.set_parameter(
                f"encoder_{layer_idx}_b2",
                np.zeros(self.hidden_size, dtype=np.float32),
            )

        # Output projection
        out_scale = 1.0 / math.sqrt(self.hidden_size)
        self.set_parameter(
            "output_proj",
            np.random.randn(self.hidden_size, self.embedding_dim).astype(np.float32) * out_scale,
        )
        self.set_parameter(
            "output_bias",
            np.zeros(self.embedding_dim, dtype=np.float32),
        )

    def _encoder_layer(self, x: np.ndarray, layer_idx: int) -> np.ndarray:
        """Single encoder layer with residual connection."""
        w1 = self.get_parameter(f"encoder_{layer_idx}_w1")
        b1 = self.get_parameter(f"encoder_{layer_idx}_b1")
        w2 = self.get_parameter(f"encoder_{layer_idx}_w2")
        b2 = self.get_parameter(f"encoder_{layer_idx}_b2")

        assert w1 is not None and b1 is not None
        assert w2 is not None and b2 is not None

        # FFN with GELU and residual
        hidden = np.maximum(0, x @ w1 + b1)  # ReLU for simplicity
        output = hidden @ w2 + b2
        return x + output  # Residual connection

    def forward(self, input_ids: np.ndarray) -> np.ndarray:
        """Forward pass to generate embeddings.

        Args:
            input_ids: Token IDs [batch, seq_len].

        Returns:
            Embeddings [batch, embedding_dim].
        """
        batch_size, seq_len = input_ids.shape
        seq_len = min(seq_len, self.max_seq_length)

        # Embedding lookup
        token_emb = self.get_parameter("token_embedding")
        pos_emb = self.get_parameter("position_embedding")
        assert token_emb is not None and pos_emb is not None

        x = token_emb[input_ids[:, :seq_len]] + pos_emb[:seq_len]

        # Encoder layers
        for layer_idx in range(self.num_layers):
            x = self._encoder_layer(x, layer_idx)

        # Mean pooling
        pooled = np.mean(x, axis=1)  # [batch, hidden_size]

        # Output projection
        W_out = self.get_parameter("output_proj")
        b_out = self.get_parameter("output_bias")
        assert W_out is not None and b_out is not None

        embeddings = pooled @ W_out + b_out

        # Normalize if requested
        if self.normalize_output:
            embeddings = _normalize(embeddings)

        return embeddings

    def encode(self, input_ids: np.ndarray) -> np.ndarray:
        """Alias for forward pass."""
        return self.forward(input_ids)

    def similarity(self, embeddings1: np.ndarray, embeddings2: np.ndarray) -> np.ndarray:
        """Compute cosine similarity between embedding sets.

        Args:
            embeddings1: First set of embeddings [n, embedding_dim].
            embeddings2: Second set of embeddings [m, embedding_dim].

        Returns:
            Similarity matrix [n, m].
        """
        # Normalize if not already
        emb1 = _normalize(embeddings1)
        emb2 = _normalize(embeddings2)
        return emb1 @ emb2.T

    def search(
        self,
        query_embedding: np.ndarray,
        corpus_embeddings: np.ndarray,
        top_k: int = 10,
    ) -> List[Dict[str, Any]]:
        """Search for most similar documents.

        Args:
            query_embedding: Query embedding [1, embedding_dim] or [embedding_dim].
            corpus_embeddings: Corpus embeddings [n, embedding_dim].
            top_k: Number of results to return.

        Returns:
            List of results with index and score.
        """
        if query_embedding.ndim == 1:
            query_embedding = query_embedding.reshape(1, -1)

        similarities = self.similarity(query_embedding, corpus_embeddings)[0]
        top_indices = np.argsort(similarities)[::-1][:top_k]

        results = []
        for idx in top_indices:
            results.append({"index": int(idx), "score": float(similarities[idx])})

        return results

    def get_input_shape(self) -> List[int]:
        """Return expected input shape."""
        return [self.max_seq_length]

    def get_output_shape(self) -> List[int]:
        """Return expected output shape."""
        return [self.embedding_dim]

    def get_config(self) -> Dict[str, Any]:
        """Return model configuration."""
        return {
            "embedding_dim": self.embedding_dim,
            "hidden_size": self.hidden_size,
            "num_layers": self.num_layers,
            "vocab_size": self.vocab_size,
            "max_seq_length": self.max_seq_length,
            "normalize_output": self.normalize_output,
            "device": self.device,
        }

    @classmethod
    def from_config(cls, config: Dict[str, Any]) -> "SemanticEncoder":
        """Create model from config dictionary."""
        return cls(
            embedding_dim=config.get("embedding_dim", 384),
            hidden_size=config.get("hidden_size", 256),
            num_layers=config.get("num_layers", 2),
            vocab_size=config.get("vocab_size", 32000),
            max_seq_length=config.get("max_seq_length", 512),
            normalize_output=config.get("normalize_output", True),
            device=config.get("device", "cpu"),
        )
