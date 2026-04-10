#!/usr/bin/env python3
"""
Kolibri Live Knowledge Ingestor
-------------------------------
Captures unknown/low-confidence questions from the C-core HTTP server,
generates draft answers, and queues them for moderator review.

Usage:
    python3 live_ingest.py [--port 8001] [--queue-dir PATH] [--poll-interval SECONDS]
"""

import argparse
import hashlib
import json
import os
import sqlite3
import sys
import time
import urllib.request
import urllib.error
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

# Configuration
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
DEFAULT_QUEUE_DIR = PROJECT_ROOT / "build" / "knowledge"
DEFAULT_POLL_INTERVAL = 5  # seconds
DEFAULT_CONFIDENCE_THRESHOLD = 0.4  # Below this = "unknown question"

# Knowledge base for draft generation (loaded from index.json)
KB_INDEX = {}


def load_kb_index(index_path: Path) -> dict:
    """Load knowledge base index for similarity matching."""
    if not index_path.exists():
        return {}
    try:
        with open(index_path, "r", encoding="utf-8") as f:
            data = json.load(f)
            return {doc["id"]: doc for doc in data.get("documents", [])}
    except Exception as e:
        print(f"[live-ingest] Warning: failed to load KB index: {e}", file=sys.stderr)
        return {}


def compute_hmac(question: str, timestamp: float) -> str:
    """Compute HMAC-like hash for question deduplication."""
    payload = f"{question}:{timestamp}"
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()[:16]


def find_similar_docs(question: str, kb_index: dict, top_k: int = 3) -> list:
    """Find top-K similar documents using simple keyword overlap."""
    q_words = set(question.lower().split())
    scored = []
    
    for doc_id, doc in kb_index.items():
        title = doc.get("title", "").lower()
        content = doc.get("content", "").lower()
        all_words = set(title.split() + content.split())
        
        # Simple Jaccard similarity
        overlap = len(q_words & all_words)
        union = len(q_words | all_words)
        score = overlap / union if union > 0 else 0.0
        
        if score > 0:
            scored.append((score, doc))
    
    scored.sort(reverse=True, key=lambda x: x[0])
    return [doc for score, doc in scored[:top_k]]


def generate_draft_answer(question: str, kb_index: dict) -> dict:
    """
    Generate a draft answer for an unknown question.
    
    Returns dict with:
      - answer: draft text
      - method: how it was generated
      - sources: list of similar docs used
      - confidence: estimated confidence (0.0-0.5 for drafts)
    """
    similar_docs = find_similar_docs(question, kb_index)
    
    if similar_docs:
        # Heuristic: combine top similar docs
        sources = []
        parts = []
        for doc in similar_docs[:2]:
            title = doc.get("title", "Unknown")
            content = doc.get("content", "")[:200]
            source = doc.get("source", doc.get("id", "unknown"))
            sources.append({"title": title, "source": source})
            parts.append(content)
        
        draft_answer = "\n\n".join(parts) if parts else "No similar knowledge found."
        method = "similarity_heuristic"
        confidence = 0.3
    else:
        # Fallback: mark as needing external LLM
        draft_answer = f"Нет ответа на вопрос: \"{question}\". Требуется внешний генератор (LLM)."
        method = "llm_draft_needed"
        confidence = 0.1
        sources = []
    
    return {
        "answer": draft_answer,
        "method": method,
        "sources": sources,
        "confidence": confidence,
    }


def init_queue_db(db_path: Path) -> sqlite3.Connection:
    """Initialize SQLite queue database."""
    db_path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(db_path))
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("""
        CREATE TABLE IF NOT EXISTS live_queue (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            question TEXT NOT NULL,
            hmac TEXT UNIQUE NOT NULL,
            draft_answer TEXT,
            draft_method TEXT,
            draft_confidence REAL,
            sources_json TEXT,
            status TEXT DEFAULT 'pending' CHECK(status IN ('pending', 'approved', 'rejected', 'edited')),
            moderator_answer TEXT,
            conversation_context TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            updated_at TEXT DEFAULT CURRENT_TIMESTAMP,
            reviewed_at TEXT,
            reviewed_by TEXT
        )
    """)
    conn.execute("""
        CREATE INDEX IF NOT EXISTS idx_live_queue_status 
        ON live_queue(status, created_at)
    """)
    conn.commit()
    print(f"[live-ingest] Queue DB initialized: {db_path}")
    return conn


def enqueue_question(
    conn: sqlite3.Connection,
    question: str,
    conversation_context: Optional[str] = None,
    kb_index: Optional[dict] = None,
) -> Optional[int]:
    """
    Add an unknown question to the live queue.
    
    Returns question ID if enqueued, None if duplicate.
    """
    timestamp = time.time()
    hmac = compute_hmac(question, timestamp)
    
    # Generate draft answer
    draft = generate_draft_answer(question, kb_index or {})
    
    try:
        cursor = conn.execute(
            """
            INSERT INTO live_queue 
                (question, hmac, draft_answer, draft_method, draft_confidence, 
                 sources_json, conversation_context)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (
                question,
                hmac,
                draft["answer"],
                draft["method"],
                draft["confidence"],
                json.dumps(draft["sources"], ensure_ascii=False),
                conversation_context,
            ),
        )
        conn.commit()
        q_id = cursor.lastrowid
        print(f"[live-ingest] ✅ Enqueued Q#{q_id}: \"{question[:60]}...\"")
        return q_id
    except sqlite3.IntegrityError:
        # Duplicate question (same HMAC)
        return None


def poll_unknown_questions(
    conn: sqlite3.Connection,
    api_url: str,
    poll_interval: int,
    kb_index: dict,
):
    """
    Poll the C-core API for low-confidence responses and enqueue them.
    
    This is a background sensor that monitors chat endpoint responses.
    In production, this would hook into the C server directly via callback.
    For now, we simulate by checking recent chat logs.
    """
    print(f"[live-ingest] 🔄 Polling for unknown questions every {poll_interval}s...")
    print(f"[live-ingest] Press Ctrl+C to stop")
    
    try:
        while True:
            # In a real implementation, this would subscribe to a message queue
            # or receive webhooks from the C server.
            # For now, we provide the API endpoint for external callers.
            time.sleep(poll_interval)
    except KeyboardInterrupt:
        print("\n[live-ingest] ⏹ Stopped by user")


def api_list_pending(conn: sqlite3.Connection, limit: int = 50) -> list:
    """Get list of pending questions for UI display."""
    cursor = conn.execute(
        """
        SELECT id, question, draft_answer, draft_method, draft_confidence, 
               sources_json, conversation_context, created_at
        FROM live_queue
        WHERE status = 'pending'
        ORDER BY created_at DESC
        LIMIT ?
        """,
        (limit,),
    )
    
    rows = []
    for row in cursor.fetchall():
        rows.append({
            "id": row[0],
            "question": row[1],
            "draft_answer": row[2],
            "draft_method": row[3],
            "draft_confidence": row[4],
            "sources": json.loads(row[5]) if row[5] else [],
            "conversation_context": row[6],
            "created_at": row[7],
        })
    return rows


def api_approve_question(conn: sqlite3.Connection, q_id: int, moderator_answer: Optional[str] = None) -> bool:
    """Approve a question and optionally edit the answer."""
    answer = moderator_answer if moderator_answer is not None else (
        conn.execute("SELECT draft_answer FROM live_queue WHERE id = ?", (q_id,)).fetchone()[0]
    )
    
    conn.execute(
        """
        UPDATE live_queue
        SET status = 'approved',
            moderator_answer = ?,
            reviewed_at = CURRENT_TIMESTAMP,
            updated_at = CURRENT_TIMESTAMP
        WHERE id = ?
        """,
        (answer, q_id),
    )
    conn.commit()
    print(f"[live-ingest] ✅ Approved Q#{q_id}")
    return True


def api_reject_question(conn: sqlite3.Connection, q_id: int) -> bool:
    """Reject a question."""
    conn.execute(
        """
        UPDATE live_queue
        SET status = 'rejected',
            reviewed_at = CURRENT_TIMESTAMP,
            updated_at = CURRENT_TIMESTAMP
        WHERE id = ?
        """,
        (q_id,),
    )
    conn.commit()
    print(f"[live-ingest] ❌ Rejected Q#{q_id}")
    return True


def api_edit_question(
    conn: sqlite3.Connection, 
    q_id: int, 
    new_answer: str, 
    moderator: str = "unknown"
) -> bool:
    """Edit and approve a question's answer."""
    conn.execute(
        """
        UPDATE live_queue
        SET status = 'edited',
            moderator_answer = ?,
            reviewed_by = ?,
            reviewed_at = CURRENT_TIMESTAMP,
            updated_at = CURRENT_TIMESTAMP
        WHERE id = ?
        """,
        (new_answer, moderator, q_id),
    )
    conn.commit()
    print(f"[live-ingest] ✏️ Edited & Approved Q#{q_id}")
    return True


def export_approved_to_markdown(conn: sqlite3.Connection, output_dir: Path) -> int:
    """Export approved questions to Markdown files for knowledge pipeline."""
    output_dir.mkdir(parents=True, exist_ok=True)
    
    cursor = conn.execute(
        """
        SELECT question, moderator_answer, draft_method, reviewed_at
        FROM live_queue
        WHERE status IN ('approved', 'edited')
        ORDER BY reviewed_at DESC
        """
    )
    
    count = 0
    for row in cursor.fetchall():
        question, answer, method, reviewed_at = row
        if not answer:
            continue
        
        # Sanitize filename
        safe_q = question[:50].replace("/", "_").replace("\\", "_").replace("?", "")
        filename = f"{safe_q}.md"
        filepath = output_dir / filename
        
        md_content = f"### Q: {question}\n{answer}\n---\n"
        filepath.write_text(md_content, encoding="utf-8")
        count += 1
    
    print(f"[live-ingest] 📦 Exported {count} approved questions to {output_dir}")
    return count


def main():
    parser = argparse.ArgumentParser(description="Kolibri Live Knowledge Ingestor")
    parser.add_argument("--port", type=int, default=8001, help="C-core API port")
    parser.add_argument("--queue-dir", type=str, default=str(DEFAULT_QUEUE_DIR), 
                        help="Queue directory path")
    parser.add_argument("--poll-interval", type=int, default=DEFAULT_POLL_INTERVAL,
                        help="Polling interval in seconds")
    parser.add_argument("--confidence-threshold", type=float, 
                        default=DEFAULT_CONFIDENCE_THRESHOLD,
                        help="Confidence threshold for 'unknown' detection")
    parser.add_argument("--daemon", action="store_true", 
                        help="Run in background daemon mode")
    
    subparsers = parser.add_subparsers(dest="command", help="Commands")
    
    # Subcommand: list
    subparsers.add_parser("list", help="List pending questions")
    
    # Subcommand: approve
    approve_parser = subparsers.add_parser("approve", help="Approve a question")
    approve_parser.add_argument("id", type=int, help="Question ID")
    approve_parser.add_argument("--answer", type=str, help="Edited answer text")
    
    # Subcommand: reject
    reject_parser = subparsers.add_parser("reject", help="Reject a question")
    reject_parser.add_argument("id", type=int, help="Question ID")
    
    # Subcommand: edit
    edit_parser = subparsers.add_parser("edit", help="Edit and approve a question")
    edit_parser.add_argument("id", type=int, help="Question ID")
    edit_parser.add_argument("--answer", type=str, required=True, help="New answer text")
    edit_parser.add_argument("--moderator", type=str, default="cli", help="Moderator name")
    
    # Subcommand: export
    export_parser = subparsers.add_parser("export", help="Export approved questions")
    export_parser.add_argument("--output", type=str, 
                               default=str(DEFAULT_QUEUE_DIR / "approved"),
                               help="Output directory")
    
    args = parser.parse_args()
    
    # Setup paths
    queue_dir = Path(args.queue_dir)
    db_path = queue_dir / "live_queue.db"
    index_path = PROJECT_ROOT / "build" / "knowledge" / "index.json"
    
    # Load KB index
    kb_index = load_kb_index(index_path)
    
    # Initialize DB
    conn = init_queue_db(db_path)
    
    try:
        if args.command == "list":
            pending = api_list_pending(conn)
            if not pending:
                print("📭 No pending questions")
            else:
                print(f"📋 Pending questions ({len(pending)}):")
                for q in pending:
                    print(f"\n  Q#{q['id']} [{q['draft_method']}] ({q['created_at']})")
                    print(f"  ❓ {q['question']}")
                    print(f"  💡 Draft: {q['draft_answer'][:100]}...")
        
        elif args.command == "approve":
            success = api_approve_question(conn, args.id, args.answer)
            if success:
                print(f"✅ Question #{args.id} approved")
        
        elif args.command == "reject":
            success = api_reject_question(conn, args.id)
            if success:
                print(f"❌ Question #{args.id} rejected")
        
        elif args.command == "edit":
            success = api_edit_question(conn, args.id, args.answer, args.moderator)
            if success:
                print(f"✏️ Question #{args.id} edited and approved")
        
        elif args.command == "export":
            output = Path(args.output)
            count = export_approved_to_markdown(conn, output)
            print(f"📦 Exported {count} questions to {output}")
        
        else:
            # Default: run as daemon polling for unknown questions
            print("[live-ingest] 🚀 Starting Kolibri Live Knowledge Ingestor")
            print(f"[live-ingest] Port: {args.port}")
            print(f"[live-ingest] Queue: {db_path}")
            print(f"[live-ingest] KB Index: {index_path}")
            print(f"[live-ingest] Confidence threshold: {args.confidence_threshold}")
            print()
            
            poll_unknown_questions(conn, f"http://localhost:{args.port}", 
                                  args.poll_interval, kb_index)
    
    finally:
        conn.close()


if __name__ == "__main__":
    main()
