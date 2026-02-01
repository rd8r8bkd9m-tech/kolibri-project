"""Abstract base class for all Kolibri ML models."""

from __future__ import annotations

import json
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union

import numpy as np


class ModelState(Enum):
    """Model lifecycle states."""

    UNINITIALIZED = "uninitialized"
    INITIALIZED = "initialized"
    TRAINING = "training"
    TRAINED = "trained"
    EXPORTED = "exported"


@dataclass
class ModelMetadata:
    """Metadata for a trained model."""

    name: str
    version: str
    architecture: str
    input_shape: List[int]
    output_shape: List[int]
    parameters: int = 0
    device: str = "cpu"
    quantization: str = "fp32"
    extra: Dict[str, Any] = field(default_factory=dict)


class BaseModel(ABC):
    """Abstract base class for all Kolibri ML models.

    This base class provides a unified interface for:
    - Model initialization and configuration
    - Training and inference
    - Export to various formats (ONNX, WASM, CoreML, TFLite)
    - Cross-platform device support (CPU, CUDA, Metal, WASM)

    All model implementations should inherit from this class.
    """

    def __init__(
        self,
        name: str,
        device: str = "cpu",
        dtype: str = "float32",
    ) -> None:
        """Initialize base model.

        Args:
            name: Model identifier.
            device: Target device ("cpu", "cuda", "metal", "wasm").
            dtype: Data type ("float32", "float16", "int8").
        """
        self.name = name
        self.device = device
        self.dtype = dtype
        self.state = ModelState.UNINITIALIZED
        self._parameters: Dict[str, np.ndarray] = {}
        self._buffers: Dict[str, np.ndarray] = {}
        self._metadata: Optional[ModelMetadata] = None

    @abstractmethod
    def forward(self, inputs: np.ndarray) -> np.ndarray:
        """Forward pass through the model.

        Args:
            inputs: Input tensor as numpy array.

        Returns:
            Output tensor as numpy array.
        """
        pass

    @abstractmethod
    def get_input_shape(self) -> List[int]:
        """Return expected input shape (without batch dimension)."""
        pass

    @abstractmethod
    def get_output_shape(self) -> List[int]:
        """Return expected output shape (without batch dimension)."""
        pass

    def __call__(self, inputs: np.ndarray) -> np.ndarray:
        """Make model callable for inference."""
        return self.forward(inputs)

    def initialize(self, **kwargs: Any) -> None:
        """Initialize model weights.

        Override this method for custom initialization logic.
        """
        self.state = ModelState.INITIALIZED

    def num_parameters(self) -> int:
        """Count total number of trainable parameters."""
        total = 0
        for param in self._parameters.values():
            total += param.size
        return total

    def get_parameter(self, name: str) -> Optional[np.ndarray]:
        """Get parameter by name."""
        return self._parameters.get(name)

    def set_parameter(self, name: str, value: np.ndarray) -> None:
        """Set parameter value."""
        self._parameters[name] = value

    def get_buffer(self, name: str) -> Optional[np.ndarray]:
        """Get buffer (non-trainable tensor) by name."""
        return self._buffers.get(name)

    def set_buffer(self, name: str, value: np.ndarray) -> None:
        """Set buffer value."""
        self._buffers[name] = value

    def parameters(self) -> Dict[str, np.ndarray]:
        """Return all trainable parameters."""
        return dict(self._parameters)

    def buffers(self) -> Dict[str, np.ndarray]:
        """Return all non-trainable buffers."""
        return dict(self._buffers)

    def to_device(self, device: str) -> "BaseModel":
        """Move model to specified device.

        Args:
            device: Target device.

        Returns:
            Self for chaining.
        """
        self.device = device
        return self

    def get_metadata(self) -> ModelMetadata:
        """Get model metadata."""
        if self._metadata is None:
            self._metadata = ModelMetadata(
                name=self.name,
                version="1.0.0",
                architecture=self.__class__.__name__,
                input_shape=self.get_input_shape(),
                output_shape=self.get_output_shape(),
                parameters=self.num_parameters(),
                device=self.device,
                quantization="fp32" if self.dtype == "float32" else self.dtype,
            )
        return self._metadata

    def save(self, path: Path | str) -> None:
        """Save model weights and config to file.

        Args:
            path: Output file path (without extension).
        """
        save_path = Path(path)
        save_path.parent.mkdir(parents=True, exist_ok=True)

        # Save parameters
        np.savez(
            save_path.with_suffix(".npz"),
            **{f"param_{k}": v for k, v in self._parameters.items()},
            **{f"buffer_{k}": v for k, v in self._buffers.items()},
        )

        # Save metadata
        metadata = self.get_metadata()
        meta_dict = {
            "name": metadata.name,
            "version": metadata.version,
            "architecture": metadata.architecture,
            "input_shape": metadata.input_shape,
            "output_shape": metadata.output_shape,
            "parameters": metadata.parameters,
            "device": metadata.device,
            "quantization": metadata.quantization,
            "extra": metadata.extra,
        }
        with open(save_path.with_suffix(".json"), "w", encoding="utf-8") as f:
            json.dump(meta_dict, f, ensure_ascii=False, indent=2)

    def load(self, path: Path | str) -> None:
        """Load model weights from file.

        Args:
            path: Input file path (without extension).
        """
        load_path = Path(path)

        # Load parameters
        data = np.load(load_path.with_suffix(".npz"))
        for key in data.files:
            if key.startswith("param_"):
                param_name = key[6:]  # Remove "param_" prefix
                self._parameters[param_name] = data[key]
            elif key.startswith("buffer_"):
                buffer_name = key[7:]  # Remove "buffer_" prefix
                self._buffers[buffer_name] = data[key]

        # Load metadata
        meta_path = load_path.with_suffix(".json")
        if meta_path.exists():
            with open(meta_path, encoding="utf-8") as f:
                meta_dict = json.load(f)
            self._metadata = ModelMetadata(
                name=meta_dict.get("name", self.name),
                version=meta_dict.get("version", "1.0.0"),
                architecture=meta_dict.get("architecture", self.__class__.__name__),
                input_shape=meta_dict.get("input_shape", []),
                output_shape=meta_dict.get("output_shape", []),
                parameters=meta_dict.get("parameters", 0),
                device=meta_dict.get("device", "cpu"),
                quantization=meta_dict.get("quantization", "fp32"),
                extra=meta_dict.get("extra", {}),
            )

        self.state = ModelState.TRAINED

    def quantize(self, mode: str = "int8") -> "BaseModel":
        """Quantize model weights for faster inference.

        Args:
            mode: Quantization mode ("int8", "int4", "fp16").

        Returns:
            Self for chaining.
        """
        if mode == "fp16":
            for name, param in self._parameters.items():
                self._parameters[name] = param.astype(np.float16)
            self.dtype = "float16"

        elif mode == "int8":
            for name, param in self._parameters.items():
                if param.dtype in (np.float32, np.float64):
                    # Simple min-max quantization
                    min_val = param.min()
                    max_val = param.max()
                    scale = (max_val - min_val) / 255.0 if max_val != min_val else 1.0
                    quantized = ((param - min_val) / scale).astype(np.int8)
                    self._parameters[name] = quantized
                    # Store scale and zero point as buffers
                    self._buffers[f"{name}_scale"] = np.array([scale], dtype=np.float32)
                    self._buffers[f"{name}_zero"] = np.array([min_val], dtype=np.float32)
            self.dtype = "int8"

        return self

    def export_to_dict(self) -> Dict[str, Any]:
        """Export model to dictionary format for serialization."""
        return {
            "name": self.name,
            "architecture": self.__class__.__name__,
            "device": self.device,
            "dtype": self.dtype,
            "state": self.state.value,
            "parameters": {k: v.tolist() for k, v in self._parameters.items()},
            "buffers": {k: v.tolist() for k, v in self._buffers.items()},
            "metadata": self.get_metadata().__dict__ if self._metadata else None,
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "BaseModel":
        """Create model from dictionary.

        Note: Subclasses should override this for proper instantiation.
        """
        raise NotImplementedError("Subclasses must implement from_dict")

    def summary(self) -> str:
        """Return model summary string."""
        lines = [
            f"Model: {self.name}",
            f"Architecture: {self.__class__.__name__}",
            f"Device: {self.device}",
            f"Dtype: {self.dtype}",
            f"State: {self.state.value}",
            f"Input Shape: {self.get_input_shape()}",
            f"Output Shape: {self.get_output_shape()}",
            f"Parameters: {self.num_parameters():,}",
            "",
            "Layers:",
        ]
        for name, param in self._parameters.items():
            lines.append(f"  {name}: {param.shape} ({param.dtype})")
        return "\n".join(lines)
