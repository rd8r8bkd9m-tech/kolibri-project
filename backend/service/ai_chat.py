"""
ai_chat.py — FastAPI роутер для AI чата Kolibri

Числовое Формульное Мышление:
- Все ответы содержат числовые паттерны и формулы
- Обучение с верификацией (показывает что изменилось)
- Embedding через числовые паттерны (не character n-gram)
"""
from __future__ import annotations

import asyncio
import base64
import binascii
import concurrent.futures
import functools
import hashlib
import html
import json
import math
import os
import re
import time
import uuid
from typing import Any, Optional, Literal

import httpx
from fastapi import APIRouter, File, Form, HTTPException, Query, Request, UploadFile
from fastapi.responses import FileResponse, StreamingResponse
from pydantic import BaseModel, Field

from .ai_engine import get_engine
from .auth import resolve_request_actor
from .common import extract_text, get_settings, InferenceRequest, perform_upstream_call
from .local_vision import analyze_local_image
from .persistence import get_db
from .swarm_runtime_api import run_text_ingest_demo
from .number_mind import (
    KLM_PATTERN_SIZE,
    GENE_SIZE,
    FORMULA_LAYERS,
    FORMULA_LAYERS_FAST,
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
_SAFE_FILENAME_RE = re.compile(r"^[A-Za-z0-9._-]+$")
_DB = get_db()


def _env_int(name: str, default: int) -> int:
    raw = os.getenv(name)
    if raw is None:
        return default
    try:
        return int(raw.strip())
    except (TypeError, ValueError):
        return default


_CHAT_WORKERS = max(8, min(128, _env_int("KOLIBRI_CHAT_WORKERS", 48)))
_CHAT_EXECUTOR = concurrent.futures.ThreadPoolExecutor(
    max_workers=_CHAT_WORKERS,
    thread_name_prefix="kolibri-chat",
)


# ---------------------------------------------------------------------------
# Модели запросов/ответов
# ---------------------------------------------------------------------------

class ChatRequest(BaseModel):
    message: str = Field(min_length=1, max_length=4096)
    conversation_id: Optional[str] = Field(default=None)
    client_id: Optional[str] = Field(default=None, max_length=120)
    temperature: float = Field(default=0.7, ge=0.0, le=2.0)
    profile: Literal["fast", "balanced", "deep"] = "fast"
    time_budget_ms: int = Field(default=12000, ge=1000, le=120000)
    persona: Literal["assistant", "romantic", "storyteller"] = "assistant"
    memory_enabled: bool = True
    model: Optional[str] = Field(default=None, max_length=120)


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
    # Когнитивное обогащение
    cognitive: Optional[dict] = None
    self_check: Optional[dict] = None
    client_id: Optional[str] = None


class ConversationSummary(BaseModel):
    conversation_id: str
    title: str
    pinned: bool = False
    created_at: float
    updated_at: float


class ConversationListResponse(BaseModel):
    account_id: str
    items: list[ConversationSummary] = Field(default_factory=list)


class ConversationTurnItem(BaseModel):
    role: Literal["user", "assistant", "system"]
    content: str
    created_at: float


class ConversationTurnsResponse(BaseModel):
    account_id: str
    conversation_id: str
    items: list[ConversationTurnItem] = Field(default_factory=list)


class ConversationUpsertRequest(BaseModel):
    conversation_id: Optional[str] = Field(default=None, min_length=1, max_length=220)
    title: Optional[str] = Field(default=None, max_length=240)
    pinned: Optional[bool] = None


class ImagineRequest(BaseModel):
    prompt: str = Field(min_length=1, max_length=2000)
    style: Optional[str] = Field(default=None, max_length=120)
    aspect: str = Field(default="1:1")
    model: Optional[str] = Field(default=None, max_length=120)
    quality: str = Field(default="medium")


class ImagineResponse(BaseModel):
    image_url: str
    revised_prompt: Optional[str] = None
    provider: str = "openai-images"
    model: str
    duration_ms: float


class VisionAnalyzeResponse(BaseModel):
    response: str
    provider: str
    model: str
    duration_ms: float
    mime_type: str
    width: Optional[int] = None
    height: Optional[int] = None


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


class LearnTextDemoRequest(BaseModel):
    text: str = Field(min_length=10, max_length=200000)
    question: str = Field(min_length=1, max_length=1000)
    title: str = Field(default="", max_length=200)
    source: str = Field(default="manual", max_length=120)
    category: str = Field(default="manual", max_length=120)
    conversation_id: Optional[str] = Field(default=None)
    client_id: Optional[str] = Field(default=None, max_length=120)
    temperature: float = Field(default=0.4, ge=0.0, le=2.0)
    profile: Literal["fast", "balanced", "deep"] = "balanced"
    time_budget_ms: int = Field(default=20000, ge=1000, le=120000)
    persona: Literal["assistant", "romantic", "storyteller"] = "assistant"
    memory_enabled: bool = True
    model: Optional[str] = Field(default=None, max_length=120)
    refresh_timeout_sec: int = Field(default=180, ge=30, le=900)


class LearnTextDemoResponse(BaseModel):
    report: str
    chat: ChatResponse
    demo: dict[str, Any] = Field(default_factory=dict)


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
    graph_max_patterns: int = 0
    graph_max_edges: int = 0
    graph_max_degree: int = 0
    graph_documents: int
    graph_tokens: int
    graph_version: int = 0
    delta_log_len: int = 0
    delta_oldest_version: int = 0
    graph_avg_fitness: float
    graph_avg_weight: float
    # Формулы
    formula_generation: int
    formula_fitness: float
    formula_genome_hex: str
    gene_digits: int = GENE_SIZE
    formula_layers: int = FORMULA_LAYERS
    formula_layers_fast: int = FORMULA_LAYERS_FAST
    formula_ops: int = 12
    pattern_size: int = KLM_PATTERN_SIZE
    # C-модель
    c_model_patterns: int
    c_model_edges: int
    c_model_size_mb: float
    c_model_documents: int = 0
    c_model_epoch: int = 0
    c_model_avg_fitness: float = 0.0
    c_model_avg_weight: float = 0.0
    # Эмбеддинги (Фаза 1 AI)
    embedding_vocab_size: int = 0
    embedding_trained_pairs: int = 0
    embedding_epochs: int = 0
    embedding_dim: int = 64
    embedding_avg_norm: float = 0.0
    embedding_last_loss: float = 0.0
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


class LoadedModelsResponse(BaseModel):
    primary_model: str
    model_available: bool
    c_trainer_available: bool
    model_path: str
    model_size_mb: float
    patterns: int
    edges: int
    documents: int = 0
    epoch: int = 0
    formula_generation: int
    embedding_vocab_size: int
    sentence_store_size: int


class QualityBenchmarkResponse(BaseModel):
    run_id: str = ""
    trigger: str = "manual"
    started_at: float = 0.0
    finished_at: float = 0.0
    duration_ms: float = 0.0
    score: float = 0.0
    passed: int = 0
    total: int = 0
    weighted_passed: float = 0.0
    weighted_total: float = 0.0
    pass_rate: float = 0.0
    latency_p50_ms: float = 0.0
    latency_p95_ms: float = 0.0
    confidence_avg: float = 0.0
    confidence_pass_avg: float = 0.0
    confidence_fail_avg: float = 0.0
    placeholder_rate: float = 0.0
    timeout_rate: float = 0.0
    error_rate: float = 0.0
    hallucination_proxy_rate: float = 0.0
    categories: list[dict[str, Any]] = Field(default_factory=list)
    methods: list[dict[str, Any]] = Field(default_factory=list)
    gates: dict[str, Any] = Field(default_factory=dict)
    details: list[dict[str, Any]] = Field(default_factory=list)


class QualityBenchmarkHistoryPoint(BaseModel):
    run_id: str = ""
    trigger: str = "manual"
    started_at: float = 0.0
    finished_at: float = 0.0
    duration_ms: float = 0.0
    score: float = 0.0
    passed: int = 0
    total: int = 0
    weighted_passed: float = 0.0
    weighted_total: float = 0.0
    pass_rate: float = 0.0
    latency_p50_ms: float = 0.0
    latency_p95_ms: float = 0.0
    placeholder_rate: float = 0.0
    timeout_rate: float = 0.0
    error_rate: float = 0.0
    hallucination_proxy_rate: float = 0.0
    gates_overall_pass: bool = False


class QualityBenchmarkHistoryResponse(BaseModel):
    limit: int = 30
    count: int = 0
    items: list[QualityBenchmarkHistoryPoint] = Field(default_factory=list)
    trend: dict[str, Any] = Field(default_factory=dict)


class UniquenessProofResponse(BaseModel):
    run_id: str = ""
    trigger: str = "manual"
    started_at: float = 0.0
    finished_at: float = 0.0
    duration_ms: float = 0.0
    score: float = 0.0
    passed: int = 0
    total: int = 0
    fingerprint: str = ""
    claims: list[str] = Field(default_factory=list)
    details: list[dict[str, Any]] = Field(default_factory=list)


class EmbeddingSimilarityRequest(BaseModel):
    word: str = Field(min_length=1, max_length=256)
    top_k: int = Field(default=10, ge=1, le=100)


class EmbeddingSimilarityResponse(BaseModel):
    word: str
    method: str  # 'embedding' | 'pattern'
    similar: list[dict]
    vocab_size: int
    trained_pairs: int


class EmbeddingTrainResponse(BaseModel):
    status: str
    loss: float
    pairs: int
    epochs: int
    vocab_size: int
    duration_ms: float


class EmbeddingCompareRequest(BaseModel):
    word1: str = Field(min_length=1, max_length=256)
    word2: str = Field(min_length=1, max_length=256)


class EmbeddingCompareResponse(BaseModel):
    word1: str
    word2: str
    embedding_similarity: float
    pattern_similarity: float
    method: str  # 'embedding' | 'pattern'


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


def _resolve_image_size(aspect: str) -> str:
    """Map aspect ratio to OpenAI image size presets."""
    normalized = (aspect or "1:1").strip()
    if normalized == "9:16":
        return "1024x1792"
    if normalized == "16:9":
        return "1792x1024"
    return "1024x1024"


def _resolve_image_tier(quality: str, model_name: str) -> str:
    normalized = (quality or "high").strip().lower()
    model = (model_name or "").strip().lower()
    is_gemini3 = "gemini-3-pro-image-preview" in model
    is_gemini25_flash_image = "gemini-2.5-flash-image" in model

    if normalized == "low":
        return "1K"
    if normalized == "medium":
        if is_gemini3:
            return "1K"
        return "2K"

    # Gemini 3 "high" can exceed ~60s on some network paths.
    # Keep "high" visually strong while reducing timeout risk.
    if is_gemini3:
        return "2K"
    if is_gemini25_flash_image:
        return "2K"
    return "4K"


def _read_env_file(path: str) -> dict[str, str]:
    values: dict[str, str] = {}
    try:
        with open(path, "r", encoding="utf-8") as source:
            for line in source:
                raw = line.strip()
                if not raw or raw.startswith("#") or "=" not in raw:
                    continue
                key, value = raw.split("=", 1)
                key = key.strip()
                value = value.strip().strip("'").strip('"')
                if key:
                    values[key] = value
    except OSError:
        return values
    return values


def _load_backend_file_config() -> dict[str, str]:
    file_config: dict[str, str] = {}
    for env_path in (
        "/srv/kolibri/repo/.env.backend",
        "/home/ladik/kolibri-project/.env.backend",
        ".env.backend",
        ".env",
    ):
        file_config.update(_read_env_file(env_path))
    return file_config


def _extract_image_result(payload: dict[str, Any]) -> tuple[str, Optional[str]]:
    """
    Extract image URL or base64 image from heterogeneous providers.

    Returns:
        (image_url_or_data_uri, revised_prompt)
    """
    revised_prompt: Optional[str] = None

    choices = payload.get("choices")
    if isinstance(choices, list) and choices:
        first_choice = choices[0]
        if isinstance(first_choice, dict):
            message = first_choice.get("message")
            if isinstance(message, dict):
                content = message.get("content")
                if isinstance(content, str) and content.strip():
                    revised_prompt = content.strip()
                elif isinstance(content, list):
                    text_parts = []
                    for part in content:
                        if isinstance(part, dict) and part.get("type") == "text" and isinstance(part.get("text"), str):
                            text_parts.append(part["text"])
                    if text_parts:
                        revised_prompt = "\n".join(text_parts).strip()

                images = message.get("images")
                if isinstance(images, list):
                    for image in images:
                        if not isinstance(image, dict):
                            continue
                        image_url_obj = image.get("image_url") or image.get("imageUrl")
                        if isinstance(image_url_obj, dict):
                            url = image_url_obj.get("url")
                            if isinstance(url, str) and url.strip():
                                return url.strip(), revised_prompt

    data = payload.get("data")
    if isinstance(data, list) and data:
        first = data[0]
        if isinstance(first, dict):
            if isinstance(first.get("revised_prompt"), str):
                revised_prompt = first["revised_prompt"]
            if isinstance(first.get("url"), str) and first["url"].strip():
                return first["url"].strip(), revised_prompt
            b64 = first.get("b64_json") or first.get("base64")
            if isinstance(b64, str) and b64.strip():
                return f"data:image/png;base64,{b64.strip()}", revised_prompt

    output = payload.get("output")
    if isinstance(output, list):
        for item in output:
            if not isinstance(item, dict):
                continue
            if isinstance(item.get("revised_prompt"), str):
                revised_prompt = item["revised_prompt"]
            if isinstance(item.get("url"), str) and item["url"].strip():
                return item["url"].strip(), revised_prompt

            result = item.get("result")
            if isinstance(result, str) and result.strip():
                if result.strip().startswith("http"):
                    return result.strip(), revised_prompt
                return f"data:image/png;base64,{result.strip()}", revised_prompt

            content = item.get("content")
            if isinstance(content, list):
                for piece in content:
                    if not isinstance(piece, dict):
                        continue
                    if isinstance(piece.get("revised_prompt"), str):
                        revised_prompt = piece["revised_prompt"]
                    if isinstance(piece.get("url"), str) and piece["url"].strip():
                        return piece["url"].strip(), revised_prompt
                    b64 = piece.get("b64_json") or piece.get("base64") or piece.get("image_base64")
                    if isinstance(b64, str) and b64.strip():
                        return f"data:image/png;base64,{b64.strip()}", revised_prompt

    raise ValueError("Upstream image API returned no image payload")


def _extract_gemini_image_result(payload: dict[str, Any]) -> tuple[str, Optional[str]]:
    """
    Extract image payload from Google Gemini generateContent response.

    Returns:
        (data_uri_or_url, revised_prompt)
    """
    revised_parts: list[str] = []
    candidates = payload.get("candidates")
    if isinstance(candidates, list):
        for candidate in candidates:
            if not isinstance(candidate, dict):
                continue
            content = candidate.get("content")
            if not isinstance(content, dict):
                continue
            parts = content.get("parts")
            if not isinstance(parts, list):
                continue
            for part in parts:
                if not isinstance(part, dict):
                    continue
                text = part.get("text")
                if isinstance(text, str) and text.strip():
                    revised_parts.append(text.strip())

                inline_data = part.get("inlineData")
                if isinstance(inline_data, dict):
                    b64 = inline_data.get("data")
                    if isinstance(b64, str) and b64.strip():
                        mime = inline_data.get("mimeType")
                        if not isinstance(mime, str) or not mime.strip():
                            mime = "image/png"
                        return f"data:{mime};base64,{b64.strip()}", (
                            "\n".join(revised_parts).strip() or None
                        )

    prompt_feedback = payload.get("promptFeedback")
    if isinstance(prompt_feedback, dict):
        block_reason = prompt_feedback.get("blockReason")
        if block_reason:
            raise ValueError(f"Gemini blocked content: {block_reason}")

    raise ValueError("Gemini returned no image payload")


def _extract_gemini_text_result(payload: dict[str, Any]) -> str:
    candidates = payload.get("candidates")
    if isinstance(candidates, list):
        for candidate in candidates:
            if not isinstance(candidate, dict):
                continue
            content = candidate.get("content")
            if not isinstance(content, dict):
                continue
            parts = content.get("parts")
            if not isinstance(parts, list):
                continue
            text_parts = []
            for part in parts:
                if isinstance(part, dict) and isinstance(part.get("text"), str) and part["text"].strip():
                    text_parts.append(part["text"].strip())
            if text_parts:
                return "\n".join(text_parts).strip()

    prompt_feedback = payload.get("promptFeedback")
    if isinstance(prompt_feedback, dict):
        block_reason = prompt_feedback.get("blockReason")
        if block_reason:
            raise ValueError(f"Gemini blocked content: {block_reason}")

    raise ValueError("Gemini returned no text payload")


def _detect_image_dimensions(binary: bytes) -> tuple[Optional[int], Optional[int]]:
    try:
        from PIL import Image  # type: ignore
        import io

        with Image.open(io.BytesIO(binary)) as image:
            width, height = image.size
            return int(width), int(height)
    except Exception:
        return None, None


def _prepare_image_for_vision(binary: bytes, mime_type: str) -> tuple[bytes, str, Optional[int], Optional[int]]:
    try:
        from PIL import Image  # type: ignore
        import io

        with Image.open(io.BytesIO(binary)) as image:
            width, height = image.size
            target_width = max(32, int(width))
            target_height = max(32, int(height))
            if image.mode not in {"RGB", "RGBA"}:
                image = image.convert("RGBA" if "A" in image.getbands() else "RGB")
            if width < 32 or height < 32:
                image = image.resize((target_width, target_height))

            output = io.BytesIO()
            image.save(output, format="PNG")
            return output.getvalue(), "image/png", target_width, target_height
    except Exception:
        width, height = _detect_image_dimensions(binary)
        return binary, mime_type, width, height


def _resolve_imagine_output_dir(file_config: dict[str, str]) -> str:
    return (
        os.getenv("KOLIBRI_IMAGINE_OUTPUT_DIR")
        or file_config.get("KOLIBRI_IMAGINE_OUTPUT_DIR")
        or "/srv/kolibri/repo/data/generated_images"
    ).strip()


def _persist_data_uri_image(data_uri: str, output_dir: str) -> str:
    if "," not in data_uri:
        raise ValueError("Malformed data URI")

    header, encoded = data_uri.split(",", 1)
    match = re.match(r"^data:image/([a-zA-Z0-9.+-]+);base64$", header)
    if not match:
        raise ValueError("Unsupported image data URI")

    ext_raw = match.group(1).lower()
    ext_map = {
        "jpeg": "jpg",
        "jpg": "jpg",
        "png": "png",
        "webp": "webp",
        "gif": "gif",
    }
    ext = ext_map.get(ext_raw, "png")

    try:
        binary = base64.b64decode(encoded, validate=True)
    except (binascii.Error, ValueError) as exc:
        raise ValueError("Invalid base64 image payload") from exc

    os.makedirs(output_dir, exist_ok=True)
    filename = f"img_{int(time.time())}_{uuid.uuid4().hex[:12]}.{ext}"
    final_path = os.path.join(output_dir, filename)
    tmp_path = f"{final_path}.tmp"

    with open(tmp_path, "wb") as tmp_file:
        tmp_file.write(binary)
    os.replace(tmp_path, final_path)

    return filename


def _build_local_image_data_uri(
    prompt: str,
    style: str | None = None,
    aspect: str = "1:1",
) -> str:
    """Create deterministic local SVG image without external providers."""
    aspect_sizes = {
        "1:1": (1024, 1024),
        "9:16": (1024, 1820),
        "16:9": (1820, 1024),
    }
    width, height = aspect_sizes.get(aspect, (1024, 1024))
    digest = hashlib.sha256(prompt.encode("utf-8")).hexdigest()
    c1 = f"#{digest[0:6]}"
    c2 = f"#{digest[6:12]}"
    accent = f"#{digest[12:18]}"

    title = html.escape(prompt.strip()[:120] or "Kolibri Imagine")
    subtitle = html.escape((style or "Kolibri local render").strip()[:80])

    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<defs>
  <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0%" stop-color="{c1}"/>
    <stop offset="100%" stop-color="{c2}"/>
  </linearGradient>
  <filter id="blur">
    <feGaussianBlur stdDeviation="90"/>
  </filter>
</defs>
<rect width="100%" height="100%" fill="url(#bg)"/>
<circle cx="{int(width * 0.72)}" cy="{int(height * 0.22)}" r="{int(min(width, height) * 0.18)}" fill="{accent}" opacity="0.45" filter="url(#blur)"/>
<rect x="{int(width * 0.08)}" y="{int(height * 0.62)}" width="{int(width * 0.84)}" height="{int(height * 0.26)}" rx="28" fill="#070c14" fill-opacity="0.72" stroke="#5b6a83" stroke-opacity="0.45"/>
<text x="{int(width * 0.12)}" y="{int(height * 0.72)}" fill="#eaf2ff" font-family="Arial, sans-serif" font-size="{max(26, int(width * 0.034))}" font-weight="700">{title}</text>
<text x="{int(width * 0.12)}" y="{int(height * 0.79)}" fill="#c9d6ea" font-family="Arial, sans-serif" font-size="{max(20, int(width * 0.024))}" opacity="0.9">{subtitle}</text>
<text x="{int(width * 0.12)}" y="{int(height * 0.86)}" fill="#8ca3c8" font-family="Arial, sans-serif" font-size="{max(18, int(width * 0.02))}">Rendered locally by Kolibri AI</text>
</svg>"""
    encoded = base64.b64encode(svg.encode("utf-8")).decode("ascii")
    return f"data:image/svg+xml;base64,{encoded}"


def _chat_max_tokens(profile: str) -> int:
    mode = (profile or "fast").strip().lower()
    if mode == "deep":
        return 1400
    if mode == "balanced":
        return 900
    return 600


def _build_llm_chat_prompt(
    message: str,
    context_text: str,
    *,
    persona: str = "assistant",
    memory_enabled: bool = True,
    model_name: str | None = None,
) -> str:
    # Один prompt для провайдеров с разными форматами API.
    parts = [
        (
            "Ты Колибри AI. Отвечай по-русски, по делу и без выдуманных фактов. "
            "Если данных недостаточно — прямо скажи, что не уверен, и предложи уточнение."
        ),
    ]
    if persona == "romantic":
        parts.append("Стиль ответа: тёплый, мягкий, человечный, но без лишней воды.")
    elif persona == "storyteller":
        parts.append("Стиль ответа: как короткий связный рассказ с образной подачей, но по делу.")
    if model_name:
        parts.append(f"Текущий режим интерфейса: {model_name.strip()[:120]}")
    if not memory_enabled:
        parts.append(
            "Персональная память пользователя отключена. Не опирайся на старые персональные факты, "
            "если они не даны в этом диалоге."
        )
    if context_text.strip():
        parts.append("Контекст диалога (последние реплики):\n" + context_text.strip())
    parts.append("Текущий вопрос пользователя:\n" + message.strip())
    parts.append("Дай полезный, связный ответ.")
    prompt = "\n\n".join(parts).strip()
    max_chars_raw = os.getenv("KOLIBRI_LLM_MAX_PROMPT_CHARS", "12000").strip()
    try:
        max_chars = max(2000, int(max_chars_raw))
    except ValueError:
        max_chars = 12000
    if len(prompt) > max_chars:
        return prompt[-max_chars:]
    return prompt


async def _run_engine_chat(req: ChatRequest) -> ChatResponse:
    engine = get_engine()
    try:
        loop = asyncio.get_running_loop()
        fn = functools.partial(
            engine.chat,
            message=req.message,
            conversation_id=req.conversation_id,
            client_id=req.client_id,
            temperature=req.temperature,
            response_profile=req.profile,
            time_budget_ms=req.time_budget_ms,
            persona=req.persona,
            memory_enabled=req.memory_enabled,
            model_name=req.model,
        )
        result = await loop.run_in_executor(_CHAT_EXECUTOR, fn)
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
        cognitive=result.get("cognitive"),
        self_check=result.get("self_check"),
        client_id=result.get("client_id"),
    )


def _session_title_from_message(message: str) -> str:
    text = re.sub(r"\s+", " ", str(message or "").strip())
    if not text:
        return "Новый чат"
    return text[:48]


def _upsert_conversation_session(
    client_id: str,
    conversation_id: str,
    *,
    title: str | None = None,
    pinned: bool | None = None,
) -> None:
    try:
        _DB.upsert_conversation_session(
            client_id=client_id,
            conversation_id=conversation_id,
            title=title,
            pinned=pinned,
            updated_at=time.time(),
        )
    except Exception:
        # Session metadata must not break ordinary chat.
        return


def _format_domain_delta_line(items: list[dict[str, Any]]) -> str:
    if not items:
        return "Новый документ сохранён в живую формульную память."
    parts: list[str] = []
    for item in items[:3]:
        domain = str(item.get("domain", "root") or "root")
        delta = int(item.get("delta", 0) or 0)
        sign = "+" if delta >= 0 else ""
        parts.append(f"{domain}: {sign}{delta}")
    return "Прирост памяти по доменам: " + ", ".join(parts) + "."


def _format_swarm_demo_report(demo: dict[str, Any]) -> str:
    parts: list[str] = []
    parts.append(_format_domain_delta_line(list(demo.get("domain_delta", []) or [])))

    comparison_summary = demo.get("comparison_summary")
    if isinstance(comparison_summary, dict):
        parts.append(
            "Сравнение усвоения: "
            f"1 узел {float(comparison_summary.get('single_hit_before', 0.0)):.3f} -> "
            f"{float(comparison_summary.get('single_hit_after', 0.0)):.3f}, "
            f"10 узлов {float(comparison_summary.get('swarm_hit_before', 0.0)):.3f} -> "
            f"{float(comparison_summary.get('swarm_hit_after', 0.0)):.3f}, "
            f"преимущество роя {float(comparison_summary.get('swarm_vs_single_before', 0.0)):+.3f} -> "
            f"{float(comparison_summary.get('swarm_vs_single_after', 0.0)):+.3f}."
        )
        focus_domain = str(comparison_summary.get("focus_domain", "") or "").strip()
        if focus_domain:
            parts.append(
                "Лучший прирост по домену "
                f"{focus_domain}: "
                f"документов {int(comparison_summary.get('focus_domain_documents_delta', 0) or 0):+d}, "
                f"рой {float(comparison_summary.get('focus_domain_swarm_hit_delta', 0.0)):+.3f}, "
                f"1 узел {float(comparison_summary.get('focus_domain_single_hit_delta', 0.0)):+.3f}."
            )

    domain_scores = list(demo.get("domain_score_delta", []) or [])
    if domain_scores:
        top = domain_scores[0]
        parts.append(
            "По домену "
            f"{str(top.get('domain', 'root'))} "
            f"рой улучшил hit_ratio на {float(top.get('swarm_hit_delta', 0.0)):+.3f}, "
            f"а преимущество над 1 узлом изменилось на {float(top.get('swarm_vs_single_delta_change', 0.0)):+.3f}."
        )

    knowledge_delta = demo.get("knowledge_delta")
    if isinstance(knowledge_delta, dict):
        parts.append(
            "После пересчёта: "
            f"документов {int(knowledge_delta.get('documents_delta', 0) or 0):+d}, "
            f"рой {float(knowledge_delta.get('swarm_hit_delta', 0.0)):+.3f}, "
            f"1 узел {float(knowledge_delta.get('single_hit_delta', 0.0)):+.3f}."
        )

    return " ".join(part.strip() for part in parts if part).strip()


def _coerce_chat_response(value: Any) -> ChatResponse:
    if isinstance(value, ChatResponse):
        return value
    if isinstance(value, dict):
        return ChatResponse.model_validate(value)
    payload = {
        field_name: getattr(value, field_name)
        for field_name in ChatResponse.model_fields
        if hasattr(value, field_name)
    }
    return ChatResponse.model_validate(payload)


def _upgrade_demo_chat_with_c_core(req: LearnTextDemoRequest, chat: ChatResponse) -> ChatResponse:
    if chat.method == "c-core-formula":
        return chat

    title = (req.title or "").strip()
    engine = get_engine()
    c_inference = getattr(engine, "c_inference", None)
    if not title or not c_inference or not getattr(c_inference, "available", False):
        return chat

    candidate_questions: list[str] = [req.question.strip()]
    normalized_question = req.question.strip().lower()
    if not re.search(r"^(что\s+такое|кто\s+так(?:ой|ая|ие)|объясни|расскажи\s+(?:о|про)|что\s+изучает|чем\s+занимается)\b", normalized_question):
        candidate_questions.append(f"что такое {title}")
    elif title.lower() not in normalized_question:
        candidate_questions.append(f"что такое {title}")

    for candidate in candidate_questions:
        payload = c_inference.query(candidate, strategy="formula")
        if not engine._is_valid_c_formula_answer(candidate, payload):
            continue
        response = str(payload.get("response", "") or "").strip()
        if not response:
            continue
        return ChatResponse(
            response=response,
            confidence=max(0.45, min(0.92, float(payload.get("confidence", chat.confidence) or chat.confidence))),
            conversation_id=chat.conversation_id,
            sources=["c-core-formula", "demo-memory"],
            knowledge_hits=int(payload.get("knowledge_hits", chat.knowledge_hits) or chat.knowledge_hits),
            method="c-core-formula",
            duration_ms=chat.duration_ms,
            model_available=chat.model_available,
            formula_data=chat.formula_data,
            graph_stats=chat.graph_stats,
            cognitive=chat.cognitive,
            self_check=chat.self_check,
            client_id=chat.client_id,
        )

    return chat


def _split_demo_text_sentences(text: str) -> list[str]:
    raw_parts = re.split(r"(?<=[.!?])\s+|\n+", (text or "").strip())
    return [part.strip().rstrip(".!?") for part in raw_parts if part and part.strip()]


def _build_demo_grounded_chat(req: LearnTextDemoRequest, chat: ChatResponse) -> ChatResponse:
    if chat.method == "c-core-formula":
        return chat

    title = (req.title or "").strip()
    sentences = _split_demo_text_sentences(req.text)
    if not sentences:
        return chat

    question = (req.question or "").strip().lower()

    def _anchor(sentence: str) -> str:
        if not title or not sentence:
            return sentence
        if sentence.lower().startswith(title.lower()):
            return sentence
        return f"{title} — {sentence}"

    answer = ""
    if "что изучает" in question or "изучает" in question:
        candidate = next((s for s in sentences if "изучает" in s.lower()), "")
        answer = candidate or _anchor(sentences[0])
    elif "чем занимается" in question or "занимается" in question:
        candidate = next(
            (
                s for s in sentences
                if any(token in s.lower() for token in ("занимается", "определяет", "регулирует", "описывает"))
            ),
            "",
        )
        answer = candidate or sentences[min(1, len(sentences) - 1)]
    elif re.search(r"^(что\s+такое|кто\s+так(?:ой|ая|ие)|объясни|расскажи\s+(?:о|про))\b", question):
        primary = _anchor(sentences[0])
        if len(sentences) > 1 and ("объясни" in question or "расскажи" in question):
            answer = f"{primary}. {sentences[1]}"
        else:
            answer = primary
    else:
        answer = _anchor(sentences[0])

    answer = answer.strip().rstrip(".")
    if not answer:
        return chat

    return ChatResponse(
        response=f"{answer}.",
        confidence=max(chat.confidence, 0.72),
        conversation_id=chat.conversation_id,
        sources=["demo-input-text"],
        knowledge_hits=max(chat.knowledge_hits, 1),
        method="demo-source-grounded",
        duration_ms=chat.duration_ms,
        model_available=chat.model_available,
        formula_data=chat.formula_data,
        graph_stats=chat.graph_stats,
        cognitive=chat.cognitive,
        self_check=chat.self_check,
        client_id=chat.client_id,
    )


# ---------------------------------------------------------------------------
# Эндпоинты
# ---------------------------------------------------------------------------

@router.post("/chat", response_model=ChatResponse)
async def ai_chat(req: ChatRequest, request: Request) -> ChatResponse:
    """
    Главный AI чат — Числовое Формульное Мышление.
    
    Каждый ответ содержит:
    - Текстовый ответ (из графа знаний + C-модели)
    - Числовые паттерны слов запроса и ответа
    - Формульный predict (100-слойная сеть)
    - Статистику графа знаний
    """
    settings = get_settings()
    actor = resolve_request_actor(request, req.client_id)
    effective_req = req.model_copy(
        update={"client_id": str(actor.get("account_key", req.client_id or "global") or "global")}
    )

    # LLM режим: /api/v1/ai/chat отвечает через upstream модель.
    if settings.response_mode == "llm":
        engine = get_engine()
        if effective_req.client_id:
            client_key = engine._sanitize_client_id(effective_req.client_id)
        elif effective_req.conversation_id:
            client_key = engine._sanitize_client_id(f"conv:{effective_req.conversation_id}")
        else:
            client_key = engine._sanitize_client_id(None)
        scoped_conversation_id = engine._scoped_conversation_id(effective_req.conversation_id, client_key)
        conversation = engine.get_or_create_conversation(scoped_conversation_id, client_id=client_key)
        context_text = conversation.context_text(last_n=8)
        prompt = _build_llm_chat_prompt(
            effective_req.message,
            context_text,
            persona=effective_req.persona,
            memory_enabled=effective_req.memory_enabled,
            model_name=effective_req.model,
        )
        infer_req = InferenceRequest(
            prompt=prompt,
            mode=effective_req.profile,
            temperature=effective_req.temperature,
            max_tokens=_chat_max_tokens(effective_req.profile),
        )

        try:
            text, latency_ms, provider = await perform_upstream_call(infer_req, settings)
        except HTTPException:
            if settings.llm_fallback_engine:
                return await _run_engine_chat(effective_req)
            raise
        except Exception as e:
            if settings.llm_fallback_engine:
                return await _run_engine_chat(effective_req)
            raise HTTPException(status_code=502, detail=f"LLM call failed: {e}")

        answer = (text or "").strip()
        if not answer:
            if settings.llm_fallback_engine:
                return await _run_engine_chat(effective_req)
            raise HTTPException(status_code=502, detail="LLM returned empty response")

        conversation.add("user", effective_req.message)
        conversation.add("assistant", answer)
        engine._persist_conversation_turn(client_key, conversation.id, "user", effective_req.message)
        engine._persist_conversation_turn(client_key, conversation.id, "assistant", answer)
        _upsert_conversation_session(
            client_key,
            conversation.id,
            title=_session_title_from_message(effective_req.message),
        )

        method = f"llm:{provider}" if provider else "llm"
        return ChatResponse(
            response=answer,
            confidence=0.78,
            conversation_id=conversation.id,
            sources=[provider] if provider else ["llm"],
            knowledge_hits=0,
            method=method,
            duration_ms=round(float(latency_ms), 1),
            model_available=True,
            formula_data=None,
            graph_stats=None,
            cognitive=None,
            self_check=None,
            client_id=client_key,
        )

    result = await _run_engine_chat(effective_req)
    _upsert_conversation_session(
        str(result.client_id or effective_req.client_id or "global"),
        result.conversation_id,
        title=_session_title_from_message(effective_req.message),
    )
    return result


@router.post("/demo/learn/text", response_model=LearnTextDemoResponse)
async def ai_demo_learn_text(req: LearnTextDemoRequest, request: Request) -> LearnTextDemoResponse:
    demo_payload = await asyncio.to_thread(
        run_text_ingest_demo,
        text=req.text,
        title=req.title,
        source=req.source,
        category=req.category,
        refresh_timeout_sec=req.refresh_timeout_sec,
    )
    demo = dict(demo_payload.get("demo", {}) or {})
    chat = await _run_engine_chat(
        ChatRequest(
            message=req.question,
            conversation_id=req.conversation_id,
            client_id=str(resolve_request_actor(request, req.client_id).get("account_key", req.client_id or "global")),
            temperature=req.temperature,
            profile=req.profile,
            time_budget_ms=req.time_budget_ms,
            persona=req.persona,
            memory_enabled=req.memory_enabled,
            model=req.model,
        )
    )
    chat_payload = _coerce_chat_response(chat)
    chat_payload = _upgrade_demo_chat_with_c_core(req, chat_payload)
    chat_payload = _build_demo_grounded_chat(req, chat_payload)
    report = _format_swarm_demo_report(demo)
    return LearnTextDemoResponse(report=report, chat=chat_payload, demo=demo)


@router.post("/chat/stream")
async def ai_chat_stream(req: ChatRequest, request: Request) -> StreamingResponse:
    """
    SSE-стриминг AI ответа — слова передаются по мере генерации.

    Формат SSE:
    - event: thinking — шаги Chain-of-Thought
    - event: token    — очередное слово/фраза ответа
    - event: done     — полный результат (JSON)
    """
    settings = get_settings()
    effective_req = req.model_copy(
        update={"client_id": str(resolve_request_actor(request, req.client_id).get("account_key", req.client_id or "global"))}
    )
    if settings.response_mode == "llm":
        async def llm_stream_generator():
            try:
                result = await ai_chat(effective_req, request)
            except Exception as e:
                yield f"event: error\ndata: {json.dumps({'error': str(e)}, ensure_ascii=False)}\n\n"
                return

            response_text = result.response or ""
            words = response_text.split()
            for i, word in enumerate(words):
                chunk = word + (" " if i < len(words) - 1 else "")
                yield f"event: token\ndata: {json.dumps({'text': chunk}, ensure_ascii=False)}\n\n"
                await asyncio.sleep(0.01)

            done_data = {
                "confidence": result.confidence,
                "method": result.method,
                "duration_ms": result.duration_ms,
                "knowledge_hits": result.knowledge_hits,
                "conversation_id": result.conversation_id,
                "cached": False,
            }
            yield f"event: done\ndata: {json.dumps(done_data, ensure_ascii=False)}\n\n"

        return StreamingResponse(
            llm_stream_generator(),
            media_type="text/event-stream",
            headers={
                "Cache-Control": "no-cache",
                "Connection": "keep-alive",
                "X-Accel-Buffering": "no",
            },
        )

    engine = get_engine()

    async def stream_generator():
        # Фаза 1: CoT thinking
        try:
            thinking_steps = engine._chain_of_thought.analyze_query(effective_req.message)
            for step in thinking_steps:
                payload = {
                    "type": step.step_type.name,
                    "description": step.description,
                }
                yield f"event: thinking\ndata: {json.dumps(payload, ensure_ascii=False)}\n\n"
                await asyncio.sleep(0.01)  # маленькая пауза для SSE
        except Exception:
            pass

        # Фаза 2: генерация ответа (в отдельном потоке чтобы не блокировать).
        # Во время длительной генерации отправляем keepalive, чтобы прокси не рвал SSE по idle timeout.
        result: dict[str, Any]
        chat_task = asyncio.create_task(
            asyncio.to_thread(
                engine.chat,
                message=effective_req.message,
                conversation_id=effective_req.conversation_id,
                client_id=effective_req.client_id,
                temperature=effective_req.temperature,
                response_profile=effective_req.profile,
                time_budget_ms=effective_req.time_budget_ms,
                persona=effective_req.persona,
                memory_enabled=effective_req.memory_enabled,
                model_name=effective_req.model,
            )
        )
        while True:
            try:
                result = await asyncio.wait_for(asyncio.shield(chat_task), timeout=5.0)
                break
            except asyncio.TimeoutError:
                yield "event: keepalive\ndata: {}\n\n"
                continue
            except Exception as e:
                yield f"event: error\ndata: {json.dumps({'error': str(e)}, ensure_ascii=False)}\n\n"
                return

        _upsert_conversation_session(
            str(result.get("client_id") or effective_req.client_id or "global"),
            str(result.get("conversation_id", "") or ""),
            title=_session_title_from_message(effective_req.message),
        )

        # Фаза 3: стриминг ответа по словам
        response_text = result.get("response", "")
        words = response_text.split()
        for i, word in enumerate(words):
            chunk = word + (" " if i < len(words) - 1 else "")
            yield f"event: token\ndata: {json.dumps({'text': chunk}, ensure_ascii=False)}\n\n"
            await asyncio.sleep(0.01)  # эмуляция streaming delay

        # Фаза 4: финальный результат
        done_data = {
            "confidence": result.get("confidence", 0.0),
            "method": result.get("method", "unknown"),
            "duration_ms": result.get("duration_ms", 0.0),
            "knowledge_hits": result.get("knowledge_hits", 0),
            "conversation_id": result.get("conversation_id", ""),
            "cached": result.get("cached", False),
        }
        yield f"event: done\ndata: {json.dumps(done_data, ensure_ascii=False)}\n\n"

    return StreamingResponse(
        stream_generator(),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "Connection": "keep-alive",
            "X-Accel-Buffering": "no",
        },
    )


@router.post("/imagine", response_model=ImagineResponse)
async def ai_imagine(req: ImagineRequest) -> ImagineResponse:
    """
    Генерация изображения.

    Стратегия:
    1) Google Gemini API (если есть GOOGLE/KOLIBRI_GEMINI ключ)
    2) OpenRouter fallback (если есть валидный sk-or-v1 ключ)

    Переменные окружения:
      - KOLIBRI_GEMINI_API_KEY / GOOGLE_API_KEY / GEMINI_API_KEY
      - KOLIBRI_GEMINI_IMAGE_MODEL / KOLIBRI_GEMINI_IMAGE_FALLBACK_MODEL
      - KOLIBRI_GEMINI_ENDPOINT (default: https://generativelanguage.googleapis.com/v1beta)
      - KOLIBRI_OPENROUTER_API_KEY / OPENROUTER_API_KEY
      - KOLIBRI_OPENROUTER_IMAGE_MODEL (primary)
      - KOLIBRI_OPENROUTER_IMAGE_FALLBACK_MODEL (fallback)
      - KOLIBRI_OPENROUTER_ENDPOINT
      - KOLIBRI_OPENROUTER_SITE_URL
      - KOLIBRI_OPENROUTER_APP_NAME
      - KOLIBRI_IMAGE_TIMEOUT (seconds)
    """
    started_total = time.perf_counter()
    file_config: dict[str, str] = {}
    for env_path in (
        "/srv/kolibri/repo/.env.backend",
        "/home/ladik/kolibri-project/.env.backend",
        ".env.backend",
        ".env",
    ):
        file_config.update(_read_env_file(env_path))

    aspect = (req.aspect or "1:1").strip()
    if aspect not in {"1:1", "9:16", "16:9"}:
        aspect = "1:1"

    quality = (req.quality or "medium").strip()
    if quality not in {"low", "medium", "high", "auto"}:
        quality = "medium"

    final_prompt = req.prompt.strip()
    if req.style and req.style.strip():
        final_prompt = f"{final_prompt}\nStyle: {req.style.strip()}"

    settings = get_settings()
    if settings.local_only:
        return ImagineResponse(
            image_url=_build_local_image_data_uri(
                prompt=final_prompt,
                style=req.style,
                aspect=aspect,
            ),
            revised_prompt=final_prompt,
            provider="kolibri-local-imagine",
            model=(req.model or "kolibri-svg-v1").strip(),
            duration_ms=round((time.perf_counter() - started_total) * 1000.0, 2),
        )

    openrouter_api_key = (
        os.getenv("KOLIBRI_OPENROUTER_API_KEY")
        or os.getenv("OPENROUTER_API_KEY")
        or file_config.get("KOLIBRI_OPENROUTER_API_KEY")
        or file_config.get("OPENROUTER_API_KEY")
    )
    openai_api_key = (
        os.getenv("KOLIBRI_OPENAI_API_KEY")
        or os.getenv("OPENAI_API_KEY")
        or file_config.get("KOLIBRI_OPENAI_API_KEY")
        or file_config.get("OPENAI_API_KEY")
    )
    gemini_api_key = (
        os.getenv("KOLIBRI_GEMINI_API_KEY")
        or os.getenv("GOOGLE_API_KEY")
        or os.getenv("GEMINI_API_KEY")
        or file_config.get("KOLIBRI_GEMINI_API_KEY")
        or file_config.get("GOOGLE_API_KEY")
        or file_config.get("GEMINI_API_KEY")
    )
    # Совместимость: если в OpenRouter переменной лежит Google key (AIza...),
    # используем его как Gemini key.
    if (
        not gemini_api_key
        and openrouter_api_key
        and openrouter_api_key.strip().startswith("AIza")
    ):
        gemini_api_key = openrouter_api_key.strip()

    endpoint = (
        os.getenv("KOLIBRI_OPENROUTER_ENDPOINT")
        or file_config.get("KOLIBRI_OPENROUTER_ENDPOINT")
        or "https://openrouter.ai/api/v1/chat/completions"
    ).strip()
    gemini_endpoint = (
        os.getenv("KOLIBRI_GEMINI_ENDPOINT")
        or file_config.get("KOLIBRI_GEMINI_ENDPOINT")
        or "https://generativelanguage.googleapis.com/v1beta"
    ).strip().rstrip("/")
    primary_model = (
        req.model
        or os.getenv("KOLIBRI_OPENROUTER_IMAGE_MODEL")
        or file_config.get("KOLIBRI_OPENROUTER_IMAGE_MODEL")
        or os.getenv("KOLIBRI_IMAGE_MODEL")
        or file_config.get("KOLIBRI_IMAGE_MODEL")
        or "google/gemini-3-pro-image-preview"
    ).strip()
    fallback_model = (
        os.getenv("KOLIBRI_OPENROUTER_IMAGE_FALLBACK_MODEL")
        or file_config.get("KOLIBRI_OPENROUTER_IMAGE_FALLBACK_MODEL")
        or "google/gemini-2.5-flash-image"
    ).strip()

    timeout_raw = (
        os.getenv("KOLIBRI_IMAGE_TIMEOUT")
        or file_config.get("KOLIBRI_IMAGE_TIMEOUT")
        or "120"
    )
    try:
        timeout = float(timeout_raw)
    except ValueError:
        timeout = 120.0

    gemini_prompt = f"{final_prompt}\nAspect ratio: {aspect}\nQuality: {quality}"

    referer = (
        os.getenv("KOLIBRI_OPENROUTER_SITE_URL")
        or file_config.get("KOLIBRI_OPENROUTER_SITE_URL")
        or "https://kolibriai.ru"
    ).strip()
    app_name = (
        os.getenv("KOLIBRI_OPENROUTER_APP_NAME")
        or file_config.get("KOLIBRI_OPENROUTER_APP_NAME")
        or "Kolibri AI"
    ).strip()

    def finalize_image_url(image_url: str) -> str:
        final_image_url = image_url
        if image_url.startswith("data:image/"):
            output_dir = _resolve_imagine_output_dir(file_config)
            try:
                filename = _persist_data_uri_image(image_url, output_dir)
            except Exception as exc:
                raise HTTPException(
                    status_code=502,
                    detail=f"Failed to persist generated image: {exc}",
                ) from exc

            public_base = (
                os.getenv("KOLIBRI_PUBLIC_BASE_URL")
                or file_config.get("KOLIBRI_PUBLIC_BASE_URL")
                or referer
                or ""
            ).strip().rstrip("/")
            image_path = f"/api/v1/ai/imagine/files/{filename}"
            final_image_url = f"{public_base}{image_path}" if public_base else image_path
        return final_image_url

    gemini_error: str | None = None
    if gemini_api_key:
        gemini_primary_model = (
            req.model
            or os.getenv("KOLIBRI_GEMINI_IMAGE_MODEL")
            or file_config.get("KOLIBRI_GEMINI_IMAGE_MODEL")
            or "gemini-2.5-flash-image"
        ).strip()
        gemini_fallback_model = (
            os.getenv("KOLIBRI_GEMINI_IMAGE_FALLBACK_MODEL")
            or file_config.get("KOLIBRI_GEMINI_IMAGE_FALLBACK_MODEL")
            or "gemini-2.0-flash-exp-image-generation"
        ).strip()
        if "/" in gemini_primary_model:
            gemini_primary_model = gemini_primary_model.split("/", 1)[1]
        if "/" in gemini_fallback_model:
            gemini_fallback_model = gemini_fallback_model.split("/", 1)[1]

        async def generate_with_gemini(model_name: str) -> tuple[str, Optional[str], float]:
            url = f"{gemini_endpoint}/models/{model_name}:generateContent?key={gemini_api_key}"
            payload: dict[str, Any] = {
                "contents": [
                    {"parts": [{"text": gemini_prompt}]},
                ],
                "generationConfig": {
                    "responseModalities": ["TEXT", "IMAGE"],
                },
            }

            retries = 3
            backoff_sec = 1.2
            last_error: Exception | None = None

            for attempt in range(1, retries + 1):
                started = time.perf_counter()
                try:
                    async with httpx.AsyncClient(timeout=timeout) as client:
                        response = await client.post(
                            url,
                            json=payload,
                            headers={"Content-Type": "application/json"},
                        )
                except httpx.RequestError as exc:
                    last_error = exc
                    if attempt < retries:
                        await asyncio.sleep(backoff_sec * attempt)
                        continue
                    raise HTTPException(
                        status_code=502,
                        detail=f"Gemini request failed: {exc}",
                    ) from exc

                duration_ms = (time.perf_counter() - started) * 1000.0

                if response.status_code >= 500 and attempt < retries:
                    await asyncio.sleep(backoff_sec * attempt)
                    continue

                try:
                    response.raise_for_status()
                except httpx.HTTPStatusError as exc:
                    detail = exc.response.text[:1000]
                    raise HTTPException(
                        status_code=502,
                        detail=f"Gemini returned {exc.response.status_code}: {detail}",
                    ) from exc

                try:
                    payload_json = response.json()
                except json.JSONDecodeError as exc:
                    raise HTTPException(
                        status_code=502,
                        detail="Gemini returned invalid JSON",
                    ) from exc

                image_url, revised_prompt = _extract_gemini_image_result(payload_json)
                return image_url, revised_prompt, duration_ms

            raise HTTPException(status_code=502, detail=f"Gemini request failed: {last_error}")

        gemini_used_model = gemini_primary_model
        try:
            image_url, revised_prompt, duration_ms = await generate_with_gemini(gemini_primary_model)
        except Exception as first_error:
            should_fallback = (
                gemini_fallback_model
                and gemini_fallback_model != gemini_primary_model
                and req.model is None
            )
            if not should_fallback:
                gemini_error = str(first_error)
            else:
                gemini_used_model = gemini_fallback_model
                try:
                    image_url, revised_prompt, duration_ms = await generate_with_gemini(gemini_fallback_model)
                except Exception as second_error:
                    gemini_error = str(second_error)
        else:
            return ImagineResponse(
                image_url=finalize_image_url(image_url),
                revised_prompt=revised_prompt,
                provider="google-gemini",
                model=gemini_used_model,
                duration_ms=round(duration_ms, 2),
            )

    if not openrouter_api_key or openrouter_api_key.strip().startswith("AIza"):
        if gemini_error:
            raise HTTPException(status_code=502, detail=gemini_error)
        raise HTTPException(
            status_code=503,
            detail=(
                "No valid image provider key configured. "
                "Set KOLIBRI_GEMINI_API_KEY/GOOGLE_API_KEY or OpenRouter sk-or-v1 key."
            ),
        )

    headers = {
        "Authorization": f"Bearer {openrouter_api_key}",
        "Content-Type": "application/json",
    }
    if referer:
        headers["HTTP-Referer"] = referer
    if app_name:
        headers["X-Title"] = app_name

    async def generate_with_model(model_name: str) -> tuple[str, Optional[str], float]:
        image_size = _resolve_image_tier(quality, model_name)
        payload: dict[str, Any] = {
            "model": model_name,
            "messages": [{"role": "user", "content": final_prompt}],
            "modalities": ["image", "text"],
            "image_config": {
                "aspect_ratio": aspect,
                "image_size": image_size,
            },
        }

        retries = 3
        backoff_sec = 1.2
        last_error: Exception | None = None

        for attempt in range(1, retries + 1):
            started = time.perf_counter()
            try:
                async with httpx.AsyncClient(timeout=timeout) as client:
                    response = await client.post(endpoint, json=payload, headers=headers)
            except httpx.RequestError as exc:
                last_error = exc
                if attempt < retries:
                    await asyncio.sleep(backoff_sec * attempt)
                    continue
                raise HTTPException(status_code=502, detail=f"OpenRouter request failed: {exc}") from exc

            duration_ms = (time.perf_counter() - started) * 1000.0

            # 5xx часто бывают временными; пробуем повторно перед фейлом/fallback.
            if response.status_code >= 500 and attempt < retries:
                await asyncio.sleep(backoff_sec * attempt)
                continue

            try:
                response.raise_for_status()
            except httpx.HTTPStatusError as exc:
                detail = exc.response.text[:1000]
                raise HTTPException(
                    status_code=502,
                    detail=f"OpenRouter returned {exc.response.status_code}: {detail}",
                ) from exc

            try:
                payload_json = response.json()
            except json.JSONDecodeError as exc:
                raise HTTPException(
                    status_code=502,
                    detail="OpenRouter returned invalid JSON",
                ) from exc

            image_url, revised_prompt = _extract_image_result(payload_json)
            return image_url, revised_prompt, duration_ms

        raise HTTPException(status_code=502, detail=f"OpenRouter request failed: {last_error}")

    used_model = primary_model
    try:
        image_url, revised_prompt, duration_ms = await generate_with_model(primary_model)
    except Exception as first_error:
        should_fallback = (
            fallback_model
            and fallback_model != primary_model
            and req.model is None
        )
        if not should_fallback:
            if isinstance(first_error, HTTPException):
                raise first_error
            raise HTTPException(status_code=502, detail=str(first_error)) from first_error

        used_model = fallback_model
        try:
            image_url, revised_prompt, duration_ms = await generate_with_model(fallback_model)
        except Exception as second_error:
            if isinstance(second_error, HTTPException):
                raise second_error
            raise HTTPException(status_code=502, detail=str(second_error)) from second_error

    return ImagineResponse(
        image_url=finalize_image_url(image_url),
        revised_prompt=revised_prompt,
        provider="openrouter",
        model=used_model,
        duration_ms=round(duration_ms, 2),
    )


@router.post("/vision/analyze", response_model=VisionAnalyzeResponse)
async def ai_vision_analyze(
    file: UploadFile = File(...),
    prompt: str = Form(default="Опиши изображение и выдели главное по-русски."),
) -> VisionAnalyzeResponse:
    started_total = time.perf_counter()
    file_config = _load_backend_file_config()

    mime_type = (file.content_type or "").strip().lower()
    if not mime_type.startswith("image/"):
        raise HTTPException(status_code=400, detail="Поддерживаются только изображения")

    binary = await file.read()
    if not binary:
        raise HTTPException(status_code=400, detail="Пустой файл")
    if len(binary) > 12 * 1024 * 1024:
        raise HTTPException(status_code=413, detail="Файл слишком большой")

    binary, mime_type, width, height = _prepare_image_for_vision(binary, mime_type)
    prompt_text = (prompt or "").strip() or "Опиши изображение и выдели главное по-русски."
    data_b64 = base64.b64encode(binary).decode("ascii")
    data_uri = f"data:{mime_type};base64,{data_b64}"
    vision_mode = (
        os.getenv("KOLIBRI_VISION_MODE")
        or file_config.get("KOLIBRI_VISION_MODE")
        or "local"
    ).strip().lower()

    openrouter_api_key = (
        os.getenv("KOLIBRI_OPENROUTER_API_KEY")
        or os.getenv("OPENROUTER_API_KEY")
        or file_config.get("KOLIBRI_OPENROUTER_API_KEY")
        or file_config.get("OPENROUTER_API_KEY")
    )
    openai_api_key = (
        os.getenv("KOLIBRI_OPENAI_API_KEY")
        or os.getenv("OPENAI_API_KEY")
        or file_config.get("KOLIBRI_OPENAI_API_KEY")
        or file_config.get("OPENAI_API_KEY")
    )
    gemini_api_key = (
        os.getenv("KOLIBRI_GEMINI_API_KEY")
        or os.getenv("GOOGLE_API_KEY")
        or os.getenv("GEMINI_API_KEY")
        or file_config.get("KOLIBRI_GEMINI_API_KEY")
        or file_config.get("GOOGLE_API_KEY")
        or file_config.get("GEMINI_API_KEY")
    )
    if not gemini_api_key and openrouter_api_key and openrouter_api_key.strip().startswith("AIza"):
        gemini_api_key = openrouter_api_key.strip()

    settings = get_settings()
    prefer_local = settings.local_only or vision_mode not in {"provider", "remote", "external"}
    if prefer_local:
        local_response = analyze_local_image(
            binary=binary,
            file_name=file.filename or "",
            mime_type=mime_type,
            prompt=prompt_text,
            width=width,
            height=height,
        )
        return VisionAnalyzeResponse(
            response=local_response,
            provider="kolibri-local-vision",
            model="kolibri-local-vision-v2",
            duration_ms=round((time.perf_counter() - started_total) * 1000.0, 2),
            mime_type=mime_type,
            width=width,
            height=height,
        )

    openrouter_endpoint = (
        os.getenv("KOLIBRI_OPENROUTER_ENDPOINT")
        or file_config.get("KOLIBRI_OPENROUTER_ENDPOINT")
        or "https://openrouter.ai/api/v1/chat/completions"
    ).strip()
    openai_endpoint = (
        os.getenv("KOLIBRI_OPENAI_ENDPOINT")
        or file_config.get("KOLIBRI_OPENAI_ENDPOINT")
        or "https://api.openai.com/v1/chat/completions"
    ).strip()
    gemini_endpoint = (
        os.getenv("KOLIBRI_GEMINI_ENDPOINT")
        or file_config.get("KOLIBRI_GEMINI_ENDPOINT")
        or "https://generativelanguage.googleapis.com/v1beta"
    ).strip().rstrip("/")
    timeout_raw = (
        os.getenv("KOLIBRI_IMAGE_TIMEOUT")
        or file_config.get("KOLIBRI_IMAGE_TIMEOUT")
        or "120"
    )
    try:
        timeout = float(timeout_raw)
    except ValueError:
        timeout = 120.0

    referer = (
        os.getenv("KOLIBRI_OPENROUTER_SITE_URL")
        or file_config.get("KOLIBRI_OPENROUTER_SITE_URL")
        or "https://kolibriai.ru"
    ).strip()
    app_name = (
        os.getenv("KOLIBRI_OPENROUTER_APP_NAME")
        or file_config.get("KOLIBRI_OPENROUTER_APP_NAME")
        or "Kolibri AI"
    ).strip()

    openrouter_error: HTTPException | None = None
    if openrouter_api_key and not openrouter_api_key.strip().startswith("AIza"):
        model_name = (
            os.getenv("KOLIBRI_OPENROUTER_VISION_MODEL")
            or file_config.get("KOLIBRI_OPENROUTER_VISION_MODEL")
            or "google/gemini-2.5-flash"
        ).strip()
        headers = {
            "Authorization": f"Bearer {openrouter_api_key}",
            "Content-Type": "application/json",
        }
        if referer:
            headers["HTTP-Referer"] = referer
        if app_name:
            headers["X-Title"] = app_name
        payload = {
            "model": model_name,
            "messages": [
                {
                    "role": "user",
                    "content": [
                        {"type": "text", "text": prompt_text},
                        {"type": "image_url", "image_url": {"url": data_uri}},
                    ],
                }
            ],
        }
        started = time.perf_counter()
        try:
            async with httpx.AsyncClient(timeout=timeout) as client:
                response = await client.post(openrouter_endpoint, json=payload, headers=headers)
            response.raise_for_status()
            try:
                payload_json = response.json()
            except json.JSONDecodeError as exc:
                raise HTTPException(status_code=502, detail="OpenRouter vision returned invalid JSON") from exc

            return VisionAnalyzeResponse(
                response=extract_text(payload_json),
                provider="openrouter-vision",
                model=model_name,
                duration_ms=round((time.perf_counter() - started) * 1000.0, 2),
                mime_type=mime_type,
                width=width,
                height=height,
            )
        except httpx.HTTPStatusError as exc:
            detail = exc.response.text[:1000]
            openrouter_error = HTTPException(
                status_code=502,
                detail=f"OpenRouter vision returned {exc.response.status_code}: {detail}",
            )
        except HTTPException as exc:
            openrouter_error = exc

    openai_error: HTTPException | None = None
    if openai_api_key:
        model_name = (
            os.getenv("KOLIBRI_OPENAI_VISION_MODEL")
            or file_config.get("KOLIBRI_OPENAI_VISION_MODEL")
            or "gpt-4.1-mini"
        ).strip()
        payload = {
            "model": model_name,
            "messages": [
                {
                    "role": "user",
                    "content": [
                        {"type": "text", "text": prompt_text},
                        {"type": "image_url", "image_url": {"url": data_uri, "detail": "auto"}},
                    ],
                }
            ],
            "temperature": 0.2,
        }
        started = time.perf_counter()
        try:
            async with httpx.AsyncClient(timeout=timeout) as client:
                response = await client.post(
                    openai_endpoint,
                    json=payload,
                    headers={
                        "Authorization": f"Bearer {openai_api_key}",
                        "Content-Type": "application/json",
                    },
                )
            response.raise_for_status()
            try:
                payload_json = response.json()
            except json.JSONDecodeError as exc:
                raise HTTPException(status_code=502, detail="OpenAI vision returned invalid JSON") from exc

            return VisionAnalyzeResponse(
                response=extract_text(payload_json),
                provider="openai-vision",
                model=model_name,
                duration_ms=round((time.perf_counter() - started) * 1000.0, 2),
                mime_type=mime_type,
                width=width,
                height=height,
            )
        except httpx.HTTPStatusError as exc:
            detail = exc.response.text[:1000]
            openai_error = HTTPException(
                status_code=502,
                detail=f"OpenAI vision returned {exc.response.status_code}: {detail}",
            )
        except HTTPException as exc:
            openai_error = exc

    gemini_error: HTTPException | None = None
    if gemini_api_key:
        model_name = (
            os.getenv("KOLIBRI_GEMINI_VISION_MODEL")
            or file_config.get("KOLIBRI_GEMINI_VISION_MODEL")
            or "gemini-2.5-flash"
        ).strip()
        if "/" in model_name:
            model_name = model_name.split("/", 1)[1]
        url = f"{gemini_endpoint}/models/{model_name}:generateContent?key={gemini_api_key}"
        payload = {
            "contents": [
                {
                    "parts": [
                        {"text": prompt_text},
                        {"inlineData": {"mimeType": mime_type, "data": data_b64}},
                    ]
                }
            ]
        }
        started = time.perf_counter()
        try:
            async with httpx.AsyncClient(timeout=timeout) as client:
                response = await client.post(url, json=payload, headers={"Content-Type": "application/json"})
            response.raise_for_status()
            try:
                payload_json = response.json()
            except json.JSONDecodeError as exc:
                raise HTTPException(status_code=502, detail="Gemini vision returned invalid JSON") from exc

            return VisionAnalyzeResponse(
                response=_extract_gemini_text_result(payload_json),
                provider="google-gemini-vision",
                model=model_name,
                duration_ms=round((time.perf_counter() - started) * 1000.0, 2),
                mime_type=mime_type,
                width=width,
                height=height,
            )
        except httpx.HTTPStatusError as exc:
            detail = exc.response.text[:1000]
            gemini_error = HTTPException(
                status_code=502,
                detail=f"Gemini vision returned {exc.response.status_code}: {detail}",
            )
        except HTTPException as exc:
            gemini_error = exc

    failure_reasons: list[str] = []
    if openrouter_error is not None:
        failure_reasons.append("OpenRouter недоступен")
    if openai_error is not None:
        failure_reasons.append("OpenAI vision недоступен")
    if gemini_error is not None:
        failure_reasons.append("Gemini недоступен в текущем регионе или для текущего изображения")
    if not failure_reasons:
        failure_reasons.append("не настроен внешний vision-провайдер")

    local_response = analyze_local_image(
        binary=binary,
        file_name=file.filename or "",
        mime_type=mime_type,
        prompt=f"{prompt_text}. Учти причину fallback: {', '.join(failure_reasons)}.",
        width=width,
        height=height,
    )
    if failure_reasons:
        local_response = f"{local_response} Внешний vision-провайдер не использован: {', '.join(failure_reasons)}."
    return VisionAnalyzeResponse(
        response=local_response,
        provider="kolibri-local-vision",
        model="kolibri-local-vision-v2",
        duration_ms=round((time.perf_counter() - started_total) * 1000.0, 2),
        mime_type=mime_type,
        width=width,
        height=height,
    )


@router.get("/imagine/files/{filename}")
async def ai_imagine_file(filename: str):
    file_config = _load_backend_file_config()

    if not _SAFE_FILENAME_RE.fullmatch(filename):
        raise HTTPException(status_code=400, detail="Invalid filename")

    output_dir = _resolve_imagine_output_dir(file_config)
    file_path = os.path.join(output_dir, filename)
    if not os.path.isfile(file_path):
        raise HTTPException(status_code=404, detail="Image not found")

    return FileResponse(file_path)


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
            # Не блокируем event loop на длительном обучении.
            result = await asyncio.to_thread(engine.train_and_verify, req.text)
        else:
            result = await asyncio.to_thread(engine.train_text, req.text)
            result["before"] = {}
            result["after"] = engine.graph.get_stats()
            result["sample_patterns"] = {}
            result["formula_generation"] = engine.formula_pool.generation
            result["formula_fitness"] = round(engine.formula_pool.best().fitness, 4)
        # Фиксируем обучение на диск/в БД, чтобы знания переживали перезапуск.
        await asyncio.to_thread(engine.persist_state)
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


@router.get("/models", response_model=LoadedModelsResponse)
async def loaded_models() -> LoadedModelsResponse:
    """Список загруженных KLM-моделей и их состояние."""
    engine = get_engine()
    c = engine._get_model_stats()

    return LoadedModelsResponse(
        primary_model=engine.c_retriever.model_path.name,
        model_available=engine.c_retriever.available,
        c_trainer_available=engine.c_retriever.trainer_bin.exists(),
        model_path=str(engine.c_retriever.model_path),
        model_size_mb=c.get("size_mb", 0.0),
        patterns=c.get("patterns", 0),
        edges=c.get("edges", 0),
        documents=c.get("documents", 0),
        epoch=c.get("epoch", 0),
        formula_generation=engine.formula_pool.generation,
        embedding_vocab_size=engine.embeddings.vocab_size,
        sentence_store_size=engine.sentence_store.size,
    )


@router.get("/stats", response_model=EngineStatsResponse)
async def engine_stats() -> EngineStatsResponse:
    """Статистика AI-движка — числовой граф + формулы + эмбеддинги + C-модель."""
    engine = get_engine()
    g = engine.graph.get_stats()
    c = engine._get_model_stats()
    best = engine.formula_pool.best()
    emb = engine.embeddings.get_stats()

    return EngineStatsResponse(
        model_available=engine.c_retriever.available,
        graph_patterns=g["patterns"],
        graph_edges=g["edges"],
        graph_max_patterns=g.get("max_patterns", 0),
        graph_max_edges=g.get("max_edges", 0),
        graph_max_degree=g.get("max_degree", 0),
        graph_documents=g["documents_trained"],
        graph_tokens=g["tokens_processed"],
        graph_version=g.get("graph_version", 0),
        delta_log_len=g.get("delta_log_len", 0),
        delta_oldest_version=g.get("delta_oldest_version", 0),
        graph_avg_fitness=g["avg_fitness"],
        graph_avg_weight=g["avg_weight"],
        formula_generation=engine.formula_pool.generation,
        formula_fitness=round(best.fitness, 4),
        formula_genome_hex=best.gene.to_hex()[:32],
        gene_digits=len(best.gene.digits),
        formula_layers=FORMULA_LAYERS,
        formula_layers_fast=FORMULA_LAYERS_FAST,
        formula_ops=12,
        pattern_size=KLM_PATTERN_SIZE,
        c_model_patterns=c.get("patterns", 0),
        c_model_edges=c.get("edges", 0),
        c_model_size_mb=c.get("size_mb", 0.0),
        c_model_documents=c.get("documents", 0),
        c_model_epoch=c.get("epoch", 0),
        c_model_avg_fitness=c.get("avg_fitness", 0.0),
        c_model_avg_weight=c.get("avg_weight", 0.0),
        embedding_vocab_size=emb["vocab_size"],
        embedding_trained_pairs=emb["trained_pairs"],
        embedding_epochs=emb["epochs_completed"],
        embedding_dim=emb["dim"],
        embedding_avg_norm=emb["avg_vector_norm"],
        embedding_last_loss=emb["last_loss"],
        active_conversations=len(engine.conversations),
        sentence_store_size=engine.sentence_store.size,
    )


def _quality_response_from_report(report: dict[str, Any]) -> QualityBenchmarkResponse:
    return QualityBenchmarkResponse(
        run_id=str(report.get("run_id", "") or ""),
        trigger=str(report.get("trigger", "manual") or "manual"),
        started_at=float(report.get("started_at", 0.0) or 0.0),
        finished_at=float(report.get("finished_at", 0.0) or 0.0),
        duration_ms=float(report.get("duration_ms", 0.0) or 0.0),
        score=float(report.get("score", 0.0) or 0.0),
        passed=int(report.get("passed", 0) or 0),
        total=int(report.get("total", 0) or 0),
        weighted_passed=float(report.get("weighted_passed", 0.0) or 0.0),
        weighted_total=float(report.get("weighted_total", 0.0) or 0.0),
        pass_rate=float(report.get("pass_rate", 0.0) or 0.0),
        latency_p50_ms=float(report.get("latency_p50_ms", 0.0) or 0.0),
        latency_p95_ms=float(report.get("latency_p95_ms", 0.0) or 0.0),
        confidence_avg=float(report.get("confidence_avg", 0.0) or 0.0),
        confidence_pass_avg=float(report.get("confidence_pass_avg", 0.0) or 0.0),
        confidence_fail_avg=float(report.get("confidence_fail_avg", 0.0) or 0.0),
        placeholder_rate=float(report.get("placeholder_rate", 0.0) or 0.0),
        timeout_rate=float(report.get("timeout_rate", 0.0) or 0.0),
        error_rate=float(report.get("error_rate", 0.0) or 0.0),
        hallucination_proxy_rate=float(report.get("hallucination_proxy_rate", 0.0) or 0.0),
        categories=list(report.get("categories", []) or []),
        methods=list(report.get("methods", []) or []),
        gates=dict(report.get("gates", {}) or {}),
        details=list(report.get("details", []) or []),
    )


@router.get("/quality/benchmark", response_model=QualityBenchmarkResponse)
async def quality_benchmark_latest() -> QualityBenchmarkResponse:
    """Последний авто/ручной quality benchmark на контрольных вопросах."""
    engine = get_engine()
    report = await asyncio.to_thread(engine.get_quality_benchmark_report)
    if not isinstance(report, dict):
        raise HTTPException(status_code=404, detail="No benchmark report yet")
    return _quality_response_from_report(report)


@router.get("/quality/benchmark/history", response_model=QualityBenchmarkHistoryResponse)
async def quality_benchmark_history(
    limit: int = Query(default=30, ge=1, le=200),
) -> QualityBenchmarkHistoryResponse:
    """История quality benchmark (последние N запусков) + тренд по score/latency."""
    engine = get_engine()
    rows = await asyncio.to_thread(engine.get_quality_benchmark_history, int(limit))
    points: list[QualityBenchmarkHistoryPoint] = []
    for row in rows:
        gates = dict(row.get("gates", {}) or {})
        points.append(
            QualityBenchmarkHistoryPoint(
                run_id=str(row.get("run_id", "") or ""),
                trigger=str(row.get("trigger", "manual") or "manual"),
                started_at=float(row.get("started_at", 0.0) or 0.0),
                finished_at=float(row.get("finished_at", 0.0) or 0.0),
                duration_ms=float(row.get("duration_ms", 0.0) or 0.0),
                score=float(row.get("score", 0.0) or 0.0),
                passed=int(row.get("passed", 0) or 0),
                total=int(row.get("total", 0) or 0),
                weighted_passed=float(row.get("weighted_passed", 0.0) or 0.0),
                weighted_total=float(row.get("weighted_total", 0.0) or 0.0),
                pass_rate=float(row.get("pass_rate", 0.0) or 0.0),
                latency_p50_ms=float(row.get("latency_p50_ms", 0.0) or 0.0),
                latency_p95_ms=float(row.get("latency_p95_ms", 0.0) or 0.0),
                placeholder_rate=float(row.get("placeholder_rate", 0.0) or 0.0),
                timeout_rate=float(row.get("timeout_rate", 0.0) or 0.0),
                error_rate=float(row.get("error_rate", 0.0) or 0.0),
                hallucination_proxy_rate=float(row.get("hallucination_proxy_rate", 0.0) or 0.0),
                gates_overall_pass=bool(gates.get("overall_pass", False)),
            )
        )

    score_values = [float(p.score) for p in points]
    p95_values = [float(p.latency_p95_ms) for p in points if float(p.latency_p95_ms) > 0.0]
    gate_failures = sum(1 for p in points if not p.gates_overall_pass)
    category_rates: dict[str, list[float]] = {}
    category_weighted_rates: dict[str, list[float]] = {}
    for row in rows:
        for category_row in list(row.get("categories", []) or []):
            if not isinstance(category_row, dict):
                continue
            name = str(category_row.get("category", "") or "").strip()
            if not name:
                continue
            category_rates.setdefault(name, []).append(float(category_row.get("pass_rate", 0.0) or 0.0))
            category_weighted_rates.setdefault(name, []).append(
                float(category_row.get("weighted_pass_rate", 0.0) or 0.0)
            )

    trend: dict[str, Any] = {
        "score_avg": round(sum(score_values) / max(1, len(score_values)), 4) if score_values else 0.0,
        "latency_p95_ms_avg": round(sum(p95_values) / max(1, len(p95_values)), 1) if p95_values else 0.0,
        "gate_failures": int(gate_failures),
        "category_pass_rate_avg": {
            key: round(sum(values) / max(1, len(values)), 4)
            for key, values in sorted(category_rates.items())
        },
        "category_weighted_pass_rate_avg": {
            key: round(sum(values) / max(1, len(values)), 4)
            for key, values in sorted(category_weighted_rates.items())
        },
    }
    if len(points) >= 2:
        newest = points[0]
        oldest = points[-1]
        trend["score_delta"] = round(float(newest.score) - float(oldest.score), 4)
        trend["latency_p95_ms_delta"] = round(float(newest.latency_p95_ms) - float(oldest.latency_p95_ms), 1)
    else:
        trend["score_delta"] = 0.0
        trend["latency_p95_ms_delta"] = 0.0

    return QualityBenchmarkHistoryResponse(
        limit=int(limit),
        count=len(points),
        items=points,
        trend=trend,
    )


@router.post("/quality/benchmark/run", response_model=QualityBenchmarkResponse)
async def quality_benchmark_run() -> QualityBenchmarkResponse:
    """Принудительно запустить quality benchmark (контрольные вопросы)."""
    engine = get_engine()
    report = await asyncio.to_thread(engine.run_quality_benchmark, "manual")
    return _quality_response_from_report(report)


@router.get("/quality/uniqueness", response_model=UniquenessProofResponse)
async def uniqueness_proof_latest() -> UniquenessProofResponse:
    """Последний отчёт proof-suite уникальных свойств Kolibri."""
    engine = get_engine()
    report = await asyncio.to_thread(engine.get_uniqueness_report)
    if not isinstance(report, dict):
        raise HTTPException(status_code=404, detail="No uniqueness proof report yet")
    return UniquenessProofResponse(
        run_id=str(report.get("run_id", "") or ""),
        trigger=str(report.get("trigger", "manual") or "manual"),
        started_at=float(report.get("started_at", 0.0) or 0.0),
        finished_at=float(report.get("finished_at", 0.0) or 0.0),
        duration_ms=float(report.get("duration_ms", 0.0) or 0.0),
        score=float(report.get("score", 0.0) or 0.0),
        passed=int(report.get("passed", 0) or 0),
        total=int(report.get("total", 0) or 0),
        fingerprint=str(report.get("fingerprint", "") or ""),
        claims=[str(x) for x in (report.get("claims", []) or [])],
        details=list(report.get("details", []) or []),
    )


@router.post("/quality/uniqueness/run", response_model=UniquenessProofResponse)
async def uniqueness_proof_run() -> UniquenessProofResponse:
    """Принудительно запустить proof-suite уникальности Kolibri."""
    engine = get_engine()
    report = await asyncio.to_thread(engine.run_uniqueness_proof, "manual")
    return UniquenessProofResponse(
        run_id=str(report.get("run_id", "") or ""),
        trigger=str(report.get("trigger", "manual") or "manual"),
        started_at=float(report.get("started_at", 0.0) or 0.0),
        finished_at=float(report.get("finished_at", 0.0) or 0.0),
        duration_ms=float(report.get("duration_ms", 0.0) or 0.0),
        score=float(report.get("score", 0.0) or 0.0),
        passed=int(report.get("passed", 0) or 0),
        total=int(report.get("total", 0) or 0),
        fingerprint=str(report.get("fingerprint", "") or ""),
        claims=[str(x) for x in (report.get("claims", []) or [])],
        details=list(report.get("details", []) or []),
    )


@router.post("/reload", response_model=ReloadResponse)
async def reload_corpus() -> ReloadResponse:
    """Перезагрузить корпус и пересобрать числовой граф."""
    engine = get_engine()
    info = engine.reload_corpus()
    return ReloadResponse(**info)


@router.post("/embeddings/similar", response_model=EmbeddingSimilarityResponse)
async def embedding_similar(req: EmbeddingSimilarityRequest) -> EmbeddingSimilarityResponse:
    """
    Семантический поиск похожих слов через обученные эмбеддинги.
    
    В отличие от DJB2 pattern_similarity (случайное совпадение цифр),
    эмбеддинги дают НАСТОЯЩЕЕ семантическое сходство:
    "кот" ≈ "кошка" ≈ "котёнок" (cosine > 0.5)
    """
    engine = get_engine()
    word = req.word.lower().strip()
    results = engine.graph.find_similar_semantic(word, limit=req.top_k)
    
    method = results[0][2] if results else 'pattern'
    
    return EmbeddingSimilarityResponse(
        word=word,
        method=method,
        similar=[
            {"word": w, "similarity": round(s, 4), "method": m}
            for w, s, m in results
        ],
        vocab_size=engine.embeddings.vocab_size,
        trained_pairs=engine.embeddings.trained_pairs,
    )


@router.post("/embeddings/compare", response_model=EmbeddingCompareResponse)
async def embedding_compare(req: EmbeddingCompareRequest) -> EmbeddingCompareResponse:
    """
    Сравнить сходство двух слов: эмбеддинги vs DJB2 паттерны.
    
    Показывает разницу: эмбеддинги дают высокий скор
    для семантически близких слов, DJB2 — случайный.
    """
    engine = get_engine()
    w1 = req.word1.lower().strip()
    w2 = req.word2.lower().strip()
    
    h1 = djb2_hash(w1)
    h2 = djb2_hash(w2)
    
    # DJB2 pattern similarity (baseline)
    pat_sim = pattern_similarity(word_to_pattern(w1), word_to_pattern(w2))
    
    # Embedding cosine similarity (learned)
    emb_sim = engine.embeddings.cosine_similarity(h1, h2)
    
    method = 'embedding' if engine.embeddings.has(h1) and engine.embeddings.has(h2) else 'pattern'
    
    return EmbeddingCompareResponse(
        word1=w1,
        word2=w2,
        embedding_similarity=round(emb_sim, 4),
        pattern_similarity=round(pat_sim, 4),
        method=method,
    )


@router.post("/embeddings/train", response_model=EmbeddingTrainResponse)
async def train_embeddings() -> EmbeddingTrainResponse:
    """
    Переобучить эмбеддинги на текущем графе знаний.
    
    Запускает полный цикл Word2Vec-style обучения:
    5 эпох, негативный семплинг, learning rate decay.
    """
    engine = get_engine()
    result = engine.embeddings.train_on_graph(
        edges=engine.graph.edges,
        hash_to_word=engine.graph._hash_to_word,
        all_hashes=set(engine.graph.patterns.keys()),
        epochs=5,
        lr=0.025,
        neg_samples=5,
    )
    # Сохранить после обучения
    engine._save_embeddings()
    
    return EmbeddingTrainResponse(
        status="ok",
        loss=result["loss"],
        pairs=result["pairs"],
        epochs=result["epochs"],
        vocab_size=result["vocab_size"],
        duration_ms=result.get("duration_ms", 0.0),
    )


@router.get("/conversations", response_model=ConversationListResponse)
async def list_conversations(
    request: Request,
    client_id: Optional[str] = Query(default=None, max_length=120),
    limit: int = Query(default=100, ge=1, le=500),
) -> ConversationListResponse:
    actor = resolve_request_actor(request, client_id)
    account_id = str(actor.get("account_key", "global") or "global")
    items = [
        ConversationSummary(**item)
        for item in _DB.list_conversation_sessions(account_id, limit=limit)
    ]
    return ConversationListResponse(account_id=account_id, items=items)


@router.post("/conversations", response_model=ConversationSummary)
async def upsert_conversation(
    req: ConversationUpsertRequest,
    request: Request,
    client_id: Optional[str] = Query(default=None, max_length=120),
) -> ConversationSummary:
    if not req.conversation_id:
        raise HTTPException(status_code=422, detail="conversation_id is required")
    actor = resolve_request_actor(request, client_id)
    account_id = str(actor.get("account_key", "global") or "global")
    _upsert_conversation_session(
        account_id,
        req.conversation_id,
        title=req.title,
        pinned=req.pinned,
    )
    rows = _DB.list_conversation_sessions(account_id, limit=500)
    row = next((item for item in rows if item.get("conversation_id") == req.conversation_id), None)
    if row is None:
        row = {
            "conversation_id": req.conversation_id,
            "title": req.title or "Новый чат",
            "pinned": bool(req.pinned),
            "created_at": time.time(),
            "updated_at": time.time(),
        }
    return ConversationSummary(**row)


@router.get("/conversations/{conv_id}/turns", response_model=ConversationTurnsResponse)
async def list_conversation_turns(
    conv_id: str,
    request: Request,
    client_id: Optional[str] = Query(default=None, max_length=120),
    limit: int = Query(default=120, ge=1, le=400),
) -> ConversationTurnsResponse:
    engine = get_engine()
    actor = resolve_request_actor(request, client_id)
    account_id = str(actor.get("account_key", "global") or "global")
    resolved_conversation_id, items = engine.get_conversation_turns(
        conv_id,
        client_id=account_id,
        limit=limit,
    )
    return ConversationTurnsResponse(
        account_id=account_id,
        conversation_id=resolved_conversation_id or conv_id,
        items=[ConversationTurnItem(**item) for item in items],
    )


@router.patch("/conversations/{conv_id}", response_model=ConversationSummary)
async def patch_conversation(
    conv_id: str,
    req: ConversationUpsertRequest,
    request: Request,
    client_id: Optional[str] = Query(default=None, max_length=120),
) -> ConversationSummary:
    actor = resolve_request_actor(request, client_id)
    account_id = str(actor.get("account_key", "global") or "global")
    _upsert_conversation_session(
        account_id,
        conv_id,
        title=req.title,
        pinned=req.pinned,
    )
    rows = _DB.list_conversation_sessions(account_id, limit=500)
    row = next((item for item in rows if item.get("conversation_id") == conv_id), None)
    if row is None:
        row = {
            "conversation_id": conv_id,
            "title": req.title or "Новый чат",
            "pinned": bool(req.pinned),
            "created_at": time.time(),
            "updated_at": time.time(),
        }
    return ConversationSummary(**row)


@router.delete("/conversations/{conv_id}")
async def delete_conversation(
    conv_id: str,
    request: Request,
    client_id: Optional[str] = Query(default=None, max_length=120),
) -> dict:
    engine = get_engine()
    actor = resolve_request_actor(request, client_id)
    account_id = str(actor.get("account_key", client_id or "global") or "global")
    _DB.delete_conversation_session(account_id, conv_id)
    if engine.delete_conversation(conv_id, client_id=account_id):
        return {"status": "deleted", "conversation_id": conv_id}


# ============================================================================
# Unified Knowledge Hub API
# ============================================================================

class KnowledgeQueryRequest(BaseModel):
    query: str = Field(min_length=1, max_length=500)
    top_k: int = Field(default=5, ge=1, le=20)


class KnowledgeSourceItem(BaseModel):
    name: str
    score: float
    content: str
    metadata: dict = Field(default_factory=dict)


class KnowledgeQueryResponse(BaseModel):
    query: str
    sources: list[KnowledgeSourceItem]
    best_answer: str
    confidence: float
    total_sources: int
    fusion_method: str
    duration_ms: float


class KnowledgeAnalyticsResponse(BaseModel):
    knowledge_graph: dict
    formula_pool: dict
    embeddings: dict


@router.post("/knowledge/query", response_model=KnowledgeQueryResponse)
async def knowledge_query(req: KnowledgeQueryRequest):
    """
    Запрос к Unified Knowledge Hub: «что мы знаем о X?»

    Объединяет результаты из:
    - .klm паттерны
    - Граф знаний
    - Формулы
    - Embeddings
    - Sentence Store
    """
    from .unified_knowledge_hub import get_unified_knowledge_hub

    hub = get_unified_knowledge_hub()
    result = hub.query(req.query, top_k=req.top_k)

    return KnowledgeQueryResponse(
        query=result.query,
        sources=[
            KnowledgeSourceItem(
                name=s.name,
                score=s.score,
                content=s.content,
                metadata=s.metadata,
            )
            for s in result.sources
        ],
        best_answer=result.best_answer,
        confidence=result.confidence,
        total_sources=result.total_sources,
        fusion_method=result.fusion_method,
        duration_ms=result.duration_ms,
    )


@router.get("/knowledge/analytics", response_model=KnowledgeAnalyticsResponse)
async def knowledge_analytics():
    """
    Аналитика по всем подсистемам знаний.

    Возвращает статистику:
    - Knowledge Graph (паттерны, рёбра, документы)
    - Formula Pool (размер, поколение, fitness)
    - Embeddings (словарь, пары, эпохи)
    - Sentence Store (размер, память)
    """
    from .unified_knowledge_hub import get_unified_knowledge_hub

    hub = get_unified_knowledge_hub()
    return hub.get_analytics()
