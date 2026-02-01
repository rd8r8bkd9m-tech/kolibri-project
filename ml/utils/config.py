"""Configuration management for Kolibri ML system."""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Optional

try:
    import yaml

    YAML_AVAILABLE = True
except ImportError:
    YAML_AVAILABLE = False


@dataclass
class TransformerConfig:
    """Configuration for TransformerLite model."""

    hidden_size: int = 256
    num_layers: int = 4
    num_heads: int = 4
    intermediate_size: int = 1024
    max_seq_length: int = 512
    vocab_size: int = 32000
    dropout: float = 0.1


@dataclass
class NeuralCompressorConfig:
    """Configuration for NeuralCompressor model."""

    context_size: int = 1024
    prediction_mode: str = "adaptive"
    hidden_size: int = 128
    num_layers: int = 2


@dataclass
class InferenceConfig:
    """Configuration for inference settings."""

    use_onnx: bool = True
    optimize_for_latency: bool = True
    max_batch_size: int = 32
    num_threads: int = 4


@dataclass
class MLConfig:
    """Main configuration for Kolibri ML system."""

    default_device: str = "auto"
    model_cache: str = field(default_factory=lambda: str(Path.home() / ".kolibri" / "ml_models"))
    quantization: str = "fp16"
    batch_size: int = 32
    transformer: TransformerConfig = field(default_factory=TransformerConfig)
    neural_compressor: NeuralCompressorConfig = field(default_factory=NeuralCompressorConfig)
    inference: InferenceConfig = field(default_factory=InferenceConfig)

    def __post_init__(self) -> None:
        """Validate configuration after initialization."""
        valid_devices = {"auto", "cpu", "cuda", "metal", "wasm"}
        if self.default_device not in valid_devices:
            raise ValueError(f"default_device must be one of {valid_devices}")

        valid_quant = {"fp32", "fp16", "int8", "int4"}
        if self.quantization not in valid_quant:
            raise ValueError(f"quantization must be one of {valid_quant}")

    def to_dict(self) -> Dict[str, Any]:
        """Convert config to dictionary."""
        return {
            "ml": {
                "default_device": self.default_device,
                "model_cache": self.model_cache,
                "quantization": self.quantization,
                "batch_size": self.batch_size,
            },
            "models": {
                "transformer_lite": {
                    "hidden_size": self.transformer.hidden_size,
                    "num_layers": self.transformer.num_layers,
                    "num_heads": self.transformer.num_heads,
                    "intermediate_size": self.transformer.intermediate_size,
                    "max_seq_length": self.transformer.max_seq_length,
                    "vocab_size": self.transformer.vocab_size,
                    "dropout": self.transformer.dropout,
                },
                "neural_compressor": {
                    "context_size": self.neural_compressor.context_size,
                    "prediction_mode": self.neural_compressor.prediction_mode,
                    "hidden_size": self.neural_compressor.hidden_size,
                    "num_layers": self.neural_compressor.num_layers,
                },
            },
            "inference": {
                "use_onnx": self.inference.use_onnx,
                "optimize_for_latency": self.inference.optimize_for_latency,
                "max_batch_size": self.inference.max_batch_size,
                "num_threads": self.inference.num_threads,
            },
        }


def load_config(path: Optional[Path | str] = None) -> MLConfig:
    """Load ML configuration from YAML file or environment.

    Args:
        path: Path to config file. If None, tries default locations.

    Returns:
        MLConfig instance with loaded or default values.
    """
    config_path: Optional[Path] = None

    if path is not None:
        config_path = Path(path)
    else:
        env_path = os.getenv("KOLIBRI_ML_CONFIG")
        if env_path:
            config_path = Path(env_path)
        else:
            default_locations = [
                Path.cwd() / "ml" / "config.yaml",
                Path.home() / ".kolibri" / "ml_config.yaml",
                Path("/etc/kolibri/ml_config.yaml"),
            ]
            for loc in default_locations:
                if loc.exists():
                    config_path = loc
                    break

    if config_path is None or not config_path.exists():
        return MLConfig()

    if not YAML_AVAILABLE:
        return MLConfig()

    with open(config_path, encoding="utf-8") as f:
        data = yaml.safe_load(f)

    if data is None:
        return MLConfig()

    ml_data = data.get("ml", {})
    models_data = data.get("models", {})
    inference_data = data.get("inference", {})

    transformer_data = models_data.get("transformer_lite", {})
    compressor_data = models_data.get("neural_compressor", {})

    transformer_config = TransformerConfig(
        hidden_size=transformer_data.get("hidden_size", 256),
        num_layers=transformer_data.get("num_layers", 4),
        num_heads=transformer_data.get("num_heads", 4),
        intermediate_size=transformer_data.get("intermediate_size", 1024),
        max_seq_length=transformer_data.get("max_seq_length", 512),
        vocab_size=transformer_data.get("vocab_size", 32000),
        dropout=transformer_data.get("dropout", 0.1),
    )

    compressor_config = NeuralCompressorConfig(
        context_size=compressor_data.get("context_size", 1024),
        prediction_mode=compressor_data.get("prediction_mode", "adaptive"),
        hidden_size=compressor_data.get("hidden_size", 128),
        num_layers=compressor_data.get("num_layers", 2),
    )

    inference_config = InferenceConfig(
        use_onnx=inference_data.get("use_onnx", True),
        optimize_for_latency=inference_data.get("optimize_for_latency", True),
        max_batch_size=inference_data.get("max_batch_size", 32),
        num_threads=inference_data.get("num_threads", 4),
    )

    return MLConfig(
        default_device=ml_data.get("default_device", "auto"),
        model_cache=ml_data.get("model_cache", str(Path.home() / ".kolibri" / "ml_models")),
        quantization=ml_data.get("quantization", "fp16"),
        batch_size=ml_data.get("batch_size", 32),
        transformer=transformer_config,
        neural_compressor=compressor_config,
        inference=inference_config,
    )


def save_config(config: MLConfig, path: Path | str) -> None:
    """Save ML configuration to YAML file.

    Args:
        config: Configuration to save.
        path: Output file path.
    """
    if not YAML_AVAILABLE:
        raise ImportError("PyYAML is required to save config. Install with: pip install pyyaml")

    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(config.to_dict(), f, default_flow_style=False, allow_unicode=True)
