"""
Production health probes — liveness / readiness / detailed diagnostics.

Эндпоинты:
  GET /api/v1/health/live   — Kubernetes liveness probe (процесс жив)
  GET /api/v1/health/ready  — Kubernetes readiness probe (готов к трафику)
  GET /api/v1/health/detail — Подробная диагностика всех подсистем
"""
from __future__ import annotations

import os
import time
from dataclasses import dataclass, field
from typing import Any

import psutil
from fastapi import APIRouter, status
from fastapi.responses import JSONResponse

router = APIRouter(prefix="/api/v1/health", tags=["health"])

_START_TIME = time.time()


# ------------------------------------------------------------------
# Вспомогательные проверки
# ------------------------------------------------------------------

def _check_engine() -> dict[str, Any]:
    """Проверяет состояние AI-движка."""
    try:
        from .ai_engine import get_engine
        engine = get_engine()
        graph = engine.graph
        edge_count = sum(
            len(targets) if isinstance(targets, dict) else 1
            for targets in graph.edges.values()
        )
        emb = getattr(engine, "_embeddings", None)
        cog = getattr(engine, "_cognition", None)
        return {
            "status": "ok",
            "patterns": len(graph.patterns),
            "edges": edge_count,
            "documents_loaded": getattr(engine, "_corpus_loaded", False),
            "embeddings_ready": emb is not None
                and len(getattr(emb, "_vectors", getattr(emb, "vectors", {}))) > 0,
            "causal_index_ready": cog is not None
                and getattr(cog, "causal_index", None) is not None,
        }
    except Exception as exc:
        return {"status": "error", "error": str(exc)}


def _check_persistence() -> dict[str, Any]:
    """Проверяет SQLite-хранилище."""
    try:
        from .persistence import get_db
        db = get_db()
        if db is None:
            return {"status": "disabled"}
        st = db.stats()
        return {"status": "ok", **st}
    except Exception as exc:
        return {"status": "error", "error": str(exc)}


def _check_memory() -> dict[str, Any]:
    """Использование памяти текущего процесса."""
    proc = psutil.Process(os.getpid())
    mem = proc.memory_info()
    return {
        "rss_mb": round(mem.rss / 1024 / 1024, 1),
        "vms_mb": round(mem.vms / 1024 / 1024, 1),
        "percent": round(proc.memory_percent(), 2),
    }


def _check_disk() -> dict[str, Any]:
    """Место на диске рабочего каталога."""
    usage = psutil.disk_usage("/")
    return {
        "total_gb": round(usage.total / 1024**3, 1),
        "free_gb": round(usage.free / 1024**3, 1),
        "percent_used": usage.percent,
    }


def _check_corpus() -> dict[str, Any]:
    """Статистика загруженного корпуса."""
    corpus_dir = os.path.join(os.path.dirname(__file__), "..", "..", "data", "corpus")
    corpus_dir = os.path.normpath(corpus_dir)
    if not os.path.isdir(corpus_dir):
        return {"status": "missing", "files": 0, "size_kb": 0}
    files = [f for f in os.listdir(corpus_dir) if f.endswith(".txt")]
    total = sum(
        os.path.getsize(os.path.join(corpus_dir, f)) for f in files
    )
    return {
        "status": "ok",
        "files": len(files),
        "size_kb": round(total / 1024, 1),
    }


# ------------------------------------------------------------------
# Endpoints
# ------------------------------------------------------------------

@router.get("/live")
async def liveness():
    """Liveness probe — процесс жив и HTTP-сервер отвечает."""
    return JSONResponse(
        {"status": "alive", "uptime_s": round(time.time() - _START_TIME, 1)},
        status_code=status.HTTP_200_OK,
    )


@router.get("/ready")
async def readiness():
    """Readiness probe — движок инициализирован и готов обрабатывать запросы."""
    engine_info = _check_engine()
    ready = engine_info.get("status") == "ok" and engine_info.get("patterns", 0) > 0

    code = status.HTTP_200_OK if ready else status.HTTP_503_SERVICE_UNAVAILABLE
    return JSONResponse(
        {
            "ready": ready,
            "engine": engine_info,
            "uptime_s": round(time.time() - _START_TIME, 1),
        },
        status_code=code,
    )


@router.get("/detail")
async def detail():
    """Подробная диагностика всех подсистем."""
    engine = _check_engine()
    persistence = _check_persistence()
    memory = _check_memory()
    disk = _check_disk()
    corpus = _check_corpus()

    overall = "ok"
    if engine.get("status") != "ok":
        overall = "degraded"

    return JSONResponse(
        {
            "status": overall,
            "uptime_s": round(time.time() - _START_TIME, 1),
            "version": "0.2.0",
            "subsystems": {
                "engine": engine,
                "persistence": persistence,
                "corpus": corpus,
                "memory": memory,
                "disk": disk,
            },
        },
        status_code=status.HTTP_200_OK,
    )
