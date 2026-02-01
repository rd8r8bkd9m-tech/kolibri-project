"""ONNX Runtime integration for cross-platform inference."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, List, Optional, Union

import numpy as np

from ..utils.logger import get_logger


def is_onnx_available() -> bool:
    """Check if ONNX Runtime is available."""
    try:
        import onnxruntime  # noqa: F401

        return True
    except ImportError:
        return False


class ONNXPredictor:
    """Predictor using ONNX Runtime for optimized inference.

    Supports:
    - CPU and GPU execution providers
    - Graph optimization
    - Multi-threading
    - Dynamic batching
    """

    def __init__(
        self,
        model_path: Path | str,
        device: str = "cpu",
        num_threads: int = 4,
        optimize: bool = True,
    ) -> None:
        """Initialize ONNX predictor.

        Args:
            model_path: Path to ONNX model file.
            device: Target device ("cpu", "cuda").
            num_threads: Number of inference threads.
            optimize: Whether to enable graph optimization.
        """
        self.model_path = Path(model_path)
        self.device = device
        self.num_threads = num_threads
        self.optimize = optimize
        self.logger = get_logger()

        self._session: Optional[Any] = None
        self._input_names: List[str] = []
        self._output_names: List[str] = []

        if not is_onnx_available():
            raise ImportError("ONNX Runtime not available. Install with: pip install onnxruntime")

        self._load_model()

    def _load_model(self) -> None:
        """Load ONNX model."""
        import onnxruntime as ort

        # Configure session options
        sess_options = ort.SessionOptions()
        sess_options.intra_op_num_threads = self.num_threads
        sess_options.inter_op_num_threads = self.num_threads

        if self.optimize:
            sess_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL

        # Select execution provider
        providers = self._get_providers()

        self.logger.info(f"Loading ONNX model from {self.model_path}")
        self._session = ort.InferenceSession(
            str(self.model_path),
            sess_options=sess_options,
            providers=providers,
        )

        # Get input/output names
        self._input_names = [inp.name for inp in self._session.get_inputs()]
        self._output_names = [out.name for out in self._session.get_outputs()]

        self.logger.info(
            f"Model loaded: inputs={self._input_names}, outputs={self._output_names}"
        )

    def _get_providers(self) -> List[str]:
        """Get execution providers based on device."""
        import onnxruntime as ort

        available = ort.get_available_providers()

        if self.device == "cuda" and "CUDAExecutionProvider" in available:
            return ["CUDAExecutionProvider", "CPUExecutionProvider"]
        elif self.device == "cuda" and "TensorrtExecutionProvider" in available:
            return ["TensorrtExecutionProvider", "CPUExecutionProvider"]

        return ["CPUExecutionProvider"]

    def predict(
        self,
        inputs: Union[np.ndarray, Dict[str, np.ndarray]],
    ) -> np.ndarray:
        """Run inference.

        Args:
            inputs: Input array or dictionary of named inputs.

        Returns:
            Model output.
        """
        if self._session is None:
            raise RuntimeError("Model not loaded")

        # Prepare inputs
        if isinstance(inputs, np.ndarray):
            feed = {self._input_names[0]: inputs}
        else:
            feed = inputs

        # Run inference
        outputs = self._session.run(self._output_names, feed)

        # Return first output if single output
        if len(outputs) == 1:
            return outputs[0]
        return outputs

    def get_input_details(self) -> List[Dict[str, Any]]:
        """Get input tensor details."""
        if self._session is None:
            return []

        details = []
        for inp in self._session.get_inputs():
            details.append(
                {
                    "name": inp.name,
                    "shape": inp.shape,
                    "dtype": inp.type,
                }
            )
        return details

    def get_output_details(self) -> List[Dict[str, Any]]:
        """Get output tensor details."""
        if self._session is None:
            return []

        details = []
        for out in self._session.get_outputs():
            details.append(
                {
                    "name": out.name,
                    "shape": out.shape,
                    "dtype": out.type,
                }
            )
        return details

    def benchmark(
        self,
        input_shape: List[int],
        num_iterations: int = 100,
        warmup: int = 10,
    ) -> Dict[str, float]:
        """Run inference benchmark.

        Args:
            input_shape: Shape of input tensor.
            num_iterations: Number of benchmark iterations.
            warmup: Number of warmup iterations.

        Returns:
            Dictionary with benchmark results.
        """
        import time

        dummy_input = np.random.randn(*input_shape).astype(np.float32)

        # Warmup
        for _ in range(warmup):
            self.predict(dummy_input)

        # Benchmark
        latencies = []
        for _ in range(num_iterations):
            start = time.perf_counter()
            self.predict(dummy_input)
            latencies.append((time.perf_counter() - start) * 1000)

        return {
            "mean_latency_ms": float(np.mean(latencies)),
            "std_latency_ms": float(np.std(latencies)),
            "min_latency_ms": float(np.min(latencies)),
            "max_latency_ms": float(np.max(latencies)),
            "p50_latency_ms": float(np.percentile(latencies, 50)),
            "p95_latency_ms": float(np.percentile(latencies, 95)),
            "p99_latency_ms": float(np.percentile(latencies, 99)),
            "throughput_samples_per_sec": 1000 / float(np.mean(latencies)),
        }


class ONNXModelValidator:
    """Validator for ONNX models."""

    def __init__(self, model_path: Path | str) -> None:
        """Initialize validator.

        Args:
            model_path: Path to ONNX model.
        """
        self.model_path = Path(model_path)

    def validate(self) -> Dict[str, Any]:
        """Validate ONNX model.

        Returns:
            Validation results.
        """
        try:
            import onnx
        except ImportError:
            return {"valid": False, "error": "ONNX package not available"}

        try:
            model = onnx.load(str(self.model_path))
            onnx.checker.check_model(model)
            return {
                "valid": True,
                "ir_version": model.ir_version,
                "opset_version": model.opset_import[0].version if model.opset_import else None,
                "producer_name": model.producer_name,
                "producer_version": model.producer_version,
            }
        except Exception as e:
            return {"valid": False, "error": str(e)}
