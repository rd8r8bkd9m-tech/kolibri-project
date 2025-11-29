# Kolibri ML User Guide

This guide covers how to use the Kolibri ML system for machine learning tasks including model training, inference, and deployment.

## Table of Contents

1. [Getting Started](#getting-started)
2. [Working with Models](#working-with-models)
3. [Training Models](#training-models)
4. [Running Inference](#running-inference)
5. [Deploying Models](#deploying-models)
6. [Integration](#integration)
7. [Best Practices](#best-practices)

## Getting Started

### Installation

The ML module is included with the Kolibri project. Install dependencies:

```bash
pip install -r ml/requirements.txt
```

For GPU support (optional):
```bash
pip install torch onnxruntime-gpu
```

### Basic Usage

```python
import sys
sys.path.insert(0, '/path/to/kolibri-project')

from ml.models.classifier import Classifier
from ml.inference.predictor import Predictor
import numpy as np

# Create a classifier
model = Classifier(input_dim=64, num_classes=10)

# Create predictor for inference
predictor = Predictor(model)

# Run prediction
inputs = np.random.randn(1, 64).astype(np.float32)
outputs = predictor.predict(inputs)
print(f"Predictions shape: {outputs.shape}")
```

## Working with Models

### Available Models

| Model | Use Case | Input | Output |
|-------|----------|-------|--------|
| TransformerLite | Text encoding, generation | Token IDs | Hidden states |
| NeuralCompressor | Compression enhancement | Byte sequences | Probabilities |
| SemanticEncoder | Document embeddings | Token IDs | Embeddings |
| Classifier | Classification | Features | Class probabilities |
| TextGenerator | Text generation | Token IDs | Next token logits |

### Creating Models

```python
from ml.models.transformer_lite import TransformerLite
from ml.models.neural_compressor import NeuralCompressor
from ml.models.semantic_encoder import SemanticEncoder

# TransformerLite with custom config
transformer = TransformerLite(
    hidden_size=128,
    num_layers=2,
    num_heads=2,
    vocab_size=5000,
    max_seq_length=256,
)

# NeuralCompressor for compression tasks
compressor = NeuralCompressor(
    context_size=512,
    hidden_size=64,
    num_layers=1,
)

# SemanticEncoder for embeddings
encoder = SemanticEncoder(
    embedding_dim=256,
    hidden_size=128,
)
```

### Model Configuration

Load from config file:

```python
from ml.utils.config import load_config

config = load_config("ml/config.yaml")
transformer = TransformerLite(
    hidden_size=config.transformer.hidden_size,
    num_layers=config.transformer.num_layers,
)
```

### Saving and Loading

```python
# Save model
model.save("checkpoints/model")
# Creates: model.npz, model.json

# Load model
from ml.models.classifier import Classifier
model = Classifier(input_dim=64, num_classes=10)
model.load("checkpoints/model")
```

## Training Models

### Basic Training

```python
from ml.training.trainer import Trainer, TrainingConfig
from ml.training.data_loader import ArrayDataset, DataLoader
import numpy as np

# Prepare data
X_train = np.random.randn(1000, 64).astype(np.float32)
y_train = np.random.randint(0, 10, 1000)

dataset = ArrayDataset(X_train, y_train)
loader = DataLoader(dataset, batch_size=32, shuffle=True)

# Configure training
config = TrainingConfig(
    learning_rate=1e-3,
    epochs=10,
    batch_size=32,
)

# Train
model = Classifier(input_dim=64, num_classes=10)
trainer = Trainer(model, config)
history = trainer.train(loader.get_batches())

print(f"Final loss: {history['train_loss'][-1]:.4f}")
```

### Using Callbacks

```python
from ml.training.callbacks import (
    LoggingCallback,
    CheckpointCallback,
    EarlyStoppingCallback,
)

callbacks = [
    LoggingCallback(log_every=10),
    CheckpointCallback("checkpoints/", save_best_only=True),
    EarlyStoppingCallback(patience=5),
]

trainer = Trainer(model, config, callbacks=callbacks)
```

### Training with Validation

```python
# Split data
train_data = loader.get_batches()[:80]
val_data = loader.get_batches()[80:]

# Train with validation
history = trainer.train(train_data, eval_data=val_data)
```

## Running Inference

### Basic Inference

```python
from ml.inference.predictor import Predictor, PredictorConfig

# Configure predictor
config = PredictorConfig(
    device="auto",  # auto, cpu, cuda, metal
    max_batch_size=32,
    optimize_for_latency=True,
)

predictor = Predictor(model, config)

# Single prediction
output = predictor.predict(single_input)

# Batch prediction
outputs = predictor.predict_batch(list_of_inputs)
```

### Warmup and Benchmarking

```python
# Warmup to avoid cold start latency
predictor.warmup(num_iterations=5)

# Get statistics
stats = predictor.get_stats()
print(f"Average latency: {stats['avg_latency_ms']:.2f}ms")
```

### ONNX Runtime Inference

```python
from ml.inference.onnx_runtime import ONNXPredictor

# Load ONNX model
predictor = ONNXPredictor(
    "model.onnx",
    device="cpu",
    num_threads=4,
)

# Run inference
output = predictor.predict(inputs)

# Benchmark
results = predictor.benchmark([1, 32], num_iterations=100)
print(f"P95 latency: {results['p95_latency_ms']:.2f}ms")
```

### Quantization

```python
from ml.inference.quantization import quantize_model, QuantizationConfig

# Quantize to INT8
config = QuantizationConfig(mode="int8")
quantized_model = quantize_model(model, config)

# Use quantized model
predictor = Predictor(quantized_model)
```

## Deploying Models

### Export to ONNX

```python
from ml.export.to_onnx import export_to_onnx, ONNXExportConfig

config = ONNXExportConfig(
    opset_version=14,
    quantize=True,
)

export_to_onnx(model, "model.onnx", config=config)
```

### Export for WebAssembly

```python
from ml.export.to_wasm import export_to_wasm

export_to_wasm(model, "wasm_output/")

# Output files:
# - model.json: Model configuration
# - weights.bin: Binary weights
# - inference.js: JavaScript wrapper
# - demo.html: Demo page
```

### Export for Mobile

```python
# iOS/macOS
from ml.export.to_coreml import export_to_coreml
export_to_coreml(model, "model.mlmodel")

# Android
from ml.export.to_tflite import export_to_tflite
export_to_tflite(model, "model.tflite")
```

### Export to C Code

```python
from ml.export.to_c import export_to_c

export_to_c(model, "c_output/")

# Output files:
# - model_weights.h: Weight arrays
# - model_config.h: Configuration
# - model_inference.c: Inference code
```

## Integration

### KolibriSim Integration

```python
from core.kolibri_sim import KolibriSim
from ml.integration.kolibri_core_bridge import KolibriCoreBridge

# Create bridge
sim = KolibriSim(zerno=42)
bridge = KolibriCoreBridge(sim=sim)

# Enhanced knowledge teaching
bridge.enhance_teaching("вопрос", "ответ")

# Semantic search in knowledge base
results = bridge.semantic_search("поиск")
for r in results:
    print(f"{r['stimulus']}: {r['score']:.3f}")
```

### Compression Enhancement

```python
from ml.integration.archiver_ml import ArchiverML

archiver = ArchiverML()

# Analyze data
result = archiver.analyze(data_bytes)
print(f"Entropy: {result.entropy_bits_per_byte:.2f} bits/byte")
print(f"Recommended: {result.recommended_algorithm}")

# Get strategy
strategy = archiver.recommend_strategy(data_bytes)
print(f"Algorithm order: {strategy['order']}")
```

### Cloud Semantic Search

```python
from ml.integration.cloud_ml import CloudMLSearch

search = CloudMLSearch()

# Index documents
for doc in documents:
    search.add_document(doc.id, doc.content, title=doc.title)

# Build efficient index
search.build_index(index_type="hnsw")

# Search
results = search.search("query text", top_k=10)
for r in results:
    print(f"{r.title}: {r.score:.3f}")
```

## Best Practices

### Memory Management

```python
from ml.utils.memory_manager import MemoryManager

manager = MemoryManager(device="cuda")

# Check before large allocation
if manager.check_allocation(1_000_000_000):
    # Safe to allocate 1GB
    pass

# Clear cache after batch
manager.clear_cache()
```

### Device Selection

```python
from ml.utils.device_detector import get_device, detect_all_devices

# Auto-select best device
device = get_device("auto")
print(f"Using: {device.device_string}")

# List all available devices
for d in detect_all_devices():
    print(f"{d.name}: {d.device_type.value}")
```

### Logging

```python
from ml.utils.logger import get_logger

logger = get_logger()
logger.info("Training started", epoch=1, lr=0.001)
logger.log_inference("model_name", batch_size=32, latency_ms=5.2, device="cuda")
```

### Configuration Best Practices

1. **Use auto device selection** for portability
2. **Enable quantization** for production (INT8 or FP16)
3. **Set appropriate batch sizes** based on memory constraints
4. **Use ONNX export** for cross-platform deployment
5. **Enable warmup** to avoid cold-start latency

## Troubleshooting

### CUDA Out of Memory

```python
# Reduce batch size
config = TrainingConfig(batch_size=16)

# Clear cache
from ml.utils.memory_manager import MemoryManager
MemoryManager("cuda").clear_cache()
```

### Slow Inference

```python
# Enable ONNX runtime
from ml.inference.onnx_runtime import ONNXPredictor
predictor = ONNXPredictor("model.onnx", optimize=True)

# Increase batch size for throughput
predictor = Predictor(model, PredictorConfig(max_batch_size=64))
```

### Import Errors

```python
# Ensure project root is in path
import sys
sys.path.insert(0, '/path/to/kolibri-project')
```

## See Also

- [ML API Reference](ml_api.md)
- [ML README](../ml/README.md)
- [Kolibri Project README](../README.md)
