"""ML integration with Kolibri swarm intelligence."""

from __future__ import annotations

from typing import Any, Dict, List, Optional, TYPE_CHECKING

import numpy as np

if TYPE_CHECKING:
    from core.kolibri_sim import KolibriSim

from ..models.semantic_encoder import SemanticEncoder
from ..utils.logger import get_logger
from ..utils.tokenizer import simple_tokenize


class SwarmMLIntegration:
    """ML integration for Kolibri swarm intelligence.

    Enhances swarm operations with:
    - Semantic similarity for knowledge merging
    - Intelligent formula evolution across peers
    - Distributed model parameter aggregation
    """

    def __init__(
        self,
        encoder: Optional[SemanticEncoder] = None,
    ) -> None:
        """Initialize swarm ML integration.

        Args:
            encoder: Semantic encoder for knowledge embeddings.
        """
        self.encoder = encoder or SemanticEncoder()
        self.logger = get_logger()

        # Cache embeddings across peers
        self._peer_embeddings: Dict[int, Dict[str, np.ndarray]] = {}

    def compute_peer_similarity(
        self,
        peer_a: "KolibriSim",
        peer_b: "KolibriSim",
    ) -> float:
        """Compute semantic similarity between two peers.

        Args:
            peer_a: First peer.
            peer_b: Second peer.

        Returns:
            Similarity score (0-1).
        """
        emb_a = self._get_peer_embedding(peer_a)
        emb_b = self._get_peer_embedding(peer_b)

        # Cosine similarity
        similarity = float(np.dot(emb_a, emb_b) / (np.linalg.norm(emb_a) * np.linalg.norm(emb_b) + 1e-8))
        return max(0.0, similarity)

    def _get_peer_embedding(self, peer: "KolibriSim") -> np.ndarray:
        """Get or compute embedding for peer's knowledge."""
        peer_id = id(peer)

        if peer_id not in self._peer_embeddings:
            self._peer_embeddings[peer_id] = {}

        # Combine all knowledge into single embedding
        all_text = " ".join(f"{k} {v}" for k, v in peer.znanija.items())
        if not all_text:
            all_text = "empty"

        tokens = self._tokenize(all_text)
        token_ids = np.array([tokens], dtype=np.int64)
        embedding = self.encoder.encode(token_ids)[0]

        return embedding

    def _tokenize(self, text: str) -> List[int]:
        """Tokenize text using shared utility."""
        return simple_tokenize(text, vocab_size=32000, max_length=512)

    def smart_sync(
        self,
        target: "KolibriSim",
        sources: List["KolibriSim"],
        threshold: float = 0.5,
    ) -> Dict[str, Any]:
        """Intelligent synchronization based on semantic similarity.

        Args:
            target: Target peer to update.
            sources: Source peers to sync from.
            threshold: Minimum similarity for syncing.

        Returns:
            Sync statistics.
        """
        synced_knowledge = 0
        synced_formulas = 0
        rejected = 0

        for source in sources:
            if source is target:
                continue

            similarity = self.compute_peer_similarity(target, source)

            if similarity >= threshold:
                # Sync knowledge
                for stimulus, response in source.znanija.items():
                    if stimulus not in target.znanija:
                        target.obuchit_svjaz(stimulus, response)
                        synced_knowledge += 1

                # Sync formulas
                synced_formulas += target.import_formuly(source.formuly)
            else:
                rejected += 1
                self.logger.debug(
                    f"Rejected sync from peer (similarity={similarity:.3f} < {threshold})"
                )

        return {
            "synced_knowledge": synced_knowledge,
            "synced_formulas": synced_formulas,
            "rejected_peers": rejected,
            "sources_processed": len(sources),
        }

    def federated_aggregate(
        self,
        peers: List["KolibriSim"],
        weights: Optional[List[float]] = None,
    ) -> Dict[str, float]:
        """Aggregate formula fitness across peers (federated learning style).

        Args:
            peers: List of peers.
            weights: Optional weights for each peer.

        Returns:
            Aggregated formula fitness scores.
        """
        if not peers:
            return {}

        if weights is None:
            weights = [1.0 / len(peers)] * len(peers)

        # Collect all formula fitness scores
        all_formulas: Dict[str, List[tuple]] = {}

        for i, peer in enumerate(peers):
            for name, formula in peer.formuly.items():
                if name not in all_formulas:
                    all_formulas[name] = []
                all_formulas[name].append((formula["fitness"], weights[i]))

        # Compute weighted average
        aggregated = {}
        for name, scores in all_formulas.items():
            total_weight = sum(w for _, w in scores)
            if total_weight > 0:
                aggregated[name] = sum(f * w for f, w in scores) / total_weight

        return aggregated

    def select_best_peer(
        self,
        target: "KolibriSim",
        candidates: List["KolibriSim"],
        criterion: str = "similarity",
    ) -> Optional["KolibriSim"]:
        """Select best peer for knowledge transfer.

        Args:
            target: Target peer.
            candidates: Candidate source peers.
            criterion: Selection criterion ("similarity", "knowledge", "fitness").

        Returns:
            Best peer or None.
        """
        if not candidates:
            return None

        best_peer = None
        best_score = -float("inf")

        for candidate in candidates:
            if candidate is target:
                continue

            if criterion == "similarity":
                score = self.compute_peer_similarity(target, candidate)
            elif criterion == "knowledge":
                score = len(candidate.znanija)
            elif criterion == "fitness":
                scores = [f["fitness"] for f in candidate.formuly.values()]
                score = max(scores) if scores else 0.0
            else:
                score = 0.0

            if score > best_score:
                best_score = score
                best_peer = candidate

        return best_peer

    def evolve_swarm(
        self,
        peers: List["KolibriSim"],
        context: str,
        generations: int = 5,
    ) -> Dict[str, Any]:
        """Run evolutionary optimization across swarm.

        Args:
            peers: List of peers.
            context: Evolution context.
            generations: Number of generations.

        Returns:
            Evolution statistics.
        """
        stats = {
            "generations": generations,
            "total_formulas": 0,
            "best_fitness": 0.0,
            "avg_fitness": 0.0,
        }

        for gen in range(generations):
            all_fitness = []

            for peer in peers:
                # Evolve formula
                name = peer.evolyuciya_formul(context)

                # Evaluate with some randomness
                fitness = np.random.random()
                peer.ocenit_formulu(name, fitness)
                all_fitness.append(fitness)

            # Sync between peers
            for i, peer in enumerate(peers):
                others = [p for j, p in enumerate(peers) if j != i]
                peer.zapustit_roj(others, cikly=1)

            stats["total_formulas"] += len(peers)

        # Final statistics
        if all_fitness:
            stats["best_fitness"] = float(max(all_fitness))
            stats["avg_fitness"] = float(np.mean(all_fitness))

        return stats
