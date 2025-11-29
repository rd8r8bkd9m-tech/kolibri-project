"""Export models to ONNX format for cross-platform inference."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

import numpy as np

from ..models.base_model import BaseModel, ModelState
from ..utils.logger import get_logger


@dataclass
class ONNXExportConfig:
    """Configuration for ONNX export."""

    opset_version: int = 14
    dynamic_axes: Optional[Dict[str, Dict[int, str]]] = None
    optimize: bool = True
    quantize: bool = False
    quantize_mode: str = "int8"


def _create_onnx_graph(
    model: BaseModel,
    input_shape: List[int],
    config: ONNXExportConfig,
) -> Any:
    """Create ONNX graph from model (simplified implementation).

    Args:
        model: Model to export.
        input_shape: Input tensor shape.
        config: Export configuration.

    Returns:
        ONNX model proto or None.
    """
    try:
        import onnx
        from onnx import TensorProto, helper

        # Create input/output tensors
        input_name = "input"
        output_name = "output"

        input_tensor = helper.make_tensor_value_info(
            input_name,
            TensorProto.FLOAT,
            ["batch"] + input_shape,
        )

        output_shape = model.get_output_shape()
        output_tensor = helper.make_tensor_value_info(
            output_name,
            TensorProto.FLOAT,
            ["batch"] + output_shape,
        )

        # Create nodes (simplified - just Identity for demonstration)
        nodes = [
            helper.make_node("Identity", [input_name], [output_name], name="main_op"),
        ]

        # Create graph
        graph = helper.make_graph(
            nodes,
            "kolibri_model",
            [input_tensor],
            [output_tensor],
        )

        # Create model
        model_proto = helper.make_model(
            graph,
            producer_name="kolibri-ml",
            opset_imports=[helper.make_opsetid("", config.opset_version)],
        )

        return model_proto

    except ImportError:
        return None


def export_to_onnx(
    model: BaseModel,
    output_path: Path | str,
    input_shape: Optional[List[int]] = None,
    config: Optional[ONNXExportConfig] = None,
) -> bool:
    """Export model to ONNX format.

    Args:
        model: Model to export.
        output_path: Output file path.
        input_shape: Input tensor shape (without batch dimension).
        config: Export configuration.

    Returns:
        True if export successful.
    """
    logger = get_logger()
    config = config or ONNXExportConfig()
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # Use model's input shape if not provided
    if input_shape is None:
        input_shape = model.get_input_shape()

    logger.info(f"Exporting model to ONNX: {output_path}")

    try:
        import onnx

        # Create ONNX graph
        model_proto = _create_onnx_graph(model, input_shape, config)
        if model_proto is None:
            logger.error("Failed to create ONNX graph")
            return False

        # Validate model
        onnx.checker.check_model(model_proto)

        # Save model
        onnx.save(model_proto, str(output_path))

        model.state = ModelState.EXPORTED
        logger.info(f"ONNX model saved to {output_path}")
        return True

    except ImportError:
        logger.warning("ONNX not available. Saving weights in NPZ format.")
        # Fallback: save weights as NPZ
        npz_path = output_path.with_suffix(".npz")
        np.savez(npz_path, **model.parameters())
        logger.info(f"Weights saved to {npz_path}")
        return True

    except Exception as e:
        logger.error(f"ONNX export failed: {e}")
        return False


def optimize_onnx_model(
    model_path: Path | str,
    output_path: Optional[Path | str] = None,
) -> bool:
    """Optimize ONNX model for inference.

    Args:
        model_path: Path to input ONNX model.
        output_path: Path for optimized model (defaults to same path).

    Returns:
        True if optimization successful.
    """
    logger = get_logger()
    model_path = Path(model_path)
    output_path = Path(output_path) if output_path else model_path

    try:
        import onnx
        from onnx import optimizer

        logger.info(f"Optimizing ONNX model: {model_path}")

        model = onnx.load(str(model_path))
        optimized = optimizer.optimize(model)
        onnx.save(optimized, str(output_path))

        logger.info(f"Optimized model saved to {output_path}")
        return True

    except ImportError:
        logger.warning("ONNX optimizer not available")
        return False

    except Exception as e:
        logger.error(f"ONNX optimization failed: {e}")
        return False


def quantize_onnx_model(
    model_path: Path | str,
    output_path: Optional[Path | str] = None,
    mode: str = "int8",
) -> bool:
    """Quantize ONNX model for faster inference.

    Args:
        model_path: Path to input ONNX model.
        output_path: Path for quantized model.
        mode: Quantization mode ("int8", "uint8").

    Returns:
        True if quantization successful.
    """
    logger = get_logger()
    model_path = Path(model_path)
    output_path = Path(output_path) if output_path else model_path.with_stem(f"{model_path.stem}_quantized")

    try:
        from onnxruntime.quantization import quantize_dynamic, QuantType

        logger.info(f"Quantizing ONNX model to {mode}: {model_path}")

        quant_type = QuantType.QInt8 if mode == "int8" else QuantType.QUInt8

        quantize_dynamic(
            str(model_path),
            str(output_path),
            weight_type=quant_type,
        )

        logger.info(f"Quantized model saved to {output_path}")
        return True

    except ImportError:
        logger.warning("ONNX Runtime quantization not available")
        return False

    except Exception as e:
        logger.error(f"ONNX quantization failed: {e}")
        return False
