"""
ai_chat.py — FastAPI роутер для AI чата Kolibri

Числовое Формульное Мышление:
- Все ответы содержат числовые паттерны и формулы
- Обучение с верификацией (показывает что изменилось)
- Embedding через числовые паттерны (не character n-gram)
"""
from __future__ import annotations

import hashlib
import math
import time
from typing import Optional

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

from .ai_engine import get_engine
from .number_mind import (
    word_to_pattern,
    pattern_to_str,
    pattern_similarity,
    text_to_digits,
    digits_to_text,
    djb2_hash,
    fnv1a_hash,
    _tokenize,
)

router = APIRouter(prefix="/api/v1/ai", tags=["ai-chat"])


# ---------------------------------------------------------------------------
# Модели запросов/ответов
# ---------------------------------------------------------------------------

class ChatRequest(BaseModel):
    message: str = Field(min_length=1, max_length=4096)
    conversation_id: Optional[str] = Field(default=None)
    temperature: float = Field(default=0.7, ge=0.0, le=2.0)


class ChatResponse(BaseModel):
    response: str
    confidence: float
    conversation_id: str
    sources: list[str]
    knowledge_hits: int
    method: str
    duration_ms: float
    model_available: bool
    # Числовые метаданные
    formula_data: Optional[dict] = None
    graph_stats: Optional[dict] = None


class EmbeddingRequest(BaseModel):
    text: str = Field(min_length=1, max_length=8192)
    dimensions: int = Field(default=64, ge=32, le=512)


class EmbeddingResponse(BaseModel):
    embedding: list[float]
    dimensions: int
    text_length: int
    pattern: str
    hash_djb2: int
    hash_fnv1a: int


class TrainRequest(BaseModel):
    text: str = Field(min_length=10, max_length=100000)
    verify: bool = Field(default=True, description="Показать результат обучения")


class TrainResponse(BaseModel):
    status: str
    patterns: int
    edges: int
    new_patterns: int
    new_edges: int
    tokens: int
    sample_patterns: dict = Field(default_factory=dict)
    before: dict = Field(default_factory=dict)
    after: dict = Field(default_factory=dict)
    formula_generation: int = 0
    formula_fitness: float = 0.0


class PatternRequest(BaseModel):
    word: str = Field(min_length=1, max_length=256)


class PatternResponse(BaseModel):
    word: str
    pattern: str
    hash_djb2: int
    hash_fnv1a: int
    digits: list[int]
    recovered_text: str
    similar_words: list[dict]


class EngineStatsResponse(BaseModel):
    model_available: bool
    # Числовой граф
    graph_patterns: int
    graph_edges: int
    graph_documents: int
    graph_tokens: int
    graph_avg_fitness: float
    graph_avg_weight: float
    # Формулы
    formula_generation: int
    formula_fitness: float
    formula_genome_hex: str
    # C-модель
    c_model_patterns: int
    c_model_edges: int
    c_model_size_mb: float
    c_model_documents: int = 0
    c_model_epoch: int = 0
    c_model_avg_fitness: float = 0.0
    c_model_avg_weight: float = 0.0
    # Общее
    active_conversations: int
    sentence_store_size: int = 0


class ReloadResponse(BaseModel):
    corpus_loaded: bool
    documents: int
    vocab_size: int
    edges: int
    formula_generation: int
    formula_fitness: float = 0.0


# ---------------------------------------------------------------------------
# Числовые Embeddings (через DJB2 паттерны)
# ---------------------------------------------------------------------------

def _numeric_embedding(text: str, dims: int = 64) -> list[float]:
    """
    Embedding на основе числовых паттернов Kolibri.
    
    Алгоритм:
    1. Токенизируем текст
    2. Каждый токен → 64-цифровой паттерн
    3. Усредняем паттерны в вектор
    4. L2-нормализуем
    """
    tokens = _tokenize(text)
    if not tokens:
        return [0.0] * dims

    vector = [0.0] * dims
    count = 0

    for token in tokens:
        if len(token) < 2:
            continue
        pattern = word_to_pattern(token)
        for i in range(min(dims, len(pattern))):
            vector[i] += (pattern[i] - 4.5) / 4.5  # Нормализуем 0-9 → -1..1
        count += 1

    if count > 0:
        vector = [v / count for v in vector]

    # L2-нормализация
    norm = math.sqrt(sum(v * v for v in vector))
    if norm > 1e-12:
        vector = [v / norm for v in vector]

    return [round(v, 6) for v in vector]


# ---------------------------------------------------------------------------
# Эндпоинты
# ---------------------------------------------------------------------------

@router.post("/chat", response_model=ChatResponse)
async def ai_chat(req: ChatRequest) -> ChatResponse:
    """
    Главный AI чат — Числовое Формульное Мышление.
    
    Каждый ответ содержит:
    - Текстовый ответ (из графа знаний + C-модели)
    - Числовые паттерны слов запроса и ответа
    - Формульный predict (100-слойная сеть)
    - Статистику графа знаний
    """
    engine = get_engine()
    try:
        result = engine.chat(
            message=req.message,
            conversation_id=req.conversation_id,
            temperature=req.temperature,
        )
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"AI engine error: {e}")

    return ChatResponse(
        response=result["response"],
        confidence=result.get("confidence", 0.0),
        conversation_id=result.get("conversation_id", ""),
        sources=result.get("sources", []),
        knowledge_hits=result.get("knowledge_hits", 0),
        method=result.get("method", "unknown"),
        duration_ms=result.get("duration_ms", 0.0),
        model_available=result.get("model_available", False),
        formula_data=result.get("formula_data"),
        graph_stats=result.get("graph_stats"),
    )


@router.post("/train", response_model=TrainResponse)
async def train_on_text(req: TrainRequest) -> TrainResponse:
    """
    Обучить AI на тексте с верификацией.
    
    Показывает ЧТО ИЗМЕНИЛОСЬ:
    - Сколько паттернов создано
    - Примеры числовых паттернов новых слов
    - Статистика до/после
    """
    engine = get_engine()
    try:
        if req.verify:
            result = engine.train_and_verify(req.text)
        else:
            result = engine.train_text(req.text)
            result["before"] = {}
            result["after"] = engine.graph.get_stats()
            result["sample_patterns"] = {}
            result["formula_generation"] = engine.formula_pool.generation
            result["formula_fitness"] = round(engine.formula_pool.best().fitness, 4)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Training error: {e}")

    return TrainResponse(
        status="ok",
        patterns=result.get("patterns", 0),
        edges=result.get("edges", 0),
        new_patterns=result.get("new_patterns", 0),
        new_edges=result.get("new_edges", 0),
        tokens=result.get("tokens", 0),
        sample_patterns=result.get("sample_patterns", {}),
        before=result.get("before", {}),
        after=result.get("after", {}),
        formula_generation=result.get("formula_generation", 0),
        formula_fitness=result.get("formula_fitness", 0.0),
    )


@router.post("/pattern", response_model=PatternResponse)
async def get_pattern(req: PatternRequest) -> PatternResponse:
    """
    Получить числовой паттерн слова.
    
    Каждое слово = 64 цифры (DJB2 + LCG каскад).
    Показывает: паттерн, хеши, кодирование в цифры, восстановление.
    """
    engine = get_engine()
    word = req.word.lower().strip()
    pattern = word_to_pattern(word)
    digits = text_to_digits(word)
    recovered = digits_to_text(digits)
    similar = engine.graph.find_similar(word, limit=5)

    return PatternResponse(
        word=word,
        pattern=pattern_to_str(pattern),
        hash_djb2=djb2_hash(word),
        hash_fnv1a=fnv1a_hash(word),
        digits=digits[:30],
        recovered_text=recovered,
        similar_words=[
            {"word": w, "similarity": s}
            for w, s in similar
        ],
    )


@router.post("/embedding", response_model=EmbeddingResponse)
async def compute_embedding(req: EmbeddingRequest) -> EmbeddingResponse:
    """
    Числовой embedding — на основе паттернов Kolibri (не char n-gram!).
    """
    embedding = _numeric_embedding(req.text, dims=req.dimensions)
    pattern = pattern_to_str(word_to_pattern(req.text.split()[0] if req.text.split() else req.text))
    return EmbeddingResponse(
        embedding=embedding,
        dimensions=req.dimensions,
        text_length=len(req.text),
        pattern=pattern,
        hash_djb2=djb2_hash(req.text.lower()),
        hash_fnv1a=fnv1a_hash(req.text.lower()),
    )


@router.get("/stats", response_model=EngineStatsResponse)
async def engine_stats() -> EngineStatsResponse:
    """Статистика AI-движка — числовой граф + формулы + C-модель."""
    engine = get_engine()
    g = engine.graph.get_stats()
    c = engine._get_model_stats()
    best = engine.formula_pool.best()

    return EngineStatsResponse(
        model_available=engine.c_retriever.available,
        graph_patterns=g["patterns"],
        graph_edges=g["edges"],
        graph_documents=g["documents_trained"],
        graph_tokens=g["tokens_processed"],
        graph_avg_fitness=g["avg_fitness"],
        graph_avg_weight=g["avg_weight"],
        formula_generation=engine.formula_pool.generation,
        formula_fitness=round(best.fitness, 4),
        formula_genome_hex=best.gene.to_hex()[:32],
        c_model_patterns=c.get("patterns", 0),
        c_model_edges=c.get("edges", 0),
        c_model_size_mb=c.get("size_mb", 0.0),
        c_model_documents=c.get("documents", 0),
        c_model_epoch=c.get("epoch", 0),
        c_model_avg_fitness=c.get("avg_fitness", 0.0),
        c_model_avg_weight=c.get("avg_weight", 0.0),
        active_conversations=len(engine.conversations),
        sentence_store_size=engine.sentence_store.size,
    )


@router.post("/reload", response_model=ReloadResponse)
async def reload_corpus() -> ReloadResponse:
    """Перезагрузить корпус и пересобрать числовой граф."""
    engine = get_engine()
    info = engine.reload_corpus()
    return ReloadResponse(**info)


@router.delete("/conversations/{conv_id}")
async def delete_conversation(conv_id: str) -> dict:
    engine = get_engine()
    if conv_id in engine.conversations:
        del engine.conversations[conv_id]
        return {"status": "deleted", "conversation_id": conv_id}
    raise HTTPException(status_code=404, detail="Conversation not found")
