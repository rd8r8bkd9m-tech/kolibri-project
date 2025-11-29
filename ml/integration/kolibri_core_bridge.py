"""Integration bridge between Kolibri ML and core KolibriSim."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, List, Optional, TYPE_CHECKING

import numpy as np

if TYPE_CHECKING:
    from core.kolibri_sim import KolibriSim

from ..models.semantic_encoder import SemanticEncoder
from ..models.neural_compressor import NeuralCompressor
from ..utils.logger import get_logger
from ..utils.tokenizer import simple_tokenize


class KolibriCoreBridge:
    """Bridge between Kolibri ML models and KolibriSim core.

    Provides:
    - ML-enhanced semantic encoding for knowledge storage
    - Neural compression for genome data
    - Intelligent formula evolution using ML predictions
    """

    def __init__(
        self,
        sim: Optional["KolibriSim"] = None,
        encoder: Optional[SemanticEncoder] = None,
        compressor: Optional[NeuralCompressor] = None,
    ) -> None:
        """Initialize bridge.

        Args:
            sim: KolibriSim instance to enhance.
            encoder: Semantic encoder model.
            compressor: Neural compressor model.
        """
        self.sim = sim
        self.encoder = encoder or SemanticEncoder()
        self.compressor = compressor or NeuralCompressor()
        self.logger = get_logger()

        # Knowledge embeddings cache
        self._embeddings_cache: Dict[str, np.ndarray] = {}

    def attach_sim(self, sim: "KolibriSim") -> None:
        """Attach a KolibriSim instance.

        Args:
            sim: KolibriSim instance.
        """
        self.sim = sim
        self.logger.info(f"Attached KolibriSim (seed={sim.zerno})")

    def encode_knowledge(self, text: str) -> np.ndarray:
        """Encode text to semantic vector representation.

        Args:
            text: Text to encode.

        Returns:
            Semantic embedding vector.
        """
        # Simple tokenization for demonstration
        tokens = self._tokenize(text)
        token_ids = np.array([tokens], dtype=np.int64)
        embedding = self.encoder.encode(token_ids)
        return embedding[0]

    def _tokenize(self, text: str) -> List[int]:
        """Tokenize text using shared utility."""
        return simple_tokenize(text, vocab_size=32000, max_length=512)

    def enhance_teaching(
        self,
        stimulus: str,
        response: str,
    ) -> Dict[str, Any]:
        """Enhance knowledge teaching with semantic embeddings.

        Args:
            stimulus: Stimulus/key text.
            response: Response/value text.

        Returns:
            Enhancement metadata.
        """
        if self.sim is None:
            raise RuntimeError("No KolibriSim attached")

        # Compute embeddings
        stimulus_emb = self.encode_knowledge(stimulus)
        response_emb = self.encode_knowledge(response)

        # Cache embeddings
        self._embeddings_cache[stimulus] = stimulus_emb

        # Compute semantic similarity
        similarity = float(np.dot(stimulus_emb, response_emb))

        # Teach via KolibriSim
        self.sim.obuchit_svjaz(stimulus, response)

        return {
            "stimulus": stimulus,
            "response": response,
            "embedding_dim": len(stimulus_emb),
            "semantic_similarity": similarity,
        }

    def semantic_search(
        self,
        query: str,
        top_k: int = 5,
    ) -> List[Dict[str, Any]]:
        """Search knowledge base using semantic similarity.

        Args:
            query: Search query.
            top_k: Number of results to return.

        Returns:
            List of matching results with scores.
        """
        if self.sim is None:
            raise RuntimeError("No KolibriSim attached")

        query_emb = self.encode_knowledge(query)
        results = []

        for stimulus, response in self.sim.znanija.items():
            if stimulus in self._embeddings_cache:
                emb = self._embeddings_cache[stimulus]
            else:
                emb = self.encode_knowledge(stimulus)
                self._embeddings_cache[stimulus] = emb

            similarity = float(np.dot(query_emb, emb))
            results.append({
                "stimulus": stimulus,
                "response": response,
                "score": similarity,
            })

        # Sort by similarity
        results.sort(key=lambda x: x["score"], reverse=True)
        return results[:top_k]

    def compress_genome(self) -> Dict[str, Any]:
        """Compress genome data using neural compression.

        Returns:
            Compression statistics.
        """
        if self.sim is None:
            raise RuntimeError("No KolibriSim attached")

        # Collect genome data
        genome_data = []
        for blok in self.sim.genom:
            genome_data.extend([int(c) for c in blok.payload if c.isdigit()])

        if not genome_data:
            return {"original_size": 0, "compressed_size": 0, "ratio": 1.0}

        # Convert to bytes
        data_bytes = bytes(d % 256 for d in genome_data[:1024])
        original_size = len(data_bytes)

        # Estimate entropy
        entropy = self.compressor.estimate_entropy(data_bytes)
        theoretical_compressed = int(original_size * entropy / 8)

        return {
            "original_size": original_size,
            "entropy_bits_per_byte": entropy,
            "theoretical_compressed_size": theoretical_compressed,
            "compression_ratio": original_size / max(theoretical_compressed, 1),
        }

    def predict_formula_fitness(
        self,
        formula_code: str,
        context: str,
    ) -> float:
        """Predict fitness of a formula before evaluation.

        Args:
            formula_code: Formula code string.
            context: Context for the formula.

        Returns:
            Predicted fitness score (0-1).
        """
        # Encode formula and context
        formula_emb = self.encode_knowledge(formula_code)
        context_emb = self.encode_knowledge(context)

        # Simple fitness prediction based on similarity to context
        base_similarity = float(np.dot(formula_emb, context_emb))

        # Add some randomness to encourage exploration
        noise = np.random.uniform(-0.1, 0.1)

        return np.clip(base_similarity + noise, 0.0, 1.0)

    def enhance_evolution(self, context: str) -> str:
        """Enhance formula evolution with ML guidance.

        Args:
            context: Evolution context.

        Returns:
            Name of created formula.
        """
        if self.sim is None:
            raise RuntimeError("No KolibriSim attached")

        # Evolve formula
        formula_name = self.sim.evolyuciya_formul(context)

        # Get predicted fitness
        formula = self.sim.formuly[formula_name]
        predicted = self.predict_formula_fitness(formula["kod"], context)

        # Evaluate with predicted boost
        self.sim.ocenit_formulu(formula_name, predicted)

        self.logger.debug(
            f"Enhanced evolution: {formula_name} predicted_fitness={predicted:.3f}"
        )

        return formula_name

    def get_statistics(self) -> Dict[str, Any]:
        """Get bridge statistics.

        Returns:
            Dictionary with bridge stats.
        """
        return {
            "attached": self.sim is not None,
            "embeddings_cached": len(self._embeddings_cache),
            "encoder_params": self.encoder.num_parameters(),
            "compressor_params": self.compressor.num_parameters(),
        }
