"""Export models to WebAssembly for browser deployment."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional

import numpy as np

from ..models.base_model import BaseModel, ModelState
from ..utils.logger import get_logger


@dataclass
class WASMExportConfig:
    """Configuration for WebAssembly export."""

    target: str = "wasm32"  # "wasm32" or "wasm64"
    simd: bool = True
    threads: bool = False
    optimize_size: bool = True
    embed_weights: bool = False
    runtime: str = "onnx"  # "onnx", "tensorflow", "custom"


def export_to_wasm(
    model: BaseModel,
    output_dir: Path | str,
    config: Optional[WASMExportConfig] = None,
) -> bool:
    """Export model for WebAssembly deployment.

    Creates files necessary for running the model in a browser:
    - model.json: Model configuration and metadata
    - weights.bin: Model weights in binary format
    - inference.js: JavaScript wrapper for inference

    Args:
        model: Model to export.
        output_dir: Output directory.
        config: Export configuration.

    Returns:
        True if export successful.
    """
    logger = get_logger()
    config = config or WASMExportConfig()
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    logger.info(f"Exporting model for WebAssembly: {output_dir}")

    try:
        # Export model configuration
        model_config = {
            "name": model.name,
            "architecture": model.__class__.__name__,
            "input_shape": model.get_input_shape(),
            "output_shape": model.get_output_shape(),
            "dtype": model.dtype,
            "parameters": model.num_parameters(),
            "config": config.__dict__,
        }

        config_path = output_dir / "model.json"
        with open(config_path, "w", encoding="utf-8") as f:
            json.dump(model_config, f, ensure_ascii=False, indent=2)

        # Export weights
        weights_path = output_dir / "weights.bin"
        _export_weights_binary(model, weights_path)

        # Create JavaScript wrapper
        js_path = output_dir / "inference.js"
        _create_js_wrapper(model, js_path, config)

        # Create HTML demo
        html_path = output_dir / "demo.html"
        _create_html_demo(model, html_path)

        model.state = ModelState.EXPORTED
        logger.info(f"WebAssembly export complete: {output_dir}")
        return True

    except Exception as e:
        logger.error(f"WebAssembly export failed: {e}")
        return False


def _export_weights_binary(model: BaseModel, output_path: Path) -> None:
    """Export model weights to binary format."""
    with open(output_path, "wb") as f:
        # Write header with parameter info
        params = model.parameters()
        header = {
            "num_parameters": len(params),
            "parameter_names": list(params.keys()),
            "shapes": {name: list(p.shape) for name, p in params.items()},
            "dtypes": {name: str(p.dtype) for name, p in params.items()},
        }
        header_json = json.dumps(header).encode("utf-8")
        f.write(len(header_json).to_bytes(4, "little"))
        f.write(header_json)

        # Write parameter data
        for name, param in params.items():
            param_bytes = param.astype(np.float32).tobytes()
            f.write(param_bytes)


def _create_js_wrapper(model: BaseModel, output_path: Path, config: WASMExportConfig) -> None:
    """Create JavaScript wrapper for inference."""
    js_code = f'''/**
 * Kolibri ML - {model.name} WebAssembly Wrapper
 * Auto-generated - do not edit
 */

class KolibriModel {{
    constructor() {{
        this.modelConfig = null;
        this.weights = null;
        this.initialized = false;
    }}

    async load(basePath = '.') {{
        // Load configuration
        const configResponse = await fetch(`${{basePath}}/model.json`);
        this.modelConfig = await configResponse.json();

        // Load weights
        const weightsResponse = await fetch(`${{basePath}}/weights.bin`);
        const weightsBuffer = await weightsResponse.arrayBuffer();
        this.weights = this._parseWeights(weightsBuffer);

        this.initialized = true;
        console.log('Model loaded:', this.modelConfig.name);
    }}

    _parseWeights(buffer) {{
        const view = new DataView(buffer);
        let offset = 0;

        // Read header
        const headerLength = view.getUint32(offset, true);
        offset += 4;

        const headerBytes = new Uint8Array(buffer, offset, headerLength);
        const header = JSON.parse(new TextDecoder().decode(headerBytes));
        offset += headerLength;

        // Read parameters
        const weights = {{}};
        for (const name of header.parameter_names) {{
            const shape = header.shapes[name];
            const size = shape.reduce((a, b) => a * b, 1);
            const data = new Float32Array(buffer, offset, size);
            weights[name] = {{
                data: data,
                shape: shape
            }};
            offset += size * 4;
        }}

        return weights;
    }}

    predict(input) {{
        if (!this.initialized) {{
            throw new Error('Model not initialized. Call load() first.');
        }}

        // Simple forward pass (to be replaced with ONNX.js or custom runtime)
        return this._forward(input);
    }}

    _forward(input) {{
        // NOTE: This is a placeholder implementation.
        // For production use, replace with actual model inference logic that:
        // 1. Applies the neural network layers using loaded weights
        // 2. Implements the specific architecture (transformer, MLP, etc.)
        // 3. Uses WebGL/WebGPU for acceleration when available
        const inputShape = this.modelConfig.input_shape;
        const outputShape = this.modelConfig.output_shape;
        const batchSize = input.length / inputShape.reduce((a, b) => a * b, 1);
        const outputSize = outputShape.reduce((a, b) => a * b, 1);
        return new Float32Array(batchSize * outputSize);
    }}

    getInputShape() {{
        return this.modelConfig?.input_shape || [];
    }}

    getOutputShape() {{
        return this.modelConfig?.output_shape || [];
    }}
}}

// Export for module systems
if (typeof module !== 'undefined' && module.exports) {{
    module.exports = KolibriModel;
}}
'''

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(js_code)


def _create_html_demo(model: BaseModel, output_path: Path) -> None:
    """Create HTML demo page."""
    html_code = f'''<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Kolibri ML - {model.name} Demo</title>
    <style>
        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            max-width: 800px;
            margin: 0 auto;
            padding: 20px;
            background: #f5f5f5;
        }}
        .container {{
            background: white;
            padding: 30px;
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }}
        h1 {{ color: #333; }}
        .status {{ color: #666; margin: 10px 0; }}
        .input-area {{
            margin: 20px 0;
        }}
        textarea {{
            width: 100%;
            height: 100px;
            padding: 10px;
            border: 1px solid #ddd;
            border-radius: 4px;
        }}
        button {{
            background: #007bff;
            color: white;
            border: none;
            padding: 10px 20px;
            border-radius: 4px;
            cursor: pointer;
        }}
        button:hover {{ background: #0056b3; }}
        .output {{
            margin-top: 20px;
            padding: 15px;
            background: #f8f9fa;
            border-radius: 4px;
        }}
    </style>
</head>
<body>
    <div class="container">
        <h1>🐦 Kolibri ML - {model.name}</h1>
        <p class="status" id="status">Loading model...</p>

        <div class="input-area">
            <textarea id="input" placeholder="Enter input data..."></textarea>
            <button onclick="runInference()">Run Inference</button>
        </div>

        <div class="output" id="output">
            Output will appear here
        </div>
    </div>

    <script src="inference.js"></script>
    <script>
        const model = new KolibriModel();

        model.load('.').then(() => {{
            document.getElementById('status').textContent = 'Model loaded! Ready for inference.';
        }}).catch(err => {{
            document.getElementById('status').textContent = 'Error loading model: ' + err.message;
        }});

        function runInference() {{
            const input = document.getElementById('input').value;
            try {{
                const result = model.predict(new Float32Array(input.split(',').map(Number)));
                document.getElementById('output').textContent = 'Result: ' + Array.from(result).join(', ');
            }} catch (err) {{
                document.getElementById('output').textContent = 'Error: ' + err.message;
            }}
        }}
    </script>
</body>
</html>
'''

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(html_code)
