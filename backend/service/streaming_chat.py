"""
streaming_chat.py — Streaming SSE endpoint для Kolibri

Фаза A3: Потоковая выдача ответов
- Server-Sent Events (SSE) для real-time streaming
- Token-by-token генерация
- Пользователь видит ответ через 200ms
"""
from __future__ import annotations

import asyncio
import json
import logging
import time
from typing import AsyncGenerator

from fastapi import APIRouter, Request
from fastapi.responses import StreamingResponse

from .ai_engine import get_engine
from .rag_pipeline import RAGPipeline

log = logging.getLogger("kolibri.streaming")

router = APIRouter()

# Глобальный RAG pipeline
_rag_pipeline: RAGPipeline | None = None


def get_rag_pipeline() -> RAGPipeline:
    global _rag_pipeline
    if _rag_pipeline is None:
        _rag_pipeline = RAGPipeline()
    return _rag_pipeline


async def stream_response(
    message: str,
    conversation_id: str | None = None,
    persona: str = "assistant",
    memory_enabled: bool = True,
) -> AsyncGenerator[str, None]:
    """Генерировать SSE events с ответом."""
    t0 = time.time()

    # Отправляем статус "thinking"
    yield f"event: status\ndata: {json.dumps({'status': 'thinking', 'message': 'Думаю...'})}\n\n"
    await asyncio.sleep(0.1)

    try:
        engine = get_engine()
        rag = get_rag_pipeline()

        # Сначала ищем в RAG
        yield f"event: status\ndata: {json.dumps({'status': 'searching', 'message': 'Ищу знания...'})}\n\n"
        await asyncio.sleep(0.05)

        rag_result = rag.query(message, top_k=5)

        if rag_result["response"]:
            # Есть ответ из RAG — стримим его
            yield f"event: status\ndata: {json.dumps({'status': 'generating', 'message': 'Формирую ответ...'})}\n\n"
            await asyncio.sleep(0.05)

            response_text = rag_result["response"]

            # Стримим токен за токеном (по словам)
            words = response_text.split()
            accumulated = ""
            for i, word in enumerate(words):
                accumulated += word + " "
                yield f"event: token\ndata: {json.dumps({'token': word + ' ', 'progress': (i + 1) / len(words)})}\n\n"
                await asyncio.sleep(0.02)  # Имитация генерации

            # Финальный event
            duration_ms = (time.time() - t0) * 1000
            yield f"event: done\ndata: {json.dumps({
                'response': response_text,
                'confidence': rag_result['confidence'],
                'method': rag_result['method'],
                'sources': rag_result['sources'],
                'duration_ms': round(duration_ms, 1),
                'conversation_id': conversation_id,
            })}\n\n"
        else:
            # Fallback на основной engine
            yield f"event: status\ndata: {json.dumps({'status': 'generating', 'message': 'Генерирую ответ...'})}\n\n"
            await asyncio.sleep(0.05)

            # Получаем ответ из основного engine
            result = engine.chat(
                message=message,
                conversation_id=conversation_id,
                persona=persona,  # type: ignore
                memory_enabled=memory_enabled,
            )

            response_text = result.get("response", "")
            words = response_text.split()
            for i, word in enumerate(words):
                yield f"event: token\ndata: {json.dumps({'token': word + ' ', 'progress': (i + 1) / len(words)})}\n\n"
                await asyncio.sleep(0.02)

            duration_ms = (time.time() - t0) * 1000
            yield f"event: done\ndata: {json.dumps({
                'response': response_text,
                'confidence': result.get('confidence', 0.0),
                'method': result.get('method', 'unknown'),
                'sources': result.get('sources', []),
                'duration_ms': round(duration_ms, 1),
                'conversation_id': result.get('conversation_id', conversation_id),
            })}\n\n"

    except Exception as e:
        log.error("Streaming error: %s", e)
        yield f"event: error\ndata: {json.dumps({'error': str(e)})}\n\n"


@router.post("/api/v1/ai/chat/stream")
async def chat_stream(request: Request):
    """Streaming chat endpoint с SSE."""
    body = await request.json()

    message = body.get("message", "")
    conversation_id = body.get("conversation_id")
    persona = body.get("persona", "assistant")
    memory_enabled = body.get("memory_enabled", True)

    return StreamingResponse(
        stream_response(message, conversation_id, persona, memory_enabled),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "Connection": "keep-alive",
            "X-Accel-Buffering": "no",
        },
    )


@router.get("/api/v1/rag/stats")
async def rag_stats():
    """Статистика RAG индекса."""
    rag = get_rag_pipeline()
    return {
        "total_chunks": len(rag.index.chunks),
        "categories": list(rag.index.category_index.keys()),
        "chunk_counts": {
            cat: len(indices)
            for cat, indices in rag.index.category_index.items()
        },
    }


@router.post("/api/v1/rag/ingest")
async def rag_ingest(request: Request):
    """Загрузить темы из Wikipedia."""
    body = await request.json()
    topics = body.get("topics", [])
    language = body.get("language", "ru")

    if not topics:
        return {"error": "No topics provided"}

    rag = get_rag_pipeline()
    total = rag.ingest_topics(topics, language)

    return {
        "ingested": total,
        "total_chunks": len(rag.index.chunks),
        "topics": topics,
    }
