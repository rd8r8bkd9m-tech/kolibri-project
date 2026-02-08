"""Local GPU knowledge base service — cosine-similarity vector search."""
from __future__ import annotations

import math
import sqlite3
import struct
from array import array
from pathlib import Path
from typing import List

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field

DB_PATH = Path(".kolibri/knowledge/kolibri.db")

router = APIRouter(prefix="/api/gpu", tags=["gpu-store"])


def get_conn() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


# ─── Вспомогательные функции для векторной математики ───

def _blob_to_floats(blob: bytes, dims: int) -> list[float]:
    """Десериализация BLOB → list[float] (IEEE-754 little-endian)."""
    return list(struct.unpack(f"<{dims}f", blob))


def _cosine_similarity(a: list[float], b: list[float]) -> float:
    """Cosine similarity двух векторов одинаковой длины."""
    if len(a) != len(b) or len(a) == 0:
        return 0.0
    dot = sum(x * y for x, y in zip(a, b))
    norm_a = math.sqrt(sum(x * x for x in a))
    norm_b = math.sqrt(sum(x * x for x in b))
    if norm_a < 1e-12 or norm_b < 1e-12:
        return 0.0
    return dot / (norm_a * norm_b)


# ─── Модели ───

class StoreRequest(BaseModel):
    path: str
    sha256: str
    cls: str = Field(alias="class")
    entropy: float
    bytes: int
    content: str
    embedding: List[float]


class StoreResponse(BaseModel):
    doc_id: int
    embedding_id: int


class SearchRequest(BaseModel):
    embedding: List[float]
    limit: int = 5


class SearchHit(BaseModel):
    doc_id: int
    score: float
    path: str


@router.post("/store", response_model=StoreResponse)
async def store(req: StoreRequest, conn: sqlite3.Connection = Depends(get_conn)) -> StoreResponse:
    cur = conn.cursor()
    cur.execute(
        "INSERT OR IGNORE INTO documents(path, sha256, class, entropy, bytes) VALUES (?, ?, ?, ?, ?)",
        (req.path, req.sha256, req.cls, req.entropy, req.bytes),
    )
    cur.execute("SELECT doc_id FROM documents WHERE sha256=?", (req.sha256,))
    row = cur.fetchone()
    if not row:
        raise HTTPException(status_code=500, detail="failed to insert document")
    doc_id = row[0]
    blob = sqlite3.Binary(array("f", req.embedding).tobytes())
    cur.execute(
        "INSERT INTO embeddings(doc_id, vector, dims) VALUES (?, ?, ?)",
        (doc_id, blob, len(req.embedding)),
    )
    embedding_id = cur.lastrowid
    conn.commit()
    return StoreResponse(doc_id=doc_id, embedding_id=embedding_id or 0)


@router.post("/search", response_model=List[SearchHit])
async def search(req: SearchRequest, conn: sqlite3.Connection = Depends(get_conn)) -> list[SearchHit]:
    """
    Поиск ближайших документов по cosine-similarity.

    Полный перебор всех эмбеддингов (brute-force) — достаточен
    для масштабов до ~100k документов. Для бо́льших объёмов
    следует перейти на FAISS/Annoy/HNSW-индекс.
    """
    query_vec = req.embedding
    if not query_vec:
        return []

    cur = conn.cursor()
    cur.execute(
        "SELECT e.doc_id, e.vector, e.dims, d.path "
        "FROM embeddings e JOIN documents d ON e.doc_id = d.doc_id"
    )

    scored: list[tuple[float, int, str]] = []
    query_dims = len(query_vec)

    for row in cur.fetchall():
        doc_id: int = row[0]
        blob: bytes = row[1]
        dims: int = row[2]
        path: str = row[3]

        # Пропускаем несовпадающие размерности
        if dims != query_dims:
            continue

        doc_vec = _blob_to_floats(blob, dims)
        sim = _cosine_similarity(query_vec, doc_vec)
        scored.append((sim, doc_id, path))

    # Сортируем по убыванию score, берём top-K
    scored.sort(key=lambda t: t[0], reverse=True)
    top = scored[: req.limit]

    return [
        SearchHit(doc_id=doc_id, score=round(score, 6), path=path)
        for score, doc_id, path in top
    ]
