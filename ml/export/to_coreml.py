"""Export models to CoreML format for iOS/macOS deployment."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional

import numpy as np

from ..models.base_model import BaseModel, ModelState
from ..utils.logger import get_logger


@dataclass
class CoreMLExportConfig:
    """Configuration for CoreML export."""

    minimum_deployment_target: str = "iOS15"  # iOS14, iOS15, iOS16, etc.
    compute_units: str = "ALL"  # ALL, CPU_ONLY, CPU_AND_GPU, CPU_AND_NE
    convert_to_float16: bool = True
    use_neural_engine: bool = True


def export_to_coreml(
    model: BaseModel,
    output_path: Path | str,
    config: Optional[CoreMLExportConfig] = None,
) -> bool:
    """Export model to CoreML format.

    Args:
        model: Model to export.
        output_path: Output file path (.mlmodel or .mlpackage).
        config: Export configuration.

    Returns:
        True if export successful.
    """
    logger = get_logger()
    config = config or CoreMLExportConfig()
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    logger.info(f"Exporting model to CoreML: {output_path}")

    try:
        import coremltools as ct

        # Create a simple spec for demonstration
        # In production, would convert from ONNX or PyTorch
        input_shape = model.get_input_shape()
        output_shape = model.get_output_shape()

        # Build CoreML model spec
        input_features = [
            ("input", ct.TensorType(shape=[1] + input_shape))
        ]

        # For now, create a simple identity model
        # Real implementation would convert model architecture
        logger.info("Creating CoreML model spec...")

        # Try to convert via ONNX if available
        from .to_onnx import export_to_onnx
        import tempfile

        with tempfile.NamedTemporaryFile(suffix=".onnx", delete=False) as tmp:
            onnx_path = Path(tmp.name)

        if export_to_onnx(model, onnx_path):
            coreml_model = ct.convert(
                str(onnx_path),
                minimum_deployment_target=getattr(ct.target, config.minimum_deployment_target),
                convert_to=config.compute_units.lower(),
            )

            # Save model
            coreml_model.save(str(output_path))
            onnx_path.unlink()

            model.state = ModelState.EXPORTED
            logger.info(f"CoreML model saved to {output_path}")
            return True

        return False

    except ImportError:
        logger.warning("CoreML tools not available. Saving metadata only.")
        _save_coreml_metadata(model, output_path, config)
        return False

    except Exception as e:
        logger.error(f"CoreML export failed: {e}")
        return False


def _save_coreml_metadata(
    model: BaseModel,
    output_path: Path,
    config: CoreMLExportConfig,
) -> None:
    """Save model metadata for manual CoreML conversion."""
    import json

    metadata = {
        "name": model.name,
        "architecture": model.__class__.__name__,
        "input_shape": model.get_input_shape(),
        "output_shape": model.get_output_shape(),
        "parameters": model.num_parameters(),
        "coreml_config": config.__dict__,
    }

    metadata_path = output_path.with_suffix(".json")
    with open(metadata_path, "w", encoding="utf-8") as f:
        json.dump(metadata, f, ensure_ascii=False, indent=2)
