"""Kolibri ML Module - Machine Learning infrastructure for cross-platform inference.

This module provides a unified ML framework that works across:
- CPU (fallback for any platform)
- GPU (CUDA/Metal acceleration)
- WebAssembly (browser deployment)
- Mobile (CoreML/TFLite)

Main components:
- models: Neural network architectures (TransformerLite, NeuralCompressor, etc.)
- training: Unified trainer with GPU/CPU support
- inference: Cross-platform predictors and batch processing
- export: Model export to ONNX, WASM, CoreML, TFLite
- integration: Bridges to existing Kolibri components
- utils: Device detection, memory management, logging
"""

from __future__ import annotations

__version__ = "0.1.0"
__all__ = [
    "get_device",
    "MLConfig",
    "load_config",
]

from .utils.config import MLConfig, load_config
from .utils.device_detector import get_device
