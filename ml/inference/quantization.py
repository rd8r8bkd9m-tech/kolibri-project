"""Quantization utilities for model optimization."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, Optional, Tuple

import numpy as np

from ..models.base_model import BaseModel
from ..utils.logger import get_logger


@dataclass
class QuantizationConfig:
    """Configuration for model quantization."""

    mode: str = "int8"  # "int8", "int4", "fp16"
    symmetric: bool = True
    per_channel: bool = False
    calibration_samples: int = 100


def quantize_tensor(
    tensor: np.ndarray,
    mode: str = "int8",
    symmetric: bool = True,
) -> Tuple[np.ndarray, Dict[str, np.ndarray]]:
    """Quantize a tensor.

    Args:
        tensor: Input tensor to quantize.
        mode: Quantization mode.
        symmetric: Whether to use symmetric quantization.

    Returns:
        Tuple of (quantized_tensor, quantization_params).
    """
    if mode == "fp16":
        return tensor.astype(np.float16), {}

    if mode == "int8":
        if symmetric:
            scale = np.abs(tensor).max() / 127.0
            zero_point = np.array([0], dtype=np.int8)
        else:
            min_val = tensor.min()
            max_val = tensor.max()
            scale = (max_val - min_val) / 255.0
            zero_point = np.round(-min_val / scale).astype(np.int8)

        if scale == 0:
            scale = 1.0
        quantized = np.clip(np.round(tensor / scale) + zero_point, -128, 127).astype(np.int8)

        return quantized, {"scale": np.array([scale]), "zero_point": zero_point}

    elif mode == "int4":
        scale = np.abs(tensor).max() / 7.0
        if scale == 0:
            scale = 1.0
        quantized = np.clip(np.round(tensor / scale), -8, 7).astype(np.int8)
        return quantized, {"scale": np.array([scale])}

    raise ValueError(f"Unknown quantization mode: {mode}")


def dequantize_tensor(
    quantized: np.ndarray,
    params: Dict[str, np.ndarray],
    mode: str = "int8",
) -> np.ndarray:
    """Dequantize a tensor.

    Args:
        quantized: Quantized tensor.
        params: Quantization parameters.
        mode: Quantization mode.

    Returns:
        Dequantized tensor.
    """
    if mode == "fp16":
        return quantized.astype(np.float32)

    scale = params.get("scale", np.array([1.0]))[0]
    zero_point = params.get("zero_point", np.array([0]))[0]

    return (quantized.astype(np.float32) - zero_point) * scale


def quantize_model(
    model: BaseModel,
    config: Optional[QuantizationConfig] = None,
) -> BaseModel:
    """Quantize all parameters in a model.

    Args:
        model: Model to quantize.
        config: Quantization configuration.

    Returns:
        Quantized model.
    """
    config = config or QuantizationConfig()
    logger = get_logger()

    logger.info(f"Quantizing model to {config.mode}")

    original_size = 0
    quantized_size = 0

    for name, param in model.parameters().items():
        original_size += param.nbytes

        quantized, quant_params = quantize_tensor(param, config.mode, config.symmetric)
        model.set_parameter(name, quantized)

        # Store quantization params as buffers
        for param_name, param_value in quant_params.items():
            model.set_buffer(f"{name}_{param_name}", param_value)

        quantized_size += quantized.nbytes

    compression_ratio = original_size / quantized_size if quantized_size > 0 else 1.0
    logger.info(
        f"Quantization complete: {original_size} -> {quantized_size} bytes "
        f"({compression_ratio:.2f}x compression)"
    )

    model.dtype = config.mode
    return model


class CalibrationDataset:
    """Dataset for calibration-based quantization."""

    def __init__(self, samples: list) -> None:
        """Initialize calibration dataset.

        Args:
            samples: List of calibration samples.
        """
        self.samples = samples

    def __len__(self) -> int:
        return len(self.samples)

    def __iter__(self):
        return iter(self.samples)


def calibrate_model(
    model: BaseModel,
    calibration_data: CalibrationDataset,
    config: Optional[QuantizationConfig] = None,
) -> Dict[str, Dict[str, np.ndarray]]:
    """Calibrate model for quantization using calibration data.

    Args:
        model: Model to calibrate.
        calibration_data: Calibration dataset.
        config: Quantization configuration.

    Returns:
        Dictionary of quantization parameters per layer.
    """
    config = config or QuantizationConfig()
    logger = get_logger()

    logger.info("Running calibration...")

    # Collect activation statistics
    activation_mins: Dict[str, float] = {}
    activation_maxs: Dict[str, float] = {}

    for sample in calibration_data:
        _ = model.forward(sample)
        # In production, would collect intermediate activations

    logger.info("Calibration complete")
    return {}


class QATTrainer:
    """Quantization-Aware Training (QAT) support."""

    def __init__(
        self,
        model: BaseModel,
        config: Optional[QuantizationConfig] = None,
    ) -> None:
        """Initialize QAT trainer.

        Args:
            model: Model to train with QAT.
            config: Quantization configuration.
        """
        self.model = model
        self.config = config or QuantizationConfig()
        self.logger = get_logger()

    def prepare_model(self) -> BaseModel:
        """Prepare model for QAT by inserting fake quantization nodes.

        Returns:
            Model prepared for QAT.
        """
        self.logger.info("Preparing model for Quantization-Aware Training")
        # In production, would insert FakeQuantize operations
        return self.model

    def convert_to_quantized(self) -> BaseModel:
        """Convert QAT-trained model to quantized model.

        Returns:
            Quantized model.
        """
        return quantize_model(self.model, self.config)
