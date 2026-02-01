"""Universal predictor for inference across platforms."""

from __future__ import annotations

import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Union

import numpy as np

from ..models.base_model import BaseModel
from ..utils.device_detector import get_device, DeviceInfo
from ..utils.logger import get_logger
from ..utils.memory_manager import MemoryManager


@dataclass
class PredictorConfig:
    """Configuration for predictor."""

    device: str = "auto"
    num_threads: int = 4
    use_onnx: bool = True
    optimize_for_latency: bool = True
    max_batch_size: int = 32
    timeout: float = 30.0


class Predictor:
    """Universal predictor for cross-platform inference.

    Supports:
    - CPU inference with threading
    - GPU inference (CUDA/Metal)
    - ONNX Runtime integration
    - Automatic batching
    - Latency optimization
    """

    def __init__(
        self,
        model: BaseModel,
        config: Optional[PredictorConfig] = None,
    ) -> None:
        """Initialize predictor.

        Args:
            model: Model to use for inference.
            config: Predictor configuration.
        """
        self.model = model
        self.config = config or PredictorConfig()
        self.logger = get_logger()

        # Setup device
        self.device_info: DeviceInfo = get_device(self.config.device)
        self.model.to_device(self.device_info.device_string)
        self.logger.info(f"Predictor using device: {self.device_info.device_string}")

        # Memory management
        self.memory_manager = MemoryManager(self.device_info.device_string)

        # Statistics
        self._inference_count = 0
        self._total_latency = 0.0

    def predict(
        self,
        inputs: Union[np.ndarray, List[np.ndarray]],
        **kwargs: Any,
    ) -> np.ndarray:
        """Run inference on inputs.

        Args:
            inputs: Input data (single array or list of arrays).
            **kwargs: Additional arguments passed to model.

        Returns:
            Model predictions.
        """
        # Convert list to batch
        if isinstance(inputs, list):
            inputs = np.stack(inputs)

        # Ensure batch dimension
        if inputs.ndim == 1:
            inputs = inputs[np.newaxis, :]

        start_time = time.perf_counter()

        # Run inference
        outputs = self.model.forward(inputs)

        latency = (time.perf_counter() - start_time) * 1000  # ms

        # Update statistics
        self._inference_count += 1
        self._total_latency += latency

        if self._inference_count % 100 == 0:
            avg_latency = self._total_latency / self._inference_count
            self.logger.debug(
                f"Inference stats: count={self._inference_count}, "
                f"avg_latency={avg_latency:.2f}ms"
            )

        return outputs

    def predict_batch(
        self,
        batch: List[np.ndarray],
        batch_size: Optional[int] = None,
    ) -> List[np.ndarray]:
        """Run inference on a batch with automatic chunking.

        Args:
            batch: List of input arrays.
            batch_size: Override batch size for processing.

        Returns:
            List of prediction arrays.
        """
        batch_size = batch_size or self.config.max_batch_size
        results: List[np.ndarray] = []

        for i in range(0, len(batch), batch_size):
            chunk = batch[i : i + batch_size]
            chunk_array = np.stack(chunk)
            outputs = self.predict(chunk_array)
            results.extend([outputs[j] for j in range(len(chunk))])

        return results

    def warmup(self, num_iterations: int = 3) -> None:
        """Warm up the model with dummy inputs.

        Args:
            num_iterations: Number of warmup iterations.
        """
        self.logger.info("Running warmup...")
        input_shape = [1] + self.model.get_input_shape()
        dummy_input = np.zeros(input_shape, dtype=np.float32)

        for _ in range(num_iterations):
            self.predict(dummy_input)

        self.logger.info("Warmup complete")

    def get_stats(self) -> Dict[str, Any]:
        """Get inference statistics.

        Returns:
            Dictionary with inference stats.
        """
        avg_latency = self._total_latency / self._inference_count if self._inference_count > 0 else 0
        memory_stats = self.memory_manager.get_stats()

        return {
            "inference_count": self._inference_count,
            "total_latency_ms": self._total_latency,
            "avg_latency_ms": avg_latency,
            "device": self.device_info.device_string,
            "memory_allocated": memory_stats.allocated,
            "memory_peak": memory_stats.peak,
        }

    def reset_stats(self) -> None:
        """Reset inference statistics."""
        self._inference_count = 0
        self._total_latency = 0.0

    @classmethod
    def from_checkpoint(
        cls,
        model_class: type,
        checkpoint_path: Path | str,
        config: Optional[PredictorConfig] = None,
    ) -> "Predictor":
        """Create predictor from saved checkpoint.

        Args:
            model_class: Model class to instantiate.
            checkpoint_path: Path to model checkpoint.
            config: Predictor configuration.

        Returns:
            Initialized predictor.
        """
        model = model_class()
        model.load(checkpoint_path)
        return cls(model, config)


class StreamingPredictor(Predictor):
    """Predictor for streaming inference (e.g., text generation)."""

    def __init__(
        self,
        model: BaseModel,
        config: Optional[PredictorConfig] = None,
    ) -> None:
        super().__init__(model, config)
        self._state: Optional[Any] = None

    def predict_step(
        self,
        input_token: np.ndarray,
    ) -> np.ndarray:
        """Single-step prediction for streaming.

        Args:
            input_token: Single input token or small sequence.

        Returns:
            Next token predictions.
        """
        return self.predict(input_token)

    def reset_state(self) -> None:
        """Reset streaming state."""
        self._state = None

    def stream_generate(
        self,
        prompt: np.ndarray,
        max_tokens: int = 100,
        callback: Optional[Any] = None,
    ) -> np.ndarray:
        """Generate tokens with streaming output.

        Args:
            prompt: Initial prompt tokens.
            max_tokens: Maximum tokens to generate.
            callback: Optional callback for each token.

        Returns:
            Generated tokens.
        """
        generated = prompt.copy()

        for _ in range(max_tokens):
            logits = self.predict(generated)
            next_token = np.argmax(logits[:, -1, :], axis=-1, keepdims=True)
            generated = np.concatenate([generated, next_token], axis=1)

            if callback:
                callback(next_token[0, 0])

        return generated
