"""
Kolibri Persistence — хранение состояния графа знаний в SQLite.

Обеспечивает персистентность между перезапусками сервера:
  - Паттерны (word → pattern + fitness)
  - Рёбра (edge weights, co-occurrence counts)
  - Метаданные (документы, эпохи, версия)

Режимы:
  - KOLIBRI_DB_PATH=/path/to/kolibri.db — путь к базе (по умолчанию data/kolibri.db)
  - KOLIBRI_PERSISTENCE=0 — отключить (только in-memory)
"""
from __future__ import annotations

import json
import logging
import os
import sqlite3
import threading
import time
from pathlib import Path
from typing import Optional

log = logging.getLogger("kolibri.persistence")

_PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
_DEFAULT_DB_PATH = _PROJECT_ROOT / "data" / "kolibri.db"
_DB_PATH = Path(os.getenv("KOLIBRI_DB_PATH", str(_DEFAULT_DB_PATH)))
_PERSISTENCE_ENABLED = os.getenv("KOLIBRI_PERSISTENCE", "1").strip() != "0"
_DB_RETRY_ATTEMPTS = max(1, int(os.getenv("KOLIBRI_DB_RETRY_ATTEMPTS", "5")))
_DB_RETRY_BASE_DELAY = max(0.01, float(os.getenv("KOLIBRI_DB_RETRY_BASE_DELAY", "0.05")))
_DB_BATCH_SIZE = max(200, int(os.getenv("KOLIBRI_DB_BATCH_SIZE", "4000")))
_DB_API_LOCK_TIMEOUT = max(0.05, float(os.getenv("KOLIBRI_DB_API_LOCK_TIMEOUT", "0.25")))

# ---------------------------------------------------------------------------
# Схема БД
# ---------------------------------------------------------------------------

_SCHEMA = """
CREATE TABLE IF NOT EXISTS patterns (
    hash        INTEGER PRIMARY KEY,
    word        TEXT NOT NULL,
    pattern     TEXT NOT NULL,
    fitness     REAL DEFAULT 0.0,
    frequency   INTEGER DEFAULT 1,
    version     INTEGER DEFAULT 1,
    updated_at  REAL DEFAULT 0.0
);

CREATE TABLE IF NOT EXISTS edges (
    source_hash INTEGER NOT NULL,
    target_hash INTEGER NOT NULL,
    weight      REAL DEFAULT 1.0,
    cooccurrence INTEGER DEFAULT 1,
    version     INTEGER DEFAULT 1,
    PRIMARY KEY (source_hash, target_hash)
);

CREATE TABLE IF NOT EXISTS metadata (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS user_profiles (
    client_id   TEXT PRIMARY KEY,
    payload     TEXT NOT NULL,
    updated_at  REAL DEFAULT 0.0
);

CREATE TABLE IF NOT EXISTS quality_benchmarks (
    run_id   TEXT PRIMARY KEY,
    run_at   REAL NOT NULL,
    score    REAL DEFAULT 0.0,
    passed   INTEGER DEFAULT 0,
    total    INTEGER DEFAULT 0,
    payload  TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS conversation_turns (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    client_id       TEXT NOT NULL,
    conversation_id TEXT NOT NULL,
    role            TEXT NOT NULL,
    content         TEXT NOT NULL,
    created_at      REAL NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_edges_source ON edges(source_hash);
CREATE INDEX IF NOT EXISTS idx_edges_target ON edges(target_hash);
CREATE INDEX IF NOT EXISTS idx_patterns_word ON patterns(word);
CREATE INDEX IF NOT EXISTS idx_quality_benchmarks_run_at ON quality_benchmarks(run_at DESC);
CREATE INDEX IF NOT EXISTS idx_conversation_turns_lookup
    ON conversation_turns(client_id, conversation_id, created_at DESC);
"""


class KolibriDB:
    """Persistent storage для графа знаний."""

    def __init__(self, db_path: Optional[Path] = None) -> None:
        self.db_path = db_path or _DB_PATH
        self._conn: Optional[sqlite3.Connection] = None
        self._lock = threading.RLock()

    def is_enabled(self) -> bool:
        return _PERSISTENCE_ENABLED

    def connect(self) -> None:
        """Установить соединение и создать таблицы."""
        if not _PERSISTENCE_ENABLED:
            return
        with self._lock:
            if self._conn is not None:
                return
            self.db_path.parent.mkdir(parents=True, exist_ok=True)
            self._conn = sqlite3.connect(
                str(self.db_path),
                check_same_thread=False,
                timeout=10.0,
            )
            self._conn.execute("PRAGMA journal_mode=WAL")
            self._conn.execute("PRAGMA synchronous=NORMAL")
            self._conn.execute("PRAGMA busy_timeout=10000")
            self._conn.execute("PRAGMA cache_size=-64000")  # 64 MB cache
            self._conn.executescript(_SCHEMA)
            self._conn.commit()
            log.info("SQLite подключён: %s", self.db_path)

    def close(self) -> None:
        with self._lock:
            if self._conn:
                self._conn.close()
                self._conn = None

    def _execute_with_retry(self, fn):
        last_exc: Exception | None = None
        for attempt in range(1, _DB_RETRY_ATTEMPTS + 1):
            try:
                return fn()
            except sqlite3.OperationalError as exc:
                last_exc = exc
                msg = str(exc).lower()
                if "locked" not in msg and "busy" not in msg:
                    raise
                if attempt >= _DB_RETRY_ATTEMPTS:
                    raise
                delay = _DB_RETRY_BASE_DELAY * attempt
                time.sleep(delay)
            except sqlite3.DatabaseError as exc:
                # Некоторые race-condition'ы приходят как DatabaseError.
                last_exc = exc
                msg = str(exc).lower()
                if ("locked" not in msg and "busy" not in msg and "misuse" not in msg):
                    raise
                if attempt >= _DB_RETRY_ATTEMPTS:
                    raise
                delay = _DB_RETRY_BASE_DELAY * attempt
                time.sleep(delay)
        if last_exc:
            raise last_exc

    # ------------------------------------------------------------------
    # Паттерны
    # ------------------------------------------------------------------

    def save_patterns(
        self,
        patterns: dict[int, object],
        hash_to_word: dict[int, str],
    ) -> int:
        """Сохранить паттерны в базу (batch upsert)."""
        with self._lock:
            if not self._conn:
                return 0
            t0 = time.time()
            rows = []
            now = time.time()
            for h, pat in patterns.items():
                word = hash_to_word.get(h, "")
                digits = getattr(pat, "digits", [])
                fitness = getattr(pat, "fitness", 0.0)
                freq = getattr(pat, "frequency", 1)
                rows.append((
                    h, word, json.dumps(digits), fitness, freq, 1, now,
                ))
            if not rows:
                return 0

            def _write() -> None:
                for start in range(0, len(rows), _DB_BATCH_SIZE):
                    chunk = rows[start:start + _DB_BATCH_SIZE]
                    self._conn.executemany(
                        """INSERT OR REPLACE INTO patterns
                           (hash, word, pattern, fitness, frequency, version, updated_at)
                           VALUES (?, ?, ?, ?, ?, ?, ?)""",
                        chunk,
                    )
                self._conn.commit()

            self._execute_with_retry(_write)
            log.info("Сохранено %d паттернов за %.1fms", len(rows), (time.time() - t0) * 1000)
            return len(rows)

    def load_patterns(self) -> list[dict]:
        """Загрузить все паттерны из базы."""
        with self._lock:
            if not self._conn:
                return []

            def _read():
                cursor = self._conn.execute(
                    "SELECT hash, word, pattern, fitness, frequency FROM patterns"
                )
                return cursor.fetchall()

            rows = self._execute_with_retry(_read) or []
            result = []
            for row in rows:
                result.append({
                    "hash": row[0],
                    "word": row[1],
                    "pattern": json.loads(row[2]),
                    "fitness": row[3],
                    "frequency": row[4],
                })
            return result

    # ------------------------------------------------------------------
    # Рёбра
    # ------------------------------------------------------------------

    def save_edges(self, edges: dict[tuple[int, int], object]) -> int:
        """Сохранить рёбра графа."""
        with self._lock:
            if not self._conn:
                return 0
            t0 = time.time()
            rows = []
            for (src, tgt), edge in edges.items():
                weight = getattr(edge, "weight", 1.0)
                cooc = getattr(edge, "cooccurrence", 1)
                rows.append((src, tgt, weight, cooc, 1))
            if not rows:
                return 0

            def _write() -> None:
                for start in range(0, len(rows), _DB_BATCH_SIZE):
                    chunk = rows[start:start + _DB_BATCH_SIZE]
                    self._conn.executemany(
                        """INSERT OR REPLACE INTO edges
                           (source_hash, target_hash, weight, cooccurrence, version)
                           VALUES (?, ?, ?, ?, ?)""",
                        chunk,
                    )
                self._conn.commit()

            self._execute_with_retry(_write)
            log.info("Сохранено %d рёбер за %.1fms", len(rows), (time.time() - t0) * 1000)
            return len(rows)

    def load_edges(self) -> list[dict]:
        """Загрузить все рёбра."""
        with self._lock:
            if not self._conn:
                return []

            def _read():
                cursor = self._conn.execute(
                    "SELECT source_hash, target_hash, weight, cooccurrence FROM edges"
                )
                return cursor.fetchall()

            rows = self._execute_with_retry(_read) or []
            return [
                {
                    "source_hash": row[0],
                    "target_hash": row[1],
                    "weight": row[2],
                    "cooccurrence": row[3],
                }
                for row in rows
            ]

    # ------------------------------------------------------------------
    # Метаданные
    # ------------------------------------------------------------------

    def set_meta(self, key: str, value: str) -> None:
        with self._lock:
            if not self._conn:
                return

            def _write() -> None:
                self._conn.execute(
                    "INSERT OR REPLACE INTO metadata (key, value) VALUES (?, ?)",
                    (key, value),
                )
                self._conn.commit()

            self._execute_with_retry(_write)

    def get_meta(self, key: str, default: str = "") -> str:
        if not self._lock.acquire(timeout=_DB_API_LOCK_TIMEOUT):
            return default
        try:
            if not self._conn:
                return default

            def _read():
                cursor = self._conn.execute(
                    "SELECT value FROM metadata WHERE key = ?", (key,)
                )
                return cursor.fetchone()

            row = self._execute_with_retry(_read)
            return row[0] if row else default
        finally:
            self._lock.release()

    # ------------------------------------------------------------------
    # Профили пользователей (персистентная память по client_id)
    # ------------------------------------------------------------------

    def set_user_profile(self, client_id: str, payload: str, updated_at: float | None = None) -> None:
        if not self._lock.acquire(timeout=_DB_API_LOCK_TIMEOUT):
            return
        try:
            if not self._conn:
                return

            ts = float(updated_at if updated_at is not None else time.time())

            def _write() -> None:
                self._conn.execute(
                    """
                    INSERT INTO user_profiles (client_id, payload, updated_at)
                    VALUES (?, ?, ?)
                    ON CONFLICT(client_id) DO UPDATE SET
                        payload=excluded.payload,
                        updated_at=excluded.updated_at
                    """,
                    (client_id, payload, ts),
                )
                self._conn.commit()

            self._execute_with_retry(_write)
        finally:
            self._lock.release()

    def get_user_profile(self, client_id: str, default: str = "") -> str:
        if not self._lock.acquire(timeout=_DB_API_LOCK_TIMEOUT):
            return default
        try:
            if not self._conn:
                return default

            def _read():
                cursor = self._conn.execute(
                    "SELECT payload FROM user_profiles WHERE client_id = ?",
                    (client_id,),
                )
                return cursor.fetchone()

            row = self._execute_with_retry(_read)
            return row[0] if row else default
        finally:
            self._lock.release()

    # ------------------------------------------------------------------
    # Quality benchmarks (регулярные контрольные прогоны)
    # ------------------------------------------------------------------

    def save_quality_benchmark(
        self,
        run_id: str,
        run_at: float,
        score: float,
        passed: int,
        total: int,
        payload: str,
    ) -> None:
        if not self._lock.acquire(timeout=_DB_API_LOCK_TIMEOUT):
            log.warning("save_quality_benchmark skipped: DB API lock timeout")
            return
        try:
            if not self._conn:
                return

            def _write() -> None:
                self._conn.execute(
                    """
                    INSERT OR REPLACE INTO quality_benchmarks
                        (run_id, run_at, score, passed, total, payload)
                    VALUES (?, ?, ?, ?, ?, ?)
                    """,
                    (run_id, float(run_at), float(score), int(passed), int(total), payload),
                )
                self._conn.commit()

            self._execute_with_retry(_write)
        finally:
            self._lock.release()

    def get_latest_quality_benchmark(self) -> dict | None:
        if not self._lock.acquire(timeout=_DB_API_LOCK_TIMEOUT):
            return None
        try:
            if not self._conn:
                return None

            def _read():
                cursor = self._conn.execute(
                    """
                    SELECT run_id, run_at, score, passed, total, payload
                    FROM quality_benchmarks
                    ORDER BY run_at DESC
                    LIMIT 1
                    """
                )
                return cursor.fetchone()

            row = self._execute_with_retry(_read)
            if not row:
                return None
            return {
                "run_id": row[0],
                "run_at": float(row[1] or 0.0),
                "score": float(row[2] or 0.0),
                "passed": int(row[3] or 0),
                "total": int(row[4] or 0),
                "payload": str(row[5] or "{}"),
            }
        finally:
            self._lock.release()

    def list_quality_benchmarks(self, limit: int = 20) -> list[dict]:
        if not self._lock.acquire(timeout=_DB_API_LOCK_TIMEOUT):
            return []
        try:
            if not self._conn:
                return []
            safe_limit = max(1, min(200, int(limit)))

            def _read():
                cursor = self._conn.execute(
                    """
                    SELECT run_id, run_at, score, passed, total, payload
                    FROM quality_benchmarks
                    ORDER BY run_at DESC
                    LIMIT ?
                    """,
                    (safe_limit,),
                )
                return cursor.fetchall()

            rows = self._execute_with_retry(_read) or []
            result: list[dict] = []
            for row in rows:
                result.append(
                    {
                        "run_id": row[0],
                        "run_at": float(row[1] or 0.0),
                        "score": float(row[2] or 0.0),
                        "passed": int(row[3] or 0),
                        "total": int(row[4] or 0),
                        "payload": str(row[5] or "{}"),
                    }
                )
            return result
        finally:
            self._lock.release()

    # ------------------------------------------------------------------
    # Персистентные диалоги (контекст между перезапусками)
    # ------------------------------------------------------------------

    def append_conversation_turn(
        self,
        client_id: str,
        conversation_id: str,
        role: str,
        content: str,
        created_at: float | None = None,
    ) -> None:
        with self._lock:
            if not self._conn:
                return
            ts = float(created_at if created_at is not None else time.time())
            role_clean = str(role or "").strip().lower()[:32]
            if role_clean not in {"user", "assistant", "system"}:
                role_clean = "user"
            content_clean = str(content or "").strip()
            if not content_clean:
                return
            content_clean = content_clean[:12000]

            def _write() -> None:
                self._conn.execute(
                    """
                    INSERT INTO conversation_turns
                        (client_id, conversation_id, role, content, created_at)
                    VALUES (?, ?, ?, ?, ?)
                    """,
                    (
                        str(client_id or "global")[:120],
                        str(conversation_id or "")[:220],
                        role_clean,
                        content_clean,
                        ts,
                    ),
                )
                self._conn.commit()

            self._execute_with_retry(_write)

    def load_conversation_turns(
        self,
        client_id: str,
        conversation_id: str,
        limit: int = 80,
    ) -> list[dict]:
        if not self._lock.acquire(timeout=_DB_API_LOCK_TIMEOUT):
            return []
        try:
            if not self._conn:
                return []
            safe_limit = max(1, min(400, int(limit)))
            cid = str(client_id or "global")[:120]
            conv = str(conversation_id or "")[:220]
            if not conv:
                return []

            def _read():
                cursor = self._conn.execute(
                    """
                    SELECT role, content, created_at
                    FROM conversation_turns
                    WHERE client_id = ? AND conversation_id = ?
                    ORDER BY created_at DESC, id DESC
                    LIMIT ?
                    """,
                    (cid, conv, safe_limit),
                )
                return cursor.fetchall()

            rows = self._execute_with_retry(_read) or []
            # Возвращаем в хронологическом порядке.
            rows = list(reversed(rows))
            result: list[dict] = []
            for role, content, created_at in rows:
                result.append(
                    {
                        "role": str(role or "user"),
                        "content": str(content or ""),
                        "created_at": float(created_at or 0.0),
                    }
                )
            return result
        finally:
            self._lock.release()

    def delete_conversation(self, client_id: str, conversation_id: str) -> int:
        if not self._lock.acquire(timeout=_DB_API_LOCK_TIMEOUT):
            return 0
        try:
            if not self._conn:
                return 0
            cid = str(client_id or "global")[:120]
            conv = str(conversation_id or "")[:220]
            if not conv:
                return 0

            def _write() -> int:
                cur = self._conn.execute(
                    """
                    DELETE FROM conversation_turns
                    WHERE client_id = ? AND conversation_id = ?
                    """,
                    (cid, conv),
                )
                self._conn.commit()
                return int(cur.rowcount or 0)

            return int(self._execute_with_retry(_write) or 0)
        finally:
            self._lock.release()

    # ------------------------------------------------------------------
    # Статистика
    # ------------------------------------------------------------------

    def stats(self) -> dict:
        """Получить статистику базы."""
        with self._lock:
            if not self._conn:
                return {"enabled": False}

            def _read_counts():
                patterns_count = self._conn.execute(
                    "SELECT COUNT(*) FROM patterns"
                ).fetchone()[0]
                edges_count = self._conn.execute(
                    "SELECT COUNT(*) FROM edges"
                ).fetchone()[0]
                return patterns_count, edges_count

            patterns_count, edges_count = self._execute_with_retry(_read_counts)
            db_size = self.db_path.stat().st_size if self.db_path.exists() else 0
            return {
                "enabled": True,
                "db_path": str(self.db_path),
                "patterns_count": patterns_count,
                "edges_count": edges_count,
                "db_size_bytes": db_size,
                "db_size_mb": round(db_size / (1024 * 1024), 2),
            }


# ---------------------------------------------------------------------------
# Singleton
# ---------------------------------------------------------------------------

_db_instance: Optional[KolibriDB] = None
_db_instance_lock = threading.Lock()


def get_db() -> KolibriDB:
    """Получить singleton экземпляр базы данных."""
    global _db_instance
    with _db_instance_lock:
        if _db_instance is None:
            _db_instance = KolibriDB()
            _db_instance.connect()
    return _db_instance
