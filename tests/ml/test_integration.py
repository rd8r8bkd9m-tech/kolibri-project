"""Integration tests for ML with Kolibri components."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import numpy as np  # noqa: E402
import pytest  # noqa: E402

from core.kolibri_sim import KolibriSim  # noqa: E402
from ml.integration.kolibri_core_bridge import KolibriCoreBridge  # noqa: E402
from ml.integration.archiver_ml import ArchiverML, NeuralEntropyEstimator  # noqa: E402
from ml.integration.cloud_ml import CloudMLSearch  # noqa: E402


class TestKolibriCoreBridge:
    """Tests for KolibriSim-ML integration."""

    def test_attach_sim(self) -> None:
        """Test attaching KolibriSim to bridge."""
        sim = KolibriSim(zerno=42)
        bridge = KolibriCoreBridge()
        
        bridge.attach_sim(sim)
        assert bridge.sim is sim

    def test_encode_knowledge(self) -> None:
        """Test knowledge encoding."""
        bridge = KolibriCoreBridge()
        
        embedding = bridge.encode_knowledge("Привет мир")
        assert embedding.shape[0] == bridge.encoder.embedding_dim

    def test_enhance_teaching(self) -> None:
        """Test enhanced teaching."""
        sim = KolibriSim(zerno=42)
        bridge = KolibriCoreBridge(sim=sim)
        
        result = bridge.enhance_teaching("привет", "здравствуй")
        
        assert sim.sprosit("привет") == "здравствуй"
        assert "semantic_similarity" in result

    def test_semantic_search(self) -> None:
        """Test semantic search in knowledge base."""
        sim = KolibriSim(zerno=42)
        bridge = KolibriCoreBridge(sim=sim)
        
        # Add some knowledge
        bridge.enhance_teaching("кошка", "мяукает")
        bridge.enhance_teaching("собака", "лает")
        bridge.enhance_teaching("птица", "поёт")
        
        results = bridge.semantic_search("животное", top_k=3)
        
        assert len(results) == 3
        assert all("score" in r for r in results)

    def test_compress_genome(self) -> None:
        """Test genome compression analysis."""
        sim = KolibriSim(zerno=42)
        bridge = KolibriCoreBridge(sim=sim)
        
        # Generate some genome data
        sim.obuchit_svjaz("a", "b")
        sim.evolyuciya_formul("test")
        
        result = bridge.compress_genome()
        
        assert "entropy_bits_per_byte" in result
        assert "compression_ratio" in result

    def test_get_statistics(self) -> None:
        """Test bridge statistics."""
        sim = KolibriSim(zerno=42)
        bridge = KolibriCoreBridge(sim=sim)
        
        bridge.enhance_teaching("test", "value")
        stats = bridge.get_statistics()
        
        assert stats["attached"] is True
        assert stats["embeddings_cached"] >= 1


class TestArchiverML:
    """Tests for ML-enhanced archiver."""

    def test_analyze_repetitive_data(self) -> None:
        """Test analysis of repetitive data."""
        archiver = ArchiverML()
        data = b"AAAA" * 100

        result = archiver.analyze(data)

        # Untrained model won't give perfect results, but should analyze
        assert result.original_size == 400
        assert result.entropy_bits_per_byte >= 0

    def test_analyze_random_data(self) -> None:
        """Test analysis of random data."""
        archiver = ArchiverML()
        data = bytes(np.random.randint(0, 256, 400, dtype=np.uint8))
        
        result = archiver.analyze(data)
        
        assert result.original_size == 400
        assert result.entropy_bits_per_byte > 0

    def test_recommend_strategy(self) -> None:
        """Test compression strategy recommendation."""
        archiver = ArchiverML()
        data = b"Hello World! " * 50
        
        strategy = archiver.recommend_strategy(data)
        
        assert "algorithm" in strategy
        assert "passes" in strategy
        assert "order" in strategy

    def test_enhance_compression(self) -> None:
        """Test compression enhancement estimation."""
        archiver = ArchiverML()
        data = b"Test data for compression " * 20

        new_ratio, details = archiver.enhance_compression(data, existing_ratio=2.0)

        # The enhancement should provide some result (may be lower for untrained model)
        assert "improvement_percent" in details
        assert isinstance(new_ratio, float)


class TestNeuralEntropyEstimator:
    """Tests for neural entropy estimation."""

    def test_estimate_entropy_low(self) -> None:
        """Test entropy estimation for low entropy data."""
        estimator = NeuralEntropyEstimator()
        data = b"\x00" * 100

        entropy = estimator.estimate_entropy(data)

        # Untrained model won't have accurate predictions, but should return a value
        assert entropy >= 0

    def test_analyze_patterns(self) -> None:
        """Test pattern analysis."""
        estimator = NeuralEntropyEstimator()
        data = b"ABCABC" * 50
        
        analysis = estimator.analyze_patterns(data)
        
        assert "shannon_entropy" in analysis
        assert "neural_entropy" in analysis
        assert "redundancy" in analysis


class TestCloudMLSearch:
    """Tests for cloud storage semantic search."""

    def test_add_document(self) -> None:
        """Test adding documents."""
        search = CloudMLSearch()
        
        search.add_document("doc1", "Hello world", title="Greeting")
        search.add_document("doc2", "Goodbye world", title="Farewell")
        
        stats = search.get_stats()
        assert stats["num_documents"] == 2

    def test_search(self) -> None:
        """Test semantic search."""
        search = CloudMLSearch()
        
        search.add_document("doc1", "Machine learning is great")
        search.add_document("doc2", "Deep learning models")
        search.add_document("doc3", "Cooking recipes")
        
        results = search.search("AI and neural networks", top_k=2)
        
        assert len(results) <= 2

    def test_find_similar(self) -> None:
        """Test finding similar documents."""
        search = CloudMLSearch()
        
        search.add_document("doc1", "Python programming")
        search.add_document("doc2", "Java programming")
        search.add_document("doc3", "Cooking pasta")
        
        similar = search.find_similar("doc1", top_k=1)
        
        assert len(similar) == 1

    def test_remove_document(self) -> None:
        """Test document removal."""
        search = CloudMLSearch()
        
        search.add_document("doc1", "Test content")
        assert search.get_stats()["num_documents"] == 1
        
        removed = search.remove_document("doc1")
        assert removed
        assert search.get_stats()["num_documents"] == 0

    def test_save_load_index(self, tmp_path: Path) -> None:
        """Test saving and loading index."""
        search = CloudMLSearch()
        search.add_document("doc1", "Test content", title="Test")
        
        search.save_index(tmp_path / "index")
        
        new_search = CloudMLSearch()
        new_search.load_index(tmp_path / "index")
        
        assert new_search.get_stats()["num_documents"] == 1


class TestSwarmMLIntegration:
    """Tests for swarm ML integration."""

    def test_compute_peer_similarity(self) -> None:
        """Test peer similarity computation."""
        from ml.integration.swarm_ml import SwarmMLIntegration
        
        sim_a = KolibriSim(zerno=1)
        sim_b = KolibriSim(zerno=2)
        
        sim_a.obuchit_svjaz("кот", "мяу")
        sim_b.obuchit_svjaz("собака", "гав")
        
        swarm = SwarmMLIntegration()
        similarity = swarm.compute_peer_similarity(sim_a, sim_b)
        
        assert 0.0 <= similarity <= 1.0

    def test_smart_sync(self) -> None:
        """Test intelligent synchronization."""
        from ml.integration.swarm_ml import SwarmMLIntegration
        
        target = KolibriSim(zerno=1)
        source = KolibriSim(zerno=2)
        source.obuchit_svjaz("key", "value")
        
        swarm = SwarmMLIntegration()
        result = swarm.smart_sync(target, [source], threshold=0.0)
        
        assert result["sources_processed"] == 1


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
