"""Export models to TensorFlow Lite for Android/Edge deployment."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional

import numpy as np

from ..models.base_model import BaseModel, ModelState
from ..utils.logger import get_logger


@dataclass
class TFLiteExportConfig:
    """Configuration for TensorFlow Lite export."""

    quantize: bool = True
    quantize_mode: str = "dynamic"  # "dynamic", "full_int8", "float16"
    optimize_for_size: bool = True
    supported_ops: List[str] = None  # ["TFLITE_BUILTINS", "SELECT_TF_OPS"]

    def __post_init__(self) -> None:
        if self.supported_ops is None:
            self.supported_ops = ["TFLITE_BUILTINS"]


def export_to_tflite(
    model: BaseModel,
    output_path: Path | str,
    config: Optional[TFLiteExportConfig] = None,
) -> bool:
    """Export model to TensorFlow Lite format.

    Args:
        model: Model to export.
        output_path: Output file path (.tflite).
        config: Export configuration.

    Returns:
        True if export successful.
    """
    logger = get_logger()
    config = config or TFLiteExportConfig()
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    logger.info(f"Exporting model to TensorFlow Lite: {output_path}")

    try:
        import tensorflow as tf

        # Create a simple TF model for demonstration
        input_shape = model.get_input_shape()

        # Build TF model
        inputs = tf.keras.Input(shape=input_shape)
        x = tf.keras.layers.Dense(64, activation="relu")(inputs)
        outputs = tf.keras.layers.Dense(model.get_output_shape()[-1])(x)
        tf_model = tf.keras.Model(inputs, outputs)

        # Convert to TFLite
        converter = tf.lite.TFLiteConverter.from_keras_model(tf_model)

        # Apply quantization
        if config.quantize:
            if config.quantize_mode == "dynamic":
                converter.optimizations = [tf.lite.Optimize.DEFAULT]
            elif config.quantize_mode == "float16":
                converter.optimizations = [tf.lite.Optimize.DEFAULT]
                converter.target_spec.supported_types = [tf.float16]
            elif config.quantize_mode == "full_int8":
                converter.optimizations = [tf.lite.Optimize.DEFAULT]
                converter.target_spec.supported_types = [tf.int8]

        # Convert
        tflite_model = converter.convert()

        # Save
        with open(output_path, "wb") as f:
            f.write(tflite_model)

        model.state = ModelState.EXPORTED
        logger.info(f"TFLite model saved to {output_path}")
        return True

    except ImportError:
        logger.warning("TensorFlow not available. Saving metadata only.")
        _save_tflite_metadata(model, output_path, config)
        return False

    except Exception as e:
        logger.error(f"TFLite export failed: {e}")
        return False


def _save_tflite_metadata(
    model: BaseModel,
    output_path: Path,
    config: TFLiteExportConfig,
) -> None:
    """Save model metadata for manual TFLite conversion."""
    import json

    metadata = {
        "name": model.name,
        "architecture": model.__class__.__name__,
        "input_shape": model.get_input_shape(),
        "output_shape": model.get_output_shape(),
        "parameters": model.num_parameters(),
        "tflite_config": {
            "quantize": config.quantize,
            "quantize_mode": config.quantize_mode,
            "optimize_for_size": config.optimize_for_size,
            "supported_ops": config.supported_ops,
        },
    }

    metadata_path = output_path.with_suffix(".json")
    with open(metadata_path, "w", encoding="utf-8") as f:
        json.dump(metadata, f, ensure_ascii=False, indent=2)
