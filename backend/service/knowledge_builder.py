"""
Knowledge Graph Builder для Kolibri AI.

Собирает ReasonBlocks из документов и строит граф знаний.
Согласно kolibri_ai_masterplan.md (F2: Spectral + Knowledge).
"""
from __future__ import annotations

import datetime
import hashlib
import json
import sqlite3
import uuid
from pathlib import Path
from typing import Optional

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel

from .gpu_store import search_text_documents

# --- Конфигурация ---
KNOWLEDGE_DB_PATH = Path("build/knowledge/knowledge.db")
KNOWLEDGE_INDEX_PATH = Path("build/knowledge/index.json")


# --- Pydantic модели ---
class KnowledgeDocRequest(BaseModel):
    """Запрос на добавление документа в граф знаний."""
    title: str
    content: str
    source_path: Optional[str] = None
    doc_type: str = "docs"  # code, docs, kolibriscript, data
    tags: list[str] = []


class KnowledgeDoc(BaseModel):
    """Документ в базе знаний."""
    id: str
    title: str
    content_hash: str
    doc_type: str
    tags: list[str]
    entropy: float
    reason_block_id: Optional[str] = None
    created_at: datetime.datetime

    class Config:
        from_attributes = True


class KnowledgeRelation(BaseModel):
    """Связь между документами."""
    id: str
    source_id: str
    target_id: str
    relation_type: str  # references, derives_from, similar_to
    weight: float = 1.0


class ContextRequest(BaseModel):
    """Запрос контекста для ReasonBlock."""
    query: str
    max_results: int = 10


class ContextResult(BaseModel):
    """Результат поиска контекста."""
    doc_id: str
    title: str
    relevance: float
    snippet: str


class KnowledgeSnippet(BaseModel):
    id: str
    title: str
    content: str
    source: Optional[str] = None
    score: float = 0.0


class KnowledgeSearchResponse(BaseModel):
    snippets: list[KnowledgeSnippet]


# --- База данных ---
def init_db() -> sqlite3.Connection:
    """Инициализирует SQLite базу знаний."""
    KNOWLEDGE_DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(KNOWLEDGE_DB_PATH), check_same_thread=False)
    conn.row_factory = sqlite3.Row
    
    conn.executescript("""
        CREATE TABLE IF NOT EXISTS documents (
            id TEXT PRIMARY KEY,
            title TEXT NOT NULL,
            content TEXT NOT NULL,
            content_hash TEXT UNIQUE NOT NULL,
            doc_type TEXT DEFAULT 'docs',
            tags TEXT DEFAULT '[]',
            entropy REAL DEFAULT 0.0,
            reason_block_id TEXT,
            source_path TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
        
        CREATE TABLE IF NOT EXISTS relations (
            id TEXT PRIMARY KEY,
            source_id TEXT NOT NULL,
            target_id TEXT NOT NULL,
            relation_type TEXT NOT NULL,
            weight REAL DEFAULT 1.0,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (source_id) REFERENCES documents(id),
            FOREIGN KEY (target_id) REFERENCES documents(id)
        );
        
        CREATE TABLE IF NOT EXISTS reason_blocks (
            id TEXT PRIMARY KEY,
            doc_id TEXT NOT NULL,
            block_type TEXT NOT NULL,
            payload TEXT NOT NULL,
            genome_position INTEGER,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (doc_id) REFERENCES documents(id)
        );

        CREATE TABLE IF NOT EXISTS feedback_events (
            id TEXT PRIMARY KEY,
            question TEXT NOT NULL,
            answer TEXT NOT NULL,
            rating TEXT NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
        
        CREATE INDEX IF NOT EXISTS idx_docs_type ON documents(doc_type);
        CREATE INDEX IF NOT EXISTS idx_docs_hash ON documents(content_hash);
        CREATE INDEX IF NOT EXISTS idx_relations_source ON relations(source_id);
        CREATE INDEX IF NOT EXISTS idx_relations_target ON relations(target_id);
    """)
    conn.commit()
    return conn


# --- Глобальное соединение ---
_db_conn: Optional[sqlite3.Connection] = None


def get_db() -> sqlite3.Connection:
    """Возвращает соединение с базой знаний."""
    global _db_conn
    if _db_conn is None:
        _db_conn = init_db()
    return _db_conn


# --- Вычисление энтропии ---
def calc_entropy(data: str) -> float:
    """Вычисляет энтропию текста."""
    import math
    if not data:
        return 0.0
    counts: dict[str, int] = {}
    for ch in data:
        counts[ch] = counts.get(ch, 0) + 1
    entropy = 0.0
    total = len(data)
    for count in counts.values():
        p = count / total
        entropy -= p * math.log2(p)
    return round(entropy, 5)


# --- Извлечение ключевых слов ---
def extract_keywords(content: str) -> list[str]:
    """Извлекает ключевые слова из текста."""
    import re
    # Простое извлечение: слова длиной > 4 символов
    words = re.findall(r'\b[а-яёa-z]{5,}\b', content.lower())
    # Топ-10 по частоте
    freq: dict[str, int] = {}
    for w in words:
        freq[w] = freq.get(w, 0) + 1
    sorted_words = sorted(freq.items(), key=lambda x: -x[1])
    return [w for w, _ in sorted_words[:10]]


# --- API Router ---
router = APIRouter(prefix="/api/knowledge", tags=["Knowledge Graph"])


@router.get("/search", response_model=KnowledgeSearchResponse)
async def search_knowledge(
    q: str,
    limit: int = 5,
    db: sqlite3.Connection = Depends(get_db),
) -> KnowledgeSearchResponse:
    """Простой текстовый поиск, совместимый с фронтенд-клиентом."""
    query = q.strip()
    if not query:
        return KnowledgeSearchResponse(snippets=[])

    terms = [term for term in query.lower().split() if term]
    rows = db.execute("""
        SELECT id, title, content, source_path
        FROM documents
        ORDER BY created_at DESC
        LIMIT 200
    """).fetchall()

    snippets: list[KnowledgeSnippet] = []
    for row in rows:
        title = row["title"] or ""
        content = row["content"] or ""
        haystack = f"{title}\n{content}".lower()
        score = 0.0
        for term in terms:
            if term in title.lower():
                score += 3.0
            if term in haystack:
                score += 1.0
        if score <= 0.0:
            continue
        snippets.append(
            KnowledgeSnippet(
                id=row["id"],
                title=title or "Без названия",
                content=content[:400],
                source=row["source_path"],
                score=score,
            )
        )

    snippets.sort(key=lambda item: item.score, reverse=True)
    try:
        gpu_hits = search_text_documents(query, limit=max(1, min(limit, 20)), min_score=0.05)
    except Exception:
        gpu_hits = []

    seen_ids = {snippet.id for snippet in snippets}
    for hit in gpu_hits:
        snippet_id = f"gpu:{hit.get('doc_id', '')}"
        content = str(hit.get("snippet") or "").strip()
        if not content or snippet_id in seen_ids:
            continue
        snippets.append(
            KnowledgeSnippet(
                id=snippet_id,
                title=str(hit.get("path") or "GPU Store document"),
                content=content[:400],
                source=str(hit.get("path") or ""),
                score=float(hit.get("score") or 0.0) * 10.0,
            )
        )
        seen_ids.add(snippet_id)

    snippets.sort(key=lambda item: item.score, reverse=True)
    return KnowledgeSearchResponse(snippets=snippets[: max(1, min(limit, 20))])


@router.post("/doc", response_model=KnowledgeDoc)
async def add_document(req: KnowledgeDocRequest, db: sqlite3.Connection = Depends(get_db)):
    """Добавляет документ в граф знаний."""
    content_hash = hashlib.sha256(req.content.encode()).hexdigest()
    
    # Проверяем дубликат
    existing = db.execute(
        "SELECT id FROM documents WHERE content_hash = ?",
        (content_hash,)
    ).fetchone()
    
    if existing:
        raise HTTPException(400, f"Document already exists: {existing['id']}")
    
    doc_id = str(uuid.uuid4())
    entropy = calc_entropy(req.content)
    
    # Извлекаем ключевые слова если теги не указаны
    tags = req.tags if req.tags else extract_keywords(req.content)
    
    db.execute("""
        INSERT INTO documents (id, title, content, content_hash, doc_type, tags, entropy, source_path)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    """, (doc_id, req.title, req.content, content_hash, req.doc_type, json.dumps(tags), entropy, req.source_path))
    db.commit()
    
    return KnowledgeDoc(
        id=doc_id,
        title=req.title,
        content_hash=content_hash,
        doc_type=req.doc_type,
        tags=tags,
        entropy=entropy,
        created_at=datetime.datetime.now(datetime.UTC)
    )


@router.get("/teach")
async def teach_pair(
    q: str,
    a: str,
    db: sqlite3.Connection = Depends(get_db),
):
    """Сохраняет Q/A пару из UI как документ знаний."""
    question = q.strip()
    answer = a.strip()
    if not question or not answer:
        raise HTTPException(400, "question and answer are required")

    content = f"Q: {question}\nA: {answer}"
    content_hash = hashlib.sha256(content.encode()).hexdigest()
    existing = db.execute(
        "SELECT id FROM documents WHERE content_hash = ?",
        (content_hash,),
    ).fetchone()
    if existing:
        return {"status": "exists", "id": existing["id"]}

    doc_id = str(uuid.uuid4())
    tags = extract_keywords(f"{question} {answer}")
    db.execute("""
        INSERT INTO documents (id, title, content, content_hash, doc_type, tags, entropy, source_path)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    """, (
        doc_id,
        question[:120],
        content,
        content_hash,
        "chat",
        json.dumps(tags),
        calc_entropy(content),
        "chat://frontend",
    ))
    db.commit()
    return {"status": "stored", "id": doc_id}


@router.get("/feedback")
async def store_feedback(
    rating: str,
    q: str,
    a: str,
    db: sqlite3.Connection = Depends(get_db),
):
    """Принимает feedback из UI и пишет лёгкий журнал событий."""
    if rating not in {"good", "bad"}:
        raise HTTPException(400, "rating must be 'good' or 'bad'")
    db.execute("""
        INSERT INTO feedback_events (id, question, answer, rating)
        VALUES (?, ?, ?, ?)
    """, (str(uuid.uuid4()), q.strip(), a.strip(), rating))
    db.commit()
    return {"status": "accepted"}


@router.get("/context", response_model=list[ContextResult])
async def get_context(query: str, max_results: int = 10, db: sqlite3.Connection = Depends(get_db)):
    """Ищет релевантный контекст для запроса."""
    # Простой поиск по ключевым словам
    keywords = query.lower().split()
    
    results: list[ContextResult] = []
    
    rows = db.execute("""
        SELECT id, title, content, tags FROM documents
        ORDER BY created_at DESC
        LIMIT 100
    """).fetchall()
    
    for row in rows:
        # Вычисляем релевантность
        content_lower = row["content"].lower()
        title_lower = row["title"].lower()
        tags = json.loads(row["tags"]) if row["tags"] else []
        
        relevance = 0.0
        for kw in keywords:
            if kw in title_lower:
                relevance += 3.0
            if kw in content_lower:
                relevance += 1.0
            if kw in tags:
                relevance += 2.0
        
        if relevance > 0:
            # Извлекаем сниппет
            snippet = row["content"][:200] + "..." if len(row["content"]) > 200 else row["content"]
            results.append(ContextResult(
                doc_id=row["id"],
                title=row["title"],
                relevance=relevance,
                snippet=snippet
            ))
    
    # Сортируем по релевантности
    results.sort(key=lambda x: -x.relevance)
    return results[:max_results]


@router.get("/stats")
async def get_stats(db: sqlite3.Connection = Depends(get_db)):
    """Возвращает статистику базы знаний."""
    doc_count = db.execute("SELECT COUNT(*) as cnt FROM documents").fetchone()["cnt"]
    rel_count = db.execute("SELECT COUNT(*) as cnt FROM relations").fetchone()["cnt"]
    rb_count = db.execute("SELECT COUNT(*) as cnt FROM reason_blocks").fetchone()["cnt"]
    
    type_stats = db.execute("""
        SELECT doc_type, COUNT(*) as cnt 
        FROM documents 
        GROUP BY doc_type
    """).fetchall()
    
    return {
        "documents": doc_count,
        "relations": rel_count,
        "reason_blocks": rb_count,
        "by_type": {row["doc_type"]: row["cnt"] for row in type_stats}
    }


@router.post("/relation")
async def add_relation(
    source_id: str,
    target_id: str,
    relation_type: str = "references",
    weight: float = 1.0,
    db: sqlite3.Connection = Depends(get_db)
):
    """Добавляет связь между документами."""
    rel_id = str(uuid.uuid4())
    
    db.execute("""
        INSERT INTO relations (id, source_id, target_id, relation_type, weight)
        VALUES (?, ?, ?, ?, ?)
    """, (rel_id, source_id, target_id, relation_type, weight))
    db.commit()
    
    return {"id": rel_id, "status": "created"}


@router.get("/graph")
async def get_graph(db: sqlite3.Connection = Depends(get_db)):
    """Возвращает граф знаний для визуализации."""
    nodes = db.execute("""
        SELECT id, title, doc_type, entropy 
        FROM documents
    """).fetchall()
    
    edges = db.execute("""
        SELECT source_id, target_id, relation_type, weight
        FROM relations
    """).fetchall()
    
    return {
        "nodes": [dict(row) for row in nodes],
        "edges": [dict(row) for row in edges]
    }


# --- Экспорт индекса ---
def export_index():
    """Экспортирует индекс знаний в JSON."""
    db = get_db()
    docs = db.execute("SELECT id, title, doc_type, tags, entropy FROM documents").fetchall()
    
    index = {
        "version": "1.0",
        "exported_at": datetime.datetime.now(datetime.UTC).isoformat(),
        "documents": [dict(row) for row in docs]
    }
    
    KNOWLEDGE_INDEX_PATH.parent.mkdir(parents=True, exist_ok=True)
    with KNOWLEDGE_INDEX_PATH.open("w", encoding="utf-8") as f:
        json.dump(index, f, ensure_ascii=False, indent=2)
    
    return index
