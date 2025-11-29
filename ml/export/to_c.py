"""Export models to C code for embedded deployment."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional

import numpy as np

from ..models.base_model import BaseModel, ModelState
from ..utils.logger import get_logger


@dataclass
class CExportConfig:
    """Configuration for C code export."""

    use_simd: bool = True
    quantize: bool = True
    quantize_bits: int = 8
    static_allocation: bool = True
    include_inference: bool = True


def export_to_c(
    model: BaseModel,
    output_dir: Path | str,
    config: Optional[CExportConfig] = None,
) -> bool:
    """Export model to C code for embedded systems.

    Creates:
    - model_weights.h: Weight arrays
    - model_config.h: Model configuration
    - model_inference.h/.c: Inference functions

    Args:
        model: Model to export.
        output_dir: Output directory.
        config: Export configuration.

    Returns:
        True if export successful.
    """
    logger = get_logger()
    config = config or CExportConfig()
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    logger.info(f"Exporting model to C code: {output_dir}")

    try:
        # Generate weight header
        _generate_weights_header(model, output_dir, config)

        # Generate config header
        _generate_config_header(model, output_dir, config)

        # Generate inference code
        if config.include_inference:
            _generate_inference_code(model, output_dir, config)

        model.state = ModelState.EXPORTED
        logger.info(f"C code export complete: {output_dir}")
        return True

    except Exception as e:
        logger.error(f"C export failed: {e}")
        return False


def _generate_weights_header(
    model: BaseModel,
    output_dir: Path,
    config: CExportConfig,
) -> None:
    """Generate C header with model weights."""
    header_path = output_dir / "model_weights.h"

    lines = [
        "/**",
        f" * Kolibri ML - {model.name} Weights",
        " * Auto-generated - do not edit",
        " */",
        "",
        "#ifndef KOLIBRI_MODEL_WEIGHTS_H",
        "#define KOLIBRI_MODEL_WEIGHTS_H",
        "",
        "#include <stdint.h>",
        "",
    ]

    # Export each parameter
    for name, param in model.parameters().items():
        safe_name = name.replace(".", "_").replace("-", "_")

        if config.quantize:
            # Quantize to int8
            scale = np.abs(param).max() / 127.0
            if scale == 0:
                scale = 1.0
            quantized = np.clip(np.round(param / scale), -128, 127).astype(np.int8)

            # Write scale
            lines.append(f"static const float {safe_name}_scale = {scale}f;")

            # Write quantized data
            flat = quantized.flatten()
            lines.append(f"static const int8_t {safe_name}_data[{len(flat)}] = {{")
            for i in range(0, len(flat), 16):
                chunk = flat[i : i + 16]
                chunk_str = ", ".join(str(v) for v in chunk)
                lines.append(f"    {chunk_str},")
            lines.append("};")
        else:
            # Float32 weights
            flat = param.astype(np.float32).flatten()
            lines.append(f"static const float {safe_name}_data[{len(flat)}] = {{")
            for i in range(0, len(flat), 8):
                chunk = flat[i : i + 8]
                chunk_str = ", ".join(f"{v}f" for v in chunk)
                lines.append(f"    {chunk_str},")
            lines.append("};")

        # Write shape info
        shape = list(param.shape)
        lines.append(f"static const int {safe_name}_shape[{len(shape)}] = {{{', '.join(map(str, shape))}}}; ")
        lines.append(f"static const int {safe_name}_ndim = {len(shape)};")
        lines.append("")

    lines.append("#endif /* KOLIBRI_MODEL_WEIGHTS_H */")

    with open(header_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def _generate_config_header(
    model: BaseModel,
    output_dir: Path,
    config: CExportConfig,
) -> None:
    """Generate C header with model configuration."""
    header_path = output_dir / "model_config.h"

    input_shape = model.get_input_shape()
    output_shape = model.get_output_shape()

    lines = [
        "/**",
        f" * Kolibri ML - {model.name} Configuration",
        " * Auto-generated - do not edit",
        " */",
        "",
        "#ifndef KOLIBRI_MODEL_CONFIG_H",
        "#define KOLIBRI_MODEL_CONFIG_H",
        "",
        f'#define KOLIBRI_MODEL_NAME "{model.name}"',
        f'#define KOLIBRI_MODEL_ARCH "{model.__class__.__name__}"',
        f"#define KOLIBRI_NUM_PARAMETERS {model.num_parameters()}",
        "",
        f"#define KOLIBRI_INPUT_NDIM {len(input_shape)}",
        f"#define KOLIBRI_OUTPUT_NDIM {len(output_shape)}",
        "",
    ]

    # Input shape
    for i, dim in enumerate(input_shape):
        lines.append(f"#define KOLIBRI_INPUT_DIM_{i} {dim}")

    # Output shape
    for i, dim in enumerate(output_shape):
        lines.append(f"#define KOLIBRI_OUTPUT_DIM_{i} {dim}")

    # Quantization config
    lines.append("")
    lines.append(f"#define KOLIBRI_QUANTIZED {1 if config.quantize else 0}")
    if config.quantize:
        lines.append(f"#define KOLIBRI_QUANT_BITS {config.quantize_bits}")

    lines.append("")
    lines.append("#endif /* KOLIBRI_MODEL_CONFIG_H */")

    with open(header_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def _generate_inference_code(
    model: BaseModel,
    output_dir: Path,
    config: CExportConfig,
) -> None:
    """Generate C inference code."""
    # Header file
    header_path = output_dir / "model_inference.h"
    source_path = output_dir / "model_inference.c"

    header_lines = [
        "/**",
        f" * Kolibri ML - {model.name} Inference",
        " * Auto-generated - do not edit",
        " */",
        "",
        "#ifndef KOLIBRI_MODEL_INFERENCE_H",
        "#define KOLIBRI_MODEL_INFERENCE_H",
        "",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "#include \"model_config.h\"",
        "",
        "/* Initialize model resources */",
        "int kolibri_model_init(void);",
        "",
        "/* Run inference */",
        "int kolibri_model_predict(const float* input, float* output);",
        "",
        "/* Cleanup model resources */",
        "void kolibri_model_cleanup(void);",
        "",
        "#endif /* KOLIBRI_MODEL_INFERENCE_H */",
    ]

    source_lines = [
        "/**",
        f" * Kolibri ML - {model.name} Inference Implementation",
        " * Auto-generated - do not edit",
        " */",
        "",
        "#include \"model_inference.h\"",
        "#include \"model_weights.h\"",
        "#include <string.h>",
        "#include <math.h>",
        "",
        "int kolibri_model_init(void) {",
        "    /* Model weights are statically allocated */",
        "    return 0;",
        "}",
        "",
        "static inline float relu(float x) {",
        "    return x > 0.0f ? x : 0.0f;",
        "}",
        "",
        "int kolibri_model_predict(const float* input, float* output) {",
        "    /* Simplified forward pass - implement based on model architecture */",
        "    /* This is a placeholder that copies input to output */",
        "    size_t input_size = 1;",
        "    for (int i = 0; i < KOLIBRI_INPUT_NDIM; i++) {",
        "        /* Multiply by input dimensions */",
        "    }",
        "    memcpy(output, input, input_size * sizeof(float));",
        "    return 0;",
        "}",
        "",
        "void kolibri_model_cleanup(void) {",
        "    /* Nothing to clean up with static allocation */",
        "}",
    ]

    with open(header_path, "w", encoding="utf-8") as f:
        f.write("\n".join(header_lines))

    with open(source_path, "w", encoding="utf-8") as f:
        f.write("\n".join(source_lines))
