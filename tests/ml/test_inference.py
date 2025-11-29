"""Tests for ML inference components."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import numpy as np  # noqa: E402
import pytest  # noqa: E402

from ml.models.classifier import Classifier  # noqa: E402
from ml.models.semantic_encoder import SemanticEncoder  # noqa: E402
from ml.inference.predictor import Predictor, PredictorConfig  # noqa: E402
from ml.inference.quantization import quantize_tensor, dequantize_tensor, quantize_model  # noqa: E402
from ml.inference.batch_processor import BatchProcessor  # noqa: E402


class TestPredictor:
    """Tests for Predictor class."""

    def test_init(self) -> None:
        """Test Predictor initialization."""
        model = Classifier(input_dim=32, num_classes=5)
        predictor = Predictor(model)
        
        assert predictor.device_info is not None
        assert predictor.device_info.device_string == "cpu"

    def test_predict_single(self) -> None:
        """Test single prediction."""
        model = Classifier(input_dim=16, num_classes=3)
        predictor = Predictor(model)
        
        inputs = np.random.randn(16).astype(np.float32)
        outputs = predictor.predict(inputs)
        
        assert outputs.shape == (1, 3)

    def test_predict_batch(self) -> None:
        """Test batch prediction."""
        model = Classifier(input_dim=16, num_classes=3)
        predictor = Predictor(model)
        
        inputs = [np.random.randn(16).astype(np.float32) for _ in range(5)]
        outputs = predictor.predict_batch(inputs)
        
        assert len(outputs) == 5

    def test_warmup(self) -> None:
        """Test predictor warmup."""
        model = Classifier(input_dim=8, num_classes=2)
        predictor = Predictor(model)
        
        predictor.warmup(num_iterations=2)
        
        stats = predictor.get_stats()
        assert stats["inference_count"] == 2

    def test_get_stats(self) -> None:
        """Test statistics retrieval."""
        model = Classifier(input_dim=8, num_classes=2)
        predictor = Predictor(model)
        
        inputs = np.random.randn(8).astype(np.float32)
        predictor.predict(inputs)
        
        stats = predictor.get_stats()
        assert "inference_count" in stats
        assert "avg_latency_ms" in stats
        assert stats["inference_count"] == 1

    def test_reset_stats(self) -> None:
        """Test statistics reset."""
        model = Classifier(input_dim=8, num_classes=2)
        predictor = Predictor(model)
        
        inputs = np.random.randn(8).astype(np.float32)
        predictor.predict(inputs)
        predictor.reset_stats()
        
        stats = predictor.get_stats()
        assert stats["inference_count"] == 0


class TestQuantization:
    """Tests for quantization utilities."""

    def test_quantize_fp16(self) -> None:
        """Test FP16 quantization."""
        tensor = np.random.randn(10, 10).astype(np.float32)
        
        quantized, params = quantize_tensor(tensor, mode="fp16")
        
        assert quantized.dtype == np.float16
        assert len(params) == 0

    def test_quantize_int8(self) -> None:
        """Test INT8 quantization."""
        tensor = np.random.randn(10, 10).astype(np.float32)
        
        quantized, params = quantize_tensor(tensor, mode="int8")
        
        assert quantized.dtype == np.int8
        assert "scale" in params
        assert "zero_point" in params

    def test_dequantize_int8(self) -> None:
        """Test INT8 dequantization."""
        original = np.random.randn(5, 5).astype(np.float32)

        quantized, params = quantize_tensor(original, mode="int8")
        restored = dequantize_tensor(quantized, params, mode="int8")

        # Should be approximately equal (INT8 has limited precision)
        np.testing.assert_allclose(original, restored, rtol=0.2, atol=0.1)

    def test_quantize_model(self) -> None:
        """Test full model quantization."""
        model = Classifier(input_dim=16, num_classes=3)
        
        quantized_model = quantize_model(model)
        
        assert quantized_model.dtype == "int8"

    def test_quantize_preserves_inference(self) -> None:
        """Test that quantization preserves inference capability."""
        model = Classifier(input_dim=8, num_classes=2)
        inputs = np.random.randn(4, 8).astype(np.float32)
        
        # Get original outputs
        original_outputs = model.forward(inputs)
        
        # Quantize to fp16 (should be close to original)
        model.quantize("fp16")
        quantized_outputs = model.forward(inputs.astype(np.float16))
        
        # Outputs should be similar
        assert original_outputs.shape == quantized_outputs.shape


class TestBatchProcessor:
    """Tests for batch processor."""

    def test_process_batch_sync(self) -> None:
        """Test synchronous batch processing."""
        def process_fn(x):
            return x * 2
        
        processor = BatchProcessor(process_fn, max_batch_size=4)
        
        inputs = [np.array([i]) for i in range(10)]
        outputs = processor.process_batch_sync(inputs)
        
        assert len(outputs) == 10
        for i, out in enumerate(outputs):
            np.testing.assert_array_equal(out, np.array([i * 2]))

    def test_batch_chunking(self) -> None:
        """Test that batches are properly chunked."""
        call_count = [0]
        
        def counting_fn(x):
            call_count[0] += 1
            return x
        
        processor = BatchProcessor(counting_fn, max_batch_size=3)
        
        inputs = [np.array([i]) for i in range(10)]
        processor.process_batch_sync(inputs)
        
        assert call_count[0] == 4  # ceil(10/3)


class TestONNXRuntime:
    """Tests for ONNX Runtime integration."""

    def test_onnx_available(self) -> None:
        """Test ONNX availability check."""
        from ml.inference.onnx_runtime import is_onnx_available
        
        # Just check that the function works
        result = is_onnx_available()
        assert isinstance(result, bool)


class TestInferenceIntegration:
    """Integration tests for inference pipeline."""

    def test_semantic_search_inference(self) -> None:
        """Test semantic encoder inference pipeline."""
        encoder = SemanticEncoder(embedding_dim=64, hidden_size=32, vocab_size=1000)
        predictor = Predictor(encoder)
        
        # Encode some "documents"
        docs = [
            np.random.randint(0, 1000, 32),
            np.random.randint(0, 1000, 32),
            np.random.randint(0, 1000, 32),
        ]
        
        embeddings = []
        for doc in docs:
            emb = predictor.predict(doc.reshape(1, -1))
            embeddings.append(emb[0])
        
        # All embeddings should have same dimension
        assert all(e.shape == (64,) for e in embeddings)

    def test_classification_inference(self) -> None:
        """Test classifier inference pipeline."""
        model = Classifier(input_dim=32, hidden_dims=[16], num_classes=5)
        config = PredictorConfig(max_batch_size=8)
        predictor = Predictor(model, config)
        
        # Classify batch
        inputs = [np.random.randn(32).astype(np.float32) for _ in range(20)]
        outputs = predictor.predict_batch(inputs, batch_size=8)
        
        assert len(outputs) == 20
        assert all(o.shape == (5,) for o in outputs)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
