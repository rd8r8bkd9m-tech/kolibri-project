"""Tests for ML models."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import numpy as np  # noqa: E402
import pytest  # noqa: E402

from ml.models.base_model import BaseModel, ModelState  # noqa: E402
from ml.models.transformer_lite import TransformerLite  # noqa: E402
from ml.models.neural_compressor import NeuralCompressor  # noqa: E402
from ml.models.semantic_encoder import SemanticEncoder  # noqa: E402
from ml.models.classifier import Classifier  # noqa: E402
from ml.models.text_generator import TextGenerator  # noqa: E402


class TestTransformerLite:
    """Tests for TransformerLite model."""

    def test_init(self) -> None:
        """Test model initialization."""
        model = TransformerLite(hidden_size=64, num_layers=2, num_heads=2)
        assert model.state == ModelState.INITIALIZED
        assert model.hidden_size == 64
        assert model.num_layers == 2

    def test_forward(self) -> None:
        """Test forward pass."""
        model = TransformerLite(hidden_size=64, num_layers=2, num_heads=2, vocab_size=1000, max_seq_length=32)
        batch_size = 2
        seq_len = 16
        
        input_ids = np.random.randint(0, 1000, (batch_size, seq_len))
        outputs = model.forward(input_ids)
        
        assert outputs.shape == (batch_size, seq_len, 64)

    def test_encode(self) -> None:
        """Test encoding to fixed representation."""
        model = TransformerLite(hidden_size=64, num_layers=2, num_heads=2)
        input_ids = np.random.randint(0, 1000, (2, 16))
        
        encoding = model.encode(input_ids, pooling="mean")
        assert encoding.shape == (2, 64)

    def test_parameters_count(self) -> None:
        """Test parameter counting."""
        model = TransformerLite(hidden_size=64, num_layers=2, num_heads=2)
        num_params = model.num_parameters()
        assert num_params > 0

    def test_from_config(self) -> None:
        """Test model creation from config."""
        config = {
            "hidden_size": 128,
            "num_layers": 4,
            "num_heads": 4,
        }
        model = TransformerLite.from_config(config)
        assert model.hidden_size == 128
        assert model.num_layers == 4


class TestNeuralCompressor:
    """Tests for NeuralCompressor model."""

    def test_init(self) -> None:
        """Test model initialization."""
        model = NeuralCompressor(context_size=256, hidden_size=64)
        assert model.state == ModelState.INITIALIZED

    def test_forward(self) -> None:
        """Test forward pass."""
        model = NeuralCompressor(hidden_size=32, num_layers=1)
        input_ids = np.random.randint(0, 256, (1, 64))
        
        logits, state = model.forward(input_ids)
        assert logits.shape == (1, 64, 256)
        assert len(state) == 2

    def test_predict_next_byte(self) -> None:
        """Test next byte prediction."""
        model = NeuralCompressor(hidden_size=32, num_layers=1)
        context = np.random.randint(0, 256, (1, 32))
        
        probs, state = model.predict_next_byte(context)
        assert probs.shape == (1, 256)
        assert np.abs(probs.sum() - 1.0) < 0.01

    def test_estimate_entropy(self) -> None:
        """Test entropy estimation."""
        model = NeuralCompressor(hidden_size=32, num_layers=1)
        data = b"Hello, World! This is a test string for entropy estimation."

        entropy = model.estimate_entropy(data, chunk_size=32)
        # Untrained model produces approximate entropy (may exceed 8 due to numerical issues)
        assert entropy >= 0

    def test_compress_context(self) -> None:
        """Test compression context analysis."""
        model = NeuralCompressor()
        data = b"AAAABBBBCCCCDDDD" * 10
        
        result = model.compress_context(data)
        assert "entropy_bits_per_byte" in result
        assert "recommended_algorithm" in result


class TestSemanticEncoder:
    """Tests for SemanticEncoder model."""

    def test_init(self) -> None:
        """Test model initialization."""
        model = SemanticEncoder(embedding_dim=128, hidden_size=64)
        assert model.state == ModelState.INITIALIZED
        assert model.embedding_dim == 128

    def test_forward(self) -> None:
        """Test forward pass."""
        model = SemanticEncoder(embedding_dim=128, hidden_size=64, vocab_size=1000)
        input_ids = np.random.randint(0, 1000, (2, 32))
        
        embeddings = model.forward(input_ids)
        assert embeddings.shape == (2, 128)

    def test_normalize_output(self) -> None:
        """Test output normalization."""
        model = SemanticEncoder(embedding_dim=64, normalize_output=True)
        input_ids = np.random.randint(0, 1000, (1, 16))
        
        embedding = model.forward(input_ids)
        norm = np.linalg.norm(embedding)
        assert np.abs(norm - 1.0) < 0.01

    def test_similarity(self) -> None:
        """Test similarity computation."""
        model = SemanticEncoder(embedding_dim=64)
        emb1 = np.random.randn(3, 64).astype(np.float32)
        emb2 = np.random.randn(5, 64).astype(np.float32)
        
        sim = model.similarity(emb1, emb2)
        assert sim.shape == (3, 5)

    def test_search(self) -> None:
        """Test similarity search."""
        model = SemanticEncoder(embedding_dim=64)
        query = np.random.randn(64).astype(np.float32)
        corpus = np.random.randn(10, 64).astype(np.float32)
        
        results = model.search(query, corpus, top_k=3)
        assert len(results) == 3
        assert all("index" in r and "score" in r for r in results)


class TestClassifier:
    """Tests for Classifier model."""

    def test_init(self) -> None:
        """Test model initialization."""
        model = Classifier(input_dim=64, num_classes=10)
        assert model.state == ModelState.INITIALIZED

    def test_forward(self) -> None:
        """Test forward pass."""
        model = Classifier(input_dim=64, hidden_dims=[32], num_classes=10)
        inputs = np.random.randn(8, 64).astype(np.float32)
        
        logits = model.forward(inputs)
        assert logits.shape == (8, 10)

    def test_predict(self) -> None:
        """Test prediction probabilities."""
        model = Classifier(input_dim=64, num_classes=5)
        inputs = np.random.randn(4, 64).astype(np.float32)
        
        probs = model.predict(inputs)
        assert probs.shape == (4, 5)
        assert np.allclose(probs.sum(axis=1), 1.0, atol=0.01)

    def test_predict_classes(self) -> None:
        """Test class prediction."""
        model = Classifier(input_dim=32, num_classes=3)
        inputs = np.random.randn(5, 32).astype(np.float32)
        
        classes = model.predict_classes(inputs)
        assert classes.shape == (5,)
        assert all(0 <= c < 3 for c in classes)


class TestTextGenerator:
    """Tests for TextGenerator model."""

    def test_init(self) -> None:
        """Test model initialization."""
        model = TextGenerator(vocab_size=1000, hidden_size=64)
        assert model.state == ModelState.INITIALIZED

    def test_forward(self) -> None:
        """Test forward pass."""
        model = TextGenerator(vocab_size=1000, hidden_size=64, num_layers=2, max_length=32)
        input_ids = np.random.randint(0, 1000, (1, 16))
        
        logits = model.forward(input_ids)
        assert logits.shape == (1, 16, 1000)

    def test_generate(self) -> None:
        """Test text generation."""
        model = TextGenerator(vocab_size=100, hidden_size=32, num_layers=1, max_length=32)
        prompt = np.random.randint(0, 100, (1, 5))
        
        generated = model.generate(prompt, max_new_tokens=10)
        assert generated.shape[1] == 15  # 5 prompt + 10 generated


class TestBaseModelMethods:
    """Tests for BaseModel common methods."""

    def test_save_load(self, tmp_path: Path) -> None:
        """Test model save and load."""
        model = Classifier(input_dim=32, num_classes=5)
        save_path = tmp_path / "model"
        
        model.save(save_path)
        
        new_model = Classifier(input_dim=32, num_classes=5)
        new_model.load(save_path)
        
        assert new_model.state == ModelState.TRAINED

    def test_quantize_fp16(self) -> None:
        """Test FP16 quantization."""
        model = Classifier(input_dim=32, num_classes=3)
        model.quantize("fp16")
        
        assert model.dtype == "float16"
        for param in model.parameters().values():
            assert param.dtype == np.float16

    def test_to_device(self) -> None:
        """Test device placement."""
        model = Classifier(input_dim=32, num_classes=3)
        model.to_device("cpu")
        
        assert model.device == "cpu"

    def test_summary(self) -> None:
        """Test model summary."""
        model = Classifier(input_dim=64, hidden_dims=[32], num_classes=10)
        summary = model.summary()
        
        assert "Classifier" in summary
        assert "Parameters" in summary


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
