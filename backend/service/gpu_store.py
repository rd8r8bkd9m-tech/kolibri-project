"""Local GPU knowledge base service — cosine-similarity vector search."""
from __future__ import annotations

import base64
import datetime
import gzip
import hashlib
import json
import math
import os
import re
import sqlite3
import struct
from array import array
from pathlib import Path
from typing import List

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field

DB_PATH = Path(".kolibri/knowledge/kolibri.db")
DEFAULT_DIMS = 64
PORTABLE_MAGIC = "KOLIBRI_GPU_STORE"
PROJECT_ROOT = Path(os.getenv("KOLIBRI_PROJECT_ROOT", ".")).resolve()
TEXT_EXTENSIONS = {
    ".c", ".cc", ".cpp", ".cs", ".css", ".csv", ".go", ".h", ".hpp", ".html",
    ".ini", ".java", ".js", ".json", ".jsx", ".kt", ".log", ".md", ".mjs",
    ".py", ".rs", ".sh", ".sql", ".swift", ".toml", ".ts", ".tsx", ".txt",
    ".xml", ".yaml", ".yml",
}
SKIP_DIRS = {
    ".git", ".hg", ".svn", "__pycache__", ".pytest_cache", ".mypy_cache",
    "node_modules", "dist", "build", ".venv", ".venv312", "venv",
}

router = APIRouter(prefix="/api/gpu", tags=["gpu-store"])


def _ensure_schema(conn: sqlite3.Connection) -> None:
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS documents (
            doc_id INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT NOT NULL,
            sha256 TEXT NOT NULL UNIQUE,
            class TEXT NOT NULL,
            entropy REAL NOT NULL,
            bytes INTEGER NOT NULL,
            content TEXT NOT NULL DEFAULT ''
        )
        """
    )
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS embeddings (
            embedding_id INTEGER PRIMARY KEY AUTOINCREMENT,
            doc_id INTEGER NOT NULL,
            vector BLOB NOT NULL,
            dims INTEGER NOT NULL,
            FOREIGN KEY(doc_id) REFERENCES documents(doc_id)
        )
        """
    )
    cur = conn.execute("PRAGMA table_info(documents)")
    columns = {str(row["name"]) for row in cur.fetchall()}
    if "content" not in columns:
        try:
            conn.execute("ALTER TABLE documents ADD COLUMN content TEXT NOT NULL DEFAULT ''")
        except sqlite3.OperationalError as exc:
            if "duplicate column name" not in str(exc).lower():
                raise
    conn.commit()


def get_conn() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(DB_PATH, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    _ensure_schema(conn)
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


def _entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = [0] * 256
    for byte in data:
        counts[byte] += 1
    total = len(data)
    return -sum((count / total) * math.log2(count / total) for count in counts if count)


def _text_embedding(text: str, dims: int = DEFAULT_DIMS) -> list[float]:
    dims = max(8, min(512, int(dims or DEFAULT_DIMS)))
    vector = [0.0] * dims
    tokens = re.findall(r"[\w\-]+", text.lower(), flags=re.UNICODE)
    if not tokens:
        tokens = [text.lower()] if text else ["empty"]

    for token in tokens:
        digest = hashlib.blake2b(token.encode("utf-8", errors="ignore"), digest_size=16).digest()
        index = int.from_bytes(digest[:4], "little") % dims
        sign = 1.0 if digest[4] & 1 else -1.0
        weight = 1.0 + min(len(token), 24) / 24.0
        vector[index] += sign * weight

        # Добавляем второй индекс, чтобы короткие тексты не схлопывались в один слот.
        second = int.from_bytes(digest[5:9], "little") % dims
        vector[second] += sign * weight * 0.5

    norm = math.sqrt(sum(value * value for value in vector))
    if norm > 1e-12:
        vector = [value / norm for value in vector]
    return vector


def _query_terms(text: str) -> list[str]:
    return [term for term in re.findall(r"[\w\-]+", text.lower(), flags=re.UNICODE) if len(term) >= 2]


def _lexical_score(query_text: str | None, path: str, content: str) -> float:
    if not query_text:
        return 0.0
    terms = list(dict.fromkeys(_query_terms(query_text)))
    if not terms:
        return 0.0
    haystack = f"{path}\n{content}".lower()
    path_lower = path.lower()
    matches = sum(1 for term in terms if term in haystack)
    path_matches = sum(1 for term in terms if term in path_lower)
    if matches == 0:
        return 0.0
    return min(1.0, matches / len(terms)) + min(0.35, path_matches * 0.08)


def _store_document(
    conn: sqlite3.Connection,
    *,
    path: str,
    sha256: str,
    cls: str,
    entropy: float,
    size_bytes: int,
    content: str,
    embedding: list[float],
    replace_embeddings: bool,
) -> tuple[int, int]:
    cur = conn.cursor()
    cur.execute(
        """
        INSERT INTO documents(path, sha256, class, entropy, bytes, content)
        VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(sha256) DO UPDATE SET
            path=excluded.path,
            class=excluded.class,
            entropy=excluded.entropy,
            bytes=excluded.bytes,
            content=excluded.content
        """,
        (path, sha256, cls, entropy, size_bytes, content),
    )
    cur.execute("SELECT doc_id FROM documents WHERE sha256=?", (sha256,))
    row = cur.fetchone()
    if not row:
        raise HTTPException(status_code=500, detail="failed to insert document")

    doc_id = int(row[0])
    if replace_embeddings:
        cur.execute("DELETE FROM embeddings WHERE doc_id=?", (doc_id,))

    blob = sqlite3.Binary(array("f", embedding).tobytes())
    cur.execute(
        "INSERT INTO embeddings(doc_id, vector, dims) VALUES (?, ?, ?)",
        (doc_id, blob, len(embedding)),
    )
    embedding_id = int(cur.lastrowid or 0)
    conn.commit()
    return doc_id, embedding_id


def _store_document_blob(
    conn: sqlite3.Connection,
    *,
    path: str,
    sha256: str,
    cls: str,
    entropy: float,
    size_bytes: int,
    content: str,
    embedding_blob: bytes,
    dims: int,
    replace_embeddings: bool,
) -> tuple[int, int]:
    cur = conn.cursor()
    cur.execute(
        """
        INSERT INTO documents(path, sha256, class, entropy, bytes, content)
        VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(sha256) DO UPDATE SET
            path=excluded.path,
            class=excluded.class,
            entropy=excluded.entropy,
            bytes=excluded.bytes,
            content=excluded.content
        """,
        (path, sha256, cls, entropy, size_bytes, content),
    )
    cur.execute("SELECT doc_id FROM documents WHERE sha256=?", (sha256,))
    row = cur.fetchone()
    if not row:
        raise HTTPException(status_code=500, detail="failed to insert document")

    doc_id = int(row[0])
    if replace_embeddings:
        cur.execute("DELETE FROM embeddings WHERE doc_id=?", (doc_id,))

    cur.execute(
        "INSERT INTO embeddings(doc_id, vector, dims) VALUES (?, ?, ?)",
        (doc_id, sqlite3.Binary(embedding_blob), dims),
    )
    embedding_id = int(cur.lastrowid or 0)
    conn.commit()
    return doc_id, embedding_id


def _search_embedding(
    conn: sqlite3.Connection,
    query_vec: list[float],
    *,
    limit: int = 5,
    min_score: float | None = None,
    query_text: str | None = None,
) -> list[dict]:
    if not query_vec:
        return []

    cur = conn.cursor()
    safe_limit = max(1, min(100, int(limit or 5)))
    cur.execute(
        "SELECT e.doc_id, e.vector, e.dims, d.path, d.sha256, d.class, d.bytes, d.content "
        "FROM embeddings e JOIN documents d ON e.doc_id = d.doc_id"
    )

    scored: list[tuple[float, int, str, str, str, int, str]] = []
    query_dims = len(query_vec)

    for row in cur.fetchall():
        doc_id = int(row[0])
        blob = bytes(row[1])
        dims = int(row[2])
        path = str(row[3] or "")
        sha256 = str(row[4] or "")
        cls = str(row[5] or "")
        size_bytes = int(row[6] or 0)
        content = str(row[7] or "")

        if dims != query_dims:
            continue

        try:
            doc_vec = _blob_to_floats(blob, dims)
        except struct.error:
            continue
        vector_score = _cosine_similarity(query_vec, doc_vec)
        score = vector_score + _lexical_score(query_text, path, content)
        if min_score is not None and score < min_score:
            continue
        scored.append((score, doc_id, path, sha256, cls, size_bytes, content))

    scored.sort(key=lambda t: t[0], reverse=True)
    return [
        {
            "doc_id": doc_id,
            "score": round(score, 6),
            "path": path,
            "sha256": sha256,
            "class": cls,
            "bytes": size_bytes,
            "snippet": content[:600].replace("\n", " "),
            "content": content,
        }
        for score, doc_id, path, sha256, cls, size_bytes, content in scored[:safe_limit]
    ]


def search_text_documents(query: str, *, limit: int = 5, dims: int = DEFAULT_DIMS, min_score: float | None = None) -> list[dict]:
    """Public helper used by Knowledge and Chat to retrieve GPU Store context."""
    text = query.strip()
    if not text:
        return []
    conn = get_conn()
    try:
        embedding = _text_embedding(text, dims=dims)
        return _search_embedding(conn, embedding, limit=limit, min_score=min_score, query_text=text)
    finally:
        conn.close()


def _safe_resolve_user_path(raw_path: str) -> Path:
    if not raw_path.strip():
        raise HTTPException(status_code=400, detail="path is required")
    candidate = Path(raw_path).expanduser()
    if not candidate.is_absolute():
        candidate = PROJECT_ROOT / candidate
    resolved = candidate.resolve()
    allowed_roots = [PROJECT_ROOT, Path.home().resolve()]
    if not any(resolved == root or root in resolved.parents for root in allowed_roots):
        raise HTTPException(status_code=400, detail="path is outside allowed local roots")
    return resolved


def _read_text_file(path: Path, max_file_bytes: int) -> tuple[str | None, str | None]:
    try:
        stat = path.stat()
    except OSError as exc:
        return None, str(exc)
    if not path.is_file():
        return None, "not a regular file"
    if stat.st_size <= 0:
        return None, "empty file"
    if stat.st_size > max_file_bytes:
        return None, f"file too large: {stat.st_size} bytes"
    if path.suffix.lower() not in TEXT_EXTENSIONS:
        return None, f"unsupported extension: {path.suffix or '<none>'}"
    try:
        payload = path.read_bytes()
    except OSError as exc:
        return None, str(exc)
    if b"\x00" in payload[:4096]:
        return None, "binary file"
    return payload.decode("utf-8", errors="replace"), None


def _default_export_path() -> Path:
    stamp = datetime.datetime.now(datetime.UTC).strftime("%Y%m%d-%H%M%S")
    return PROJECT_ROOT / ".kolibri" / "exports" / f"gpu-store-{stamp}.kgpu"


def _export_rows(conn: sqlite3.Connection) -> list[dict]:
    rows = conn.execute(
        """
        SELECT d.doc_id, d.path, d.sha256, d.class, d.entropy, d.bytes, d.content,
               e.vector, e.dims
        FROM documents d
        LEFT JOIN embeddings e ON d.doc_id = e.doc_id
        ORDER BY d.doc_id ASC
        """
    ).fetchall()
    result: list[dict] = []
    for row in rows:
        vector_blob = bytes(row["vector"] or b"")
        result.append(
            {
                "doc_id": int(row["doc_id"]),
                "path": row["path"],
                "sha256": row["sha256"],
                "class": row["class"],
                "entropy": float(row["entropy"]),
                "bytes": int(row["bytes"]),
                "content": row["content"] or "",
                "embedding_b64": base64.b64encode(vector_blob).decode("ascii"),
                "dims": int(row["dims"] or DEFAULT_DIMS),
            }
        )
    return result


def _write_portable_corpus(path: Path, rows: list[dict]) -> tuple[int, str]:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "magic": PORTABLE_MAGIC,
        "version": 1,
        "created_at": datetime.datetime.now(datetime.UTC).isoformat(),
        "documents": rows,
    }
    raw = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    with gzip.open(path, "wb", compresslevel=9) as handle:
        handle.write(raw)
    data = path.read_bytes()
    return len(data), hashlib.sha256(data).hexdigest()


# ─── Модели ───

class StoreRequest(BaseModel):
    path: str
    sha256: str
    cls: str = Field(alias="class")
    entropy: float
    bytes: int
    content: str
    embedding: List[float]
    replace_embeddings: bool = True


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
    sha256: str = ""
    cls: str = Field(default="", alias="class")
    bytes: int = 0
    snippet: str = ""


class DocumentRequest(BaseModel):
    path: str
    content: str
    cls: str = Field(default="document", alias="class")
    dims: int = DEFAULT_DIMS
    replace_embeddings: bool = True


class DocumentResponse(BaseModel):
    doc_id: int
    embedding_id: int
    sha256: str
    bytes: int
    entropy: float
    dims: int
    embedding_preview: list[float]


class TextSearchRequest(BaseModel):
    query: str
    limit: int = 5
    dims: int = DEFAULT_DIMS


class BatchDocument(BaseModel):
    path: str
    content: str
    cls: str = Field(default="document", alias="class")
    dims: int = DEFAULT_DIMS


class BatchRequest(BaseModel):
    documents: list[BatchDocument]
    replace_embeddings: bool = True


class BatchItemResult(BaseModel):
    path: str
    doc_id: int | None = None
    embedding_id: int | None = None
    bytes: int = 0
    status: str
    detail: str | None = None


class BatchResponse(BaseModel):
    status: str
    indexed: int
    skipped: int
    bytes: int
    items: list[BatchItemResult]


class IndexPathRequest(BaseModel):
    path: str
    recursive: bool = True
    include_extensions: list[str] = Field(default_factory=list)
    max_files: int = 1000
    max_file_bytes: int = 1_000_000
    cls: str = Field(default="file", alias="class")
    dims: int = DEFAULT_DIMS
    replace_embeddings: bool = True


class PortableCorpusRequest(BaseModel):
    path: str | None = None


class ImportCorpusRequest(BaseModel):
    path: str
    clear_existing: bool = False
    replace_embeddings: bool = True


class PortableCorpusResponse(BaseModel):
    status: str
    path: str
    documents: int
    embeddings: int
    bytes: int
    sha256: str


class GpuStatusResponse(BaseModel):
    status: str
    db_path: str
    documents: int
    embeddings: int
    size_bytes: int


@router.get("/status", response_model=GpuStatusResponse)
async def status(conn: sqlite3.Connection = Depends(get_conn)) -> GpuStatusResponse:
    cur = conn.cursor()
    cur.execute("SELECT COUNT(*) FROM documents")
    documents = int(cur.fetchone()[0])
    cur.execute("SELECT COUNT(*) FROM embeddings")
    embeddings = int(cur.fetchone()[0])
    return GpuStatusResponse(
        status="ok",
        db_path=str(DB_PATH),
        documents=documents,
        embeddings=embeddings,
        size_bytes=DB_PATH.stat().st_size if DB_PATH.exists() else 0,
    )


@router.post("/store", response_model=StoreResponse)
async def store(req: StoreRequest, conn: sqlite3.Connection = Depends(get_conn)) -> StoreResponse:
    doc_id, embedding_id = _store_document(
        conn,
        path=req.path,
        sha256=req.sha256,
        cls=req.cls,
        entropy=req.entropy,
        size_bytes=req.bytes,
        content=req.content,
        embedding=list(req.embedding),
        replace_embeddings=req.replace_embeddings,
    )
    return StoreResponse(doc_id=doc_id, embedding_id=embedding_id)


@router.post("/document", response_model=DocumentResponse)
async def document(req: DocumentRequest, conn: sqlite3.Connection = Depends(get_conn)) -> DocumentResponse:
    payload = req.content.encode("utf-8", errors="replace")
    embedding = _text_embedding(req.content, dims=req.dims)
    sha256 = hashlib.sha256(payload).hexdigest()
    doc_id, embedding_id = _store_document(
        conn,
        path=req.path.strip() or f"document/{sha256[:12]}.txt",
        sha256=sha256,
        cls=req.cls,
        entropy=round(_entropy(payload), 6),
        size_bytes=len(payload),
        content=req.content,
        embedding=embedding,
        replace_embeddings=req.replace_embeddings,
    )
    return DocumentResponse(
        doc_id=doc_id,
        embedding_id=embedding_id,
        sha256=sha256,
        bytes=len(payload),
        entropy=round(_entropy(payload), 6),
        dims=len(embedding),
        embedding_preview=[round(value, 6) for value in embedding[:8]],
    )


@router.post("/batch", response_model=BatchResponse)
async def batch(req: BatchRequest, conn: sqlite3.Connection = Depends(get_conn)) -> BatchResponse:
    items: list[BatchItemResult] = []
    indexed = 0
    total_bytes = 0

    for item in req.documents[:1000]:
        content = item.content
        payload = content.encode("utf-8", errors="replace")
        if not content.strip():
            items.append(BatchItemResult(path=item.path, status="skipped", detail="empty content"))
            continue
        embedding = _text_embedding(content, dims=item.dims)
        sha256 = hashlib.sha256(payload).hexdigest()
        doc_id, embedding_id = _store_document(
            conn,
            path=item.path.strip() or f"document/{sha256[:12]}.txt",
            sha256=sha256,
            cls=item.cls,
            entropy=round(_entropy(payload), 6),
            size_bytes=len(payload),
            content=content,
            embedding=embedding,
            replace_embeddings=req.replace_embeddings,
        )
        indexed += 1
        total_bytes += len(payload)
        items.append(
            BatchItemResult(
                path=item.path,
                doc_id=doc_id,
                embedding_id=embedding_id,
                bytes=len(payload),
                status="indexed",
            )
        )

    skipped = len(items) - indexed
    return BatchResponse(status="ok", indexed=indexed, skipped=skipped, bytes=total_bytes, items=items)


@router.post("/index-path", response_model=BatchResponse)
async def index_path(req: IndexPathRequest, conn: sqlite3.Connection = Depends(get_conn)) -> BatchResponse:
    root = _safe_resolve_user_path(req.path)
    if not root.exists():
        raise HTTPException(status_code=404, detail="path not found")

    include_ext = {ext.lower() if ext.startswith(".") else f".{ext.lower()}" for ext in req.include_extensions}
    max_files = max(1, min(20_000, int(req.max_files or 1000)))
    max_file_bytes = max(1, min(50_000_000, int(req.max_file_bytes or 1_000_000)))

    paths: list[Path] = []
    if root.is_file():
        paths = [root]
    elif root.is_dir():
        iterator = root.rglob("*") if req.recursive else root.glob("*")
        for candidate in iterator:
            if len(paths) >= max_files:
                break
            if any(part in SKIP_DIRS for part in candidate.parts):
                continue
            if not candidate.is_file():
                continue
            suffix = candidate.suffix.lower()
            if include_ext and suffix not in include_ext:
                continue
            paths.append(candidate)
    else:
        raise HTTPException(status_code=400, detail="unsupported path type")

    items: list[BatchItemResult] = []
    indexed = 0
    total_bytes = 0

    for path in paths:
        content, error = _read_text_file(path, max_file_bytes)
        rel_path = str(path)
        try:
            rel_path = str(path.relative_to(root if root.is_dir() else root.parent))
        except ValueError:
            pass
        store_path = f"{root.name}/{rel_path}" if root.is_dir() else path.name
        if error or content is None:
            items.append(BatchItemResult(path=store_path, status="skipped", detail=error))
            continue

        payload = content.encode("utf-8", errors="replace")
        embedding = _text_embedding(content, dims=req.dims)
        sha256 = hashlib.sha256(payload).hexdigest()
        doc_id, embedding_id = _store_document(
            conn,
            path=store_path,
            sha256=sha256,
            cls=req.cls,
            entropy=round(_entropy(payload), 6),
            size_bytes=len(payload),
            content=content,
            embedding=embedding,
            replace_embeddings=req.replace_embeddings,
        )
        indexed += 1
        total_bytes += len(payload)
        items.append(
            BatchItemResult(
                path=store_path,
                doc_id=doc_id,
                embedding_id=embedding_id,
                bytes=len(payload),
                status="indexed",
            )
        )

    return BatchResponse(
        status="ok",
        indexed=indexed,
        skipped=len(items) - indexed,
        bytes=total_bytes,
        items=items,
    )


@router.post("/search", response_model=List[SearchHit])
async def search(req: SearchRequest, conn: sqlite3.Connection = Depends(get_conn)) -> list[SearchHit]:
    """
    Поиск ближайших документов по cosine-similarity.

    Полный перебор всех эмбеддингов (brute-force) — достаточен
    для масштабов до ~100k документов. Для бо́льших объёмов
    следует перейти на FAISS/Annoy/HNSW-индекс.
    """
    top = _search_embedding(conn, req.embedding, limit=req.limit)
    return [
        SearchHit(
            doc_id=int(hit["doc_id"]),
            score=float(hit["score"]),
            path=str(hit["path"]),
            sha256=str(hit["sha256"]),
            **{
                "class": str(hit["class"]),
                "bytes": int(hit["bytes"]),
                "snippet": str(hit["snippet"])[:240],
            },
        )
        for hit in top
    ]


@router.post("/search/text", response_model=List[SearchHit])
async def search_text(req: TextSearchRequest, conn: sqlite3.Connection = Depends(get_conn)) -> list[SearchHit]:
    embedding = _text_embedding(req.query, dims=req.dims)
    top = _search_embedding(conn, embedding, limit=req.limit, query_text=req.query)
    return [
        SearchHit(
            doc_id=int(hit["doc_id"]),
            score=float(hit["score"]),
            path=str(hit["path"]),
            sha256=str(hit["sha256"]),
            **{
                "class": str(hit["class"]),
                "bytes": int(hit["bytes"]),
                "snippet": str(hit["snippet"])[:240],
            },
        )
        for hit in top
    ]


@router.post("/export", response_model=PortableCorpusResponse)
async def export_corpus(req: PortableCorpusRequest, conn: sqlite3.Connection = Depends(get_conn)) -> PortableCorpusResponse:
    path = _safe_resolve_user_path(req.path) if req.path else _default_export_path()
    rows = _export_rows(conn)
    size_bytes, digest = _write_portable_corpus(path, rows)
    embeddings = sum(1 for row in rows if row.get("embedding_b64"))
    return PortableCorpusResponse(
        status="ok",
        path=str(path),
        documents=len(rows),
        embeddings=embeddings,
        bytes=size_bytes,
        sha256=digest,
    )


@router.post("/import", response_model=PortableCorpusResponse)
async def import_corpus(req: ImportCorpusRequest, conn: sqlite3.Connection = Depends(get_conn)) -> PortableCorpusResponse:
    path = _safe_resolve_user_path(req.path)
    if not path.exists():
        raise HTTPException(status_code=404, detail="corpus file not found")
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    try:
        with gzip.open(path, "rb") as handle:
            payload = json.loads(handle.read().decode("utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise HTTPException(status_code=400, detail=f"invalid portable corpus: {exc}") from exc

    if not isinstance(payload, dict) or payload.get("magic") != PORTABLE_MAGIC:
        raise HTTPException(status_code=400, detail="invalid GPU Store corpus magic")
    documents = payload.get("documents")
    if not isinstance(documents, list):
        raise HTTPException(status_code=400, detail="invalid GPU Store documents payload")

    if req.clear_existing:
        conn.execute("DELETE FROM embeddings")
        conn.execute("DELETE FROM documents")
        conn.commit()

    imported = 0
    embeddings = 0
    for item in documents:
        if not isinstance(item, dict):
            continue
        content = str(item.get("content") or "")
        vector_blob = base64.b64decode(str(item.get("embedding_b64") or ""), validate=False)
        dims = int(item.get("dims") or DEFAULT_DIMS)
        if not vector_blob:
            vector_blob = array("f", _text_embedding(content, dims=dims)).tobytes()
        _store_document_blob(
            conn,
            path=str(item.get("path") or f"document/{imported + 1}.txt"),
            sha256=str(item.get("sha256") or hashlib.sha256(content.encode("utf-8", errors="replace")).hexdigest()),
            cls=str(item.get("class") or "document"),
            entropy=float(item.get("entropy") or _entropy(content.encode("utf-8", errors="replace"))),
            size_bytes=int(item.get("bytes") or len(content.encode("utf-8", errors="replace"))),
            content=content,
            embedding_blob=vector_blob,
            dims=dims,
            replace_embeddings=req.replace_embeddings,
        )
        imported += 1
        embeddings += 1

    return PortableCorpusResponse(
        status="ok",
        path=str(path),
        documents=imported,
        embeddings=embeddings,
        bytes=len(data),
        sha256=digest,
    )
