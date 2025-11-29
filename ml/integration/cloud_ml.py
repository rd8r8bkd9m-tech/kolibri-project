"""ML for cloud storage semantic search."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

import numpy as np

from ..models.semantic_encoder import SemanticEncoder
from ..utils.logger import get_logger


@dataclass
class SearchResult:
    """Search result with document info and score."""

    doc_id: str
    title: str
    content_preview: str
    score: float
    metadata: Dict[str, Any]


class CloudMLSearch:
    """ML-powered semantic search for cloud storage.

    Provides:
    - Vector embeddings for documents
    - Semantic similarity search
    - Integration with FAISS/Annoy for efficient retrieval
    """

    def __init__(
        self,
        encoder: Optional[SemanticEncoder] = None,
        embedding_dim: int = 384,
    ) -> None:
        """Initialize cloud search.

        Args:
            encoder: Semantic encoder model.
            embedding_dim: Embedding dimension.
        """
        self.encoder = encoder or SemanticEncoder(embedding_dim=embedding_dim)
        self.embedding_dim = embedding_dim
        self.logger = get_logger()

        # Document storage
        self._documents: Dict[str, Dict[str, Any]] = {}
        self._embeddings: Dict[str, np.ndarray] = {}
        self._index: Optional[Any] = None

    def add_document(
        self,
        doc_id: str,
        content: str,
        title: str = "",
        metadata: Optional[Dict[str, Any]] = None,
    ) -> None:
        """Add document to search index.

        Args:
            doc_id: Document identifier.
            content: Document content.
            title: Document title.
            metadata: Additional metadata.
        """
        # Compute embedding
        embedding = self._encode_text(content)

        # Store document
        self._documents[doc_id] = {
            "title": title or doc_id,
            "content": content,
            "metadata": metadata or {},
        }
        self._embeddings[doc_id] = embedding

        # Invalidate index
        self._index = None

        self.logger.debug(f"Added document: {doc_id}")

    def remove_document(self, doc_id: str) -> bool:
        """Remove document from index.

        Args:
            doc_id: Document identifier.

        Returns:
            True if document was removed.
        """
        if doc_id in self._documents:
            del self._documents[doc_id]
            del self._embeddings[doc_id]
            self._index = None
            return True
        return False

    def _encode_text(self, text: str) -> np.ndarray:
        """Encode text to embedding vector."""
        tokens = self._tokenize(text)
        token_ids = np.array([tokens], dtype=np.int64)
        return self.encoder.encode(token_ids)[0]

    def _tokenize(self, text: str) -> List[int]:
        """Simple tokenization."""
        tokens = []
        for char in text.lower()[:512]:
            if char.isalnum():
                tokens.append(ord(char) % 32000)
            elif char == " ":
                tokens.append(0)
        return tokens if tokens else [0]

    def search(
        self,
        query: str,
        top_k: int = 10,
        min_score: float = 0.0,
    ) -> List[SearchResult]:
        """Search for documents matching query.

        Args:
            query: Search query.
            top_k: Maximum results to return.
            min_score: Minimum similarity score.

        Returns:
            List of search results.
        """
        if not self._embeddings:
            return []

        # Encode query
        query_emb = self._encode_text(query)

        # Compute similarities
        results = []
        for doc_id, doc_emb in self._embeddings.items():
            score = float(np.dot(query_emb, doc_emb) / (
                np.linalg.norm(query_emb) * np.linalg.norm(doc_emb) + 1e-8
            ))

            if score >= min_score:
                doc = self._documents[doc_id]
                results.append(SearchResult(
                    doc_id=doc_id,
                    title=doc["title"],
                    content_preview=doc["content"][:200] + "..." if len(doc["content"]) > 200 else doc["content"],
                    score=score,
                    metadata=doc["metadata"],
                ))

        # Sort by score
        results.sort(key=lambda x: x.score, reverse=True)
        return results[:top_k]

    def find_similar(
        self,
        doc_id: str,
        top_k: int = 5,
    ) -> List[SearchResult]:
        """Find documents similar to given document.

        Args:
            doc_id: Reference document ID.
            top_k: Maximum results.

        Returns:
            List of similar documents.
        """
        if doc_id not in self._embeddings:
            return []

        query_emb = self._embeddings[doc_id]
        results = []

        for other_id, other_emb in self._embeddings.items():
            if other_id == doc_id:
                continue

            score = float(np.dot(query_emb, other_emb) / (
                np.linalg.norm(query_emb) * np.linalg.norm(other_emb) + 1e-8
            ))

            doc = self._documents[other_id]
            results.append(SearchResult(
                doc_id=other_id,
                title=doc["title"],
                content_preview=doc["content"][:200],
                score=score,
                metadata=doc["metadata"],
            ))

        results.sort(key=lambda x: x.score, reverse=True)
        return results[:top_k]

    def build_index(self, index_type: str = "flat") -> None:
        """Build search index for efficient retrieval.

        Args:
            index_type: Index type ("flat", "ivf", "hnsw").
        """
        if not self._embeddings:
            return

        try:
            import faiss

            embeddings = np.array(list(self._embeddings.values()), dtype=np.float32)

            if index_type == "flat":
                self._index = faiss.IndexFlatIP(self.embedding_dim)
            elif index_type == "ivf":
                quantizer = faiss.IndexFlatIP(self.embedding_dim)
                nlist = min(100, len(embeddings) // 10 + 1)
                self._index = faiss.IndexIVFFlat(quantizer, self.embedding_dim, nlist)
                self._index.train(embeddings)
            elif index_type == "hnsw":
                self._index = faiss.IndexHNSWFlat(self.embedding_dim, 32)

            # Normalize for inner product
            faiss.normalize_L2(embeddings)
            self._index.add(embeddings)

            self.logger.info(f"Built {index_type} index with {len(embeddings)} documents")

        except ImportError:
            self.logger.warning("FAISS not available, using brute-force search")
            self._index = None

    def save_index(self, path: Path | str) -> None:
        """Save search index to disk.

        Args:
            path: Output path.
        """
        import json

        path = Path(path)
        path.mkdir(parents=True, exist_ok=True)

        # Save documents
        with open(path / "documents.json", "w", encoding="utf-8") as f:
            json.dump(self._documents, f, ensure_ascii=False, indent=2)

        # Save embeddings
        embeddings_array = {k: v.tolist() for k, v in self._embeddings.items()}
        with open(path / "embeddings.json", "w", encoding="utf-8") as f:
            json.dump(embeddings_array, f)

        self.logger.info(f"Saved index to {path}")

    def load_index(self, path: Path | str) -> None:
        """Load search index from disk.

        Args:
            path: Input path.
        """
        import json

        path = Path(path)

        # Load documents
        with open(path / "documents.json", encoding="utf-8") as f:
            self._documents = json.load(f)

        # Load embeddings
        with open(path / "embeddings.json", encoding="utf-8") as f:
            embeddings_dict = json.load(f)
            self._embeddings = {k: np.array(v) for k, v in embeddings_dict.items()}

        self._index = None
        self.logger.info(f"Loaded index with {len(self._documents)} documents")

    def get_stats(self) -> Dict[str, Any]:
        """Get index statistics.

        Returns:
            Statistics dictionary.
        """
        return {
            "num_documents": len(self._documents),
            "embedding_dim": self.embedding_dim,
            "has_index": self._index is not None,
            "total_content_size": sum(
                len(d["content"]) for d in self._documents.values()
            ),
        }
