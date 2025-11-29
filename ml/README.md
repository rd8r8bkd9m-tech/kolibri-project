# Kolibri ML System

Machine Learning infrastructure for the Kolibri project, providing cross-platform inference capabilities for CPU, GPU (CUDA/Metal), WebAssembly, and mobile devices.

## Features

- **Cross-Platform Models**: Run the same model on CPU, GPU, browser (WASM), and mobile
- **Lightweight Architectures**: Optimized models designed to be < 50MB
- **Multiple Export Formats**: ONNX, CoreML, TFLite, WebAssembly, C code
- **Unified Training**: Single training interface for all deployment targets
- **Integration**: Seamless integration with existing Kolibri components

## Quick Start

```python
from ml.models.transformer_lite import TransformerLite
from ml.inference.predictor import Predictor
import numpy as np

# Create model
model = TransformerLite(hidden_size=256, num_layers=4, num_heads=4)

# Create predictor (auto-selects best device)
predictor = Predictor(model)

# Run inference
input_ids = np.random.randint(0, 1000, (1, 32))
outputs = predictor.predict(input_ids)
```

## Architecture

```
ml/
├── models/             # Neural network architectures
│   ├── transformer_lite.py     # Lightweight transformer
│   ├── neural_compressor.py    # Compression-focused model
│   ├── semantic_encoder.py     # Document embeddings
│   ├── classifier.py           # Universal classifier
│   └── text_generator.py       # Text generation
│
├── training/           # Training infrastructure
│   ├── trainer.py              # Unified trainer
│   ├── data_loader.py          # Data loading utilities
│   ├── metrics.py              # Loss and metrics
│   ├── callbacks.py            # Training callbacks
│   └── distributed.py          # Multi-GPU support
│
├── inference/          # Inference optimization
│   ├── predictor.py            # Universal predictor
│   ├── onnx_runtime.py         # ONNX integration
│   ├── quantization.py         # INT8/FP16 quantization
│   └── batch_processor.py      # Batch processing
│
├── export/             # Model export
│   ├── to_onnx.py              # Export to ONNX
│   ├── to_wasm.py              # Export for WebAssembly
│   ├── to_coreml.py            # Export for iOS/macOS
│   ├── to_tflite.py            # Export for Android
│   └── to_c.py                 # Generate C code
│
├── integration/        # Kolibri integrations
│   ├── kolibri_core_bridge.py  # KolibriSim integration
│   ├── archiver_ml.py          # Compression enhancement
│   ├── gpu_backend.py          # GPU acceleration
│   ├── swarm_ml.py             # Swarm intelligence
│   └── cloud_ml.py             # Semantic search
│
└── utils/              # Utilities
    ├── config.py               # Configuration
    ├── device_detector.py      # Device detection
    ├── logger.py               # ML logging
    └── memory_manager.py       # Memory management
```

## Models

### TransformerLite

Lightweight encoder-only transformer optimized for cross-platform deployment.

```python
from ml.models.transformer_lite import TransformerLite

model = TransformerLite(
    hidden_size=256,      # Hidden dimension
    num_layers=4,         # Number of layers
    num_heads=4,          # Attention heads
    vocab_size=32000,     # Vocabulary size
    max_seq_length=512,   # Max sequence length
)

# Encode text
embeddings = model.encode(input_ids, pooling="mean")
```

### NeuralCompressor

LSTM-based model for data compression enhancement.

```python
from ml.models.neural_compressor import NeuralCompressor

model = NeuralCompressor(
    context_size=1024,
    hidden_size=128,
    num_layers=2,
)

# Estimate entropy
entropy = model.estimate_entropy(data_bytes)

# Get compression hints
hints = model.compress_context(data_bytes)
```

### SemanticEncoder

Generates fixed-size embeddings for semantic similarity and search.

```python
from ml.models.semantic_encoder import SemanticEncoder

encoder = SemanticEncoder(embedding_dim=384)

# Encode documents
embeddings = encoder.encode(token_ids)

# Search
results = encoder.search(query_emb, corpus_embs, top_k=10)
```

## Training

```python
from ml.models.classifier import Classifier
from ml.training.trainer import Trainer, TrainingConfig
from ml.training.data_loader import ArrayDataset, DataLoader

# Create model and data
model = Classifier(input_dim=64, num_classes=10)
dataset = ArrayDataset(inputs, targets)
loader = DataLoader(dataset, batch_size=32)

# Configure training
config = TrainingConfig(
    learning_rate=1e-4,
    epochs=10,
    batch_size=32,
    device="auto",  # Auto-select GPU/CPU
)

# Train
trainer = Trainer(model, config)
history = trainer.train(loader.get_batches())
```

## Export

### ONNX Export

```python
from ml.export.to_onnx import export_to_onnx

export_to_onnx(model, "model.onnx")
```

### WebAssembly Export

```python
from ml.export.to_wasm import export_to_wasm

export_to_wasm(model, "wasm_output/")
# Creates: model.json, weights.bin, inference.js, demo.html
```

### C Code Export

```python
from ml.export.to_c import export_to_c

export_to_c(model, "c_output/")
# Creates: model_weights.h, model_config.h, model_inference.c
```

## Integration with Kolibri

### KolibriSim Integration

```python
from core.kolibri_sim import KolibriSim
from ml.integration.kolibri_core_bridge import KolibriCoreBridge

sim = KolibriSim(zerno=42)
bridge = KolibriCoreBridge(sim=sim)

# Enhanced teaching with embeddings
bridge.enhance_teaching("привет", "здравствуй")

# Semantic search
results = bridge.semantic_search("приветствие")
```

### Compression Enhancement

```python
from ml.integration.archiver_ml import ArchiverML

archiver = ArchiverML()

# Analyze data for compression
result = archiver.analyze(data)
print(f"Entropy: {result.entropy_bits_per_byte} bits/byte")
print(f"Recommended: {result.recommended_algorithm}")

# Get compression strategy
strategy = archiver.recommend_strategy(data)
```

### Cloud Semantic Search

```python
from ml.integration.cloud_ml import CloudMLSearch

search = CloudMLSearch()

# Index documents
search.add_document("doc1", "Machine learning is great")
search.add_document("doc2", "Deep learning models")

# Search
results = search.search("AI and neural networks", top_k=5)
```

## API Reference

See [ML API Reference](../docs/ml_api.md) for detailed API documentation.

## Configuration

Create `ml/config.yaml`:

```yaml
ml:
  default_device: auto  # auto, cpu, cuda, metal, wasm
  model_cache: ~/.kolibri/ml_models
  quantization: fp16
  batch_size: 32

models:
  transformer_lite:
    hidden_size: 256
    num_layers: 4
    num_heads: 4

  neural_compressor:
    context_size: 1024
    prediction_mode: adaptive

inference:
  use_onnx: true
  optimize_for_latency: true
```

## Performance

| Model | Params | CPU (ms) | CUDA (ms) | WASM (ms) |
|-------|--------|----------|-----------|-----------|
| TransformerLite-64 | 1.2M | 15 | 2 | 50 |
| TransformerLite-256 | 12M | 45 | 5 | 150 |
| Classifier-64 | 50K | 0.5 | 0.1 | 2 |
| SemanticEncoder | 5M | 25 | 3 | 80 |

## Requirements

```
numpy>=1.24.0
# Optional for advanced features:
# torch>=2.0.0
# onnx>=1.14.0
# onnxruntime>=1.15.0
```

## License

Part of the Kolibri project. See main project LICENSE.
