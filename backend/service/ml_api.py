"""REST API endpoints for Kolibri ML system."""
from __future__ import annotations

import time
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

from fastapi import APIRouter, HTTPException, status
from pydantic import BaseModel, Field

# Add ML module to path
ML_PATH = Path(__file__).resolve().parents[3] / "ml"
if str(ML_PATH.parent) not in sys.path:
    sys.path.insert(0, str(ML_PATH.parent))

from ml.models.transformer_lite import TransformerLite
from ml.models.neural_compressor import NeuralCompressor
from ml.models.semantic_encoder import SemanticEncoder
from ml.models.classifier import Classifier
from ml.inference.predictor import Predictor
from ml.integration.archiver_ml import ArchiverML
from ml.integration.cloud_ml import CloudMLSearch
from ml.utils.device_detector import get_device, detect_all_devices
from ml.utils.config import load_config

router = APIRouter(prefix="/api/ml", tags=["ml"])

# Global model instances (lazy loaded)
_models: Dict[str, Any] = {}
_config = None


def _get_config():
    """Get or load ML configuration."""
    global _config
    if _config is None:
        _config = load_config()
    return _config


def _get_model(model_name: str):
    """Get or create a model instance."""
    if model_name not in _models:
        config = _get_config()
        if model_name == "transformer_lite":
            _models[model_name] = TransformerLite(
                hidden_size=config.transformer.hidden_size,
                num_layers=config.transformer.num_layers,
                num_heads=config.transformer.num_heads,
            )
        elif model_name == "neural_compressor":
            _models[model_name] = NeuralCompressor(
                context_size=config.neural_compressor.context_size,
                hidden_size=config.neural_compressor.hidden_size,
            )
        elif model_name == "semantic_encoder":
            _models[model_name] = SemanticEncoder()
        elif model_name == "classifier":
            _models[model_name] = Classifier()
        else:
            raise ValueError(f"Unknown model: {model_name}")
    return _models[model_name]


# Request/Response models

class PredictRequest(BaseModel):
    """Request for model prediction."""
    model: str = Field(default="transformer_lite", description="Model name")
    inputs: List[List[int]] = Field(description="Input token IDs [batch, seq_len]")
    parameters: Optional[Dict[str, Any]] = Field(default=None, description="Additional parameters")


class PredictResponse(BaseModel):
    """Response from model prediction."""
    outputs: List[List[float]] = Field(description="Model outputs")
    model: str
    device: str
    latency_ms: float


class CompressRequest(BaseModel):
    """Request for neural compression analysis."""
    data: str = Field(description="Base64 encoded data to analyze")
    mode: str = Field(default="analyze", description="Mode: 'analyze' or 'compress'")


class CompressResponse(BaseModel):
    """Response from compression analysis."""
    original_size: int
    estimated_compressed_size: int
    entropy_bits_per_byte: float
    compression_ratio: float
    recommended_algorithm: str


class TrainRequest(BaseModel):
    """Request for model training."""
    model: str = Field(default="classifier", description="Model to train")
    data: Dict[str, Any] = Field(description="Training data")
    config: Optional[Dict[str, Any]] = Field(default=None, description="Training config")


class TrainResponse(BaseModel):
    """Response from training."""
    model: str
    epochs: int
    final_loss: float
    status: str


class ModelInfo(BaseModel):
    """Information about a model."""
    name: str
    architecture: str
    parameters: int
    input_shape: List[int]
    output_shape: List[int]
    device: str
    quantization: str


class DeviceInfo(BaseModel):
    """Information about compute device."""
    device_type: str
    device_id: int
    name: str
    memory_total: int
    memory_available: int
    is_available: bool


class SearchRequest(BaseModel):
    """Request for semantic search."""
    query: str = Field(description="Search query")
    top_k: int = Field(default=10, description="Number of results")
    min_score: float = Field(default=0.0, description="Minimum similarity score")


class SearchResult(BaseModel):
    """Single search result."""
    doc_id: str
    title: str
    score: float
    preview: str


# API Endpoints

@router.post("/predict", response_model=PredictResponse)
async def predict(request: PredictRequest) -> PredictResponse:
    """Run inference on a model.
    
    Supports models: transformer_lite, neural_compressor, semantic_encoder, classifier
    """
    try:
        import numpy as np
        
        model = _get_model(request.model)
        predictor = Predictor(model)
        
        inputs = np.array(request.inputs, dtype=np.int64)
        
        start_time = time.perf_counter()
        outputs = predictor.predict(inputs)
        latency_ms = (time.perf_counter() - start_time) * 1000
        
        return PredictResponse(
            outputs=outputs.tolist(),
            model=request.model,
            device=predictor.device_info.device_string,
            latency_ms=latency_ms,
        )
    except Exception as e:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Prediction failed: {str(e)}",
        )


@router.post("/compress", response_model=CompressResponse)
async def compress(request: CompressRequest) -> CompressResponse:
    """Analyze data for neural compression.
    
    Returns entropy estimation and compression recommendations.
    """
    try:
        import base64
        
        data = base64.b64decode(request.data)
        archiver_ml = ArchiverML()
        result = archiver_ml.analyze(data)
        
        return CompressResponse(
            original_size=result.original_size,
            estimated_compressed_size=result.estimated_compressed_size,
            entropy_bits_per_byte=result.entropy_bits_per_byte,
            compression_ratio=result.compression_ratio,
            recommended_algorithm=result.recommended_algorithm,
        )
    except Exception as e:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Compression analysis failed: {str(e)}",
        )


@router.post("/train", response_model=TrainResponse)
async def train(request: TrainRequest) -> TrainResponse:
    """Train a model (simplified API).
    
    Note: Full training requires more sophisticated data handling.
    This endpoint provides a basic training interface.
    """
    try:
        import numpy as np
        from ml.training.trainer import Trainer, TrainingConfig
        from ml.training.data_loader import ArrayDataset, DataLoader
        
        model = _get_model(request.model)
        
        # Parse training data
        inputs = np.array(request.data.get("inputs", []), dtype=np.float32)
        targets = np.array(request.data.get("targets", []), dtype=np.int64)
        
        if len(inputs) == 0 or len(targets) == 0:
            raise ValueError("Training data must include 'inputs' and 'targets'")
        
        # Create training config
        config = TrainingConfig(
            learning_rate=request.config.get("learning_rate", 1e-4) if request.config else 1e-4,
            epochs=request.config.get("epochs", 1) if request.config else 1,
            batch_size=request.config.get("batch_size", 32) if request.config else 32,
        )
        
        # Create data loader
        dataset = ArrayDataset(inputs, targets)
        loader = DataLoader(dataset, batch_size=config.batch_size)
        train_data = loader.get_batches()
        
        # Train
        trainer = Trainer(model, config)
        history = trainer.train(train_data)
        
        final_loss = history["train_loss"][-1] if history["train_loss"] else 0.0
        
        return TrainResponse(
            model=request.model,
            epochs=config.epochs,
            final_loss=final_loss,
            status="completed",
        )
    except Exception as e:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Training failed: {str(e)}",
        )


@router.get("/models", response_model=List[ModelInfo])
async def list_models() -> List[ModelInfo]:
    """List available ML models."""
    models_info = []
    
    model_classes = [
        ("transformer_lite", TransformerLite),
        ("neural_compressor", NeuralCompressor),
        ("semantic_encoder", SemanticEncoder),
        ("classifier", Classifier),
    ]
    
    device = get_device("auto")
    
    for name, cls in model_classes:
        try:
            model = cls()
            models_info.append(ModelInfo(
                name=name,
                architecture=cls.__name__,
                parameters=model.num_parameters(),
                input_shape=model.get_input_shape(),
                output_shape=model.get_output_shape(),
                device=device.device_string,
                quantization="fp32",
            ))
        except Exception:
            continue
    
    return models_info


@router.get("/models/{model_name}", response_model=ModelInfo)
async def get_model_info(model_name: str) -> ModelInfo:
    """Get information about a specific model."""
    try:
        model = _get_model(model_name)
        device = get_device("auto")
        
        return ModelInfo(
            name=model_name,
            architecture=model.__class__.__name__,
            parameters=model.num_parameters(),
            input_shape=model.get_input_shape(),
            output_shape=model.get_output_shape(),
            device=device.device_string,
            quantization=model.dtype,
        )
    except ValueError as e:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=str(e),
        )


@router.get("/devices", response_model=List[DeviceInfo])
async def list_devices() -> List[DeviceInfo]:
    """List available compute devices."""
    devices = detect_all_devices()
    return [
        DeviceInfo(
            device_type=d.device_type.value,
            device_id=d.device_id,
            name=d.name,
            memory_total=d.memory_total,
            memory_available=d.memory_available,
            is_available=d.is_available,
        )
        for d in devices
    ]


@router.post("/search", response_model=List[SearchResult])
async def semantic_search(request: SearchRequest) -> List[SearchResult]:
    """Perform semantic search on indexed documents."""
    try:
        # Get or create search index
        if "search" not in _models:
            _models["search"] = CloudMLSearch()
        
        search = _models["search"]
        results = search.search(request.query, request.top_k, request.min_score)
        
        return [
            SearchResult(
                doc_id=r.doc_id,
                title=r.title,
                score=r.score,
                preview=r.content_preview,
            )
            for r in results
        ]
    except Exception as e:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Search failed: {str(e)}",
        )


@router.get("/health")
async def health() -> Dict[str, Any]:
    """Health check for ML service."""
    device = get_device("auto")
    return {
        "status": "healthy",
        "device": device.device_string,
        "models_loaded": len(_models),
        "version": "0.1.0",
    }
