"""Local GPU knowledge base service (stub)."""
from __future__ import annotations

import sqlite3
from array import array
from pathlib import Path
from typing import List

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field

DB_PATH = Path("build/knowledge/kolibri.db")

router = APIRouter(prefix="/api/gpu", tags=["gpu-store"])


def get_conn() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


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
async def store(req: StoreRequest, conn: sqlite3.Connection = Depends(get_conn)):
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
    return StoreResponse(doc_id=doc_id, embedding_id=embedding_id)


@router.post("/search", response_model=List[SearchHit])
async def search(req: SearchRequest, conn: sqlite3.Connection = Depends(get_conn)):
    cur = conn.cursor()
    cur.execute("SELECT d.doc_id, d.path FROM documents d LIMIT ?", (req.limit,))
    hits = [SearchHit(doc_id=row[0], score=0.0, path=row[1]) for row in cur.fetchall()]
    return hits
