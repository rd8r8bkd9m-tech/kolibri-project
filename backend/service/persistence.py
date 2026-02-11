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
import time
from pathlib import Path
from typing import Optional

log = logging.getLogger("kolibri.persistence")

_PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
_DEFAULT_DB_PATH = _PROJECT_ROOT / "data" / "kolibri.db"
_DB_PATH = Path(os.getenv("KOLIBRI_DB_PATH", str(_DEFAULT_DB_PATH)))
_PERSISTENCE_ENABLED = os.getenv("KOLIBRI_PERSISTENCE", "1").strip() != "0"

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

CREATE INDEX IF NOT EXISTS idx_edges_source ON edges(source_hash);
CREATE INDEX IF NOT EXISTS idx_edges_target ON edges(target_hash);
CREATE INDEX IF NOT EXISTS idx_patterns_word ON patterns(word);
"""


class KolibriDB:
    """Persistent storage для графа знаний."""

    def __init__(self, db_path: Optional[Path] = None) -> None:
        self.db_path = db_path or _DB_PATH
        self._conn: Optional[sqlite3.Connection] = None

    def is_enabled(self) -> bool:
        return _PERSISTENCE_ENABLED

    def connect(self) -> None:
        """Установить соединение и создать таблицы."""
        if not _PERSISTENCE_ENABLED:
            return
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self._conn = sqlite3.connect(
            str(self.db_path),
            check_same_thread=False,
            timeout=10.0,
        )
        self._conn.execute("PRAGMA journal_mode=WAL")
        self._conn.execute("PRAGMA synchronous=NORMAL")
        self._conn.execute("PRAGMA cache_size=-64000")  # 64 MB cache
        self._conn.executescript(_SCHEMA)
        self._conn.commit()
        log.info("SQLite подключён: %s", self.db_path)

    def close(self) -> None:
        if self._conn:
            self._conn.close()
            self._conn = None

    # ------------------------------------------------------------------
    # Паттерны
    # ------------------------------------------------------------------

    def save_patterns(
        self,
        patterns: dict[int, object],
        hash_to_word: dict[int, str],
    ) -> int:
        """Сохранить паттерны в базу (batch upsert)."""
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
        self._conn.executemany(
            """INSERT OR REPLACE INTO patterns
               (hash, word, pattern, fitness, frequency, version, updated_at)
               VALUES (?, ?, ?, ?, ?, ?, ?)""",
            rows,
        )
        self._conn.commit()
        log.info("Сохранено %d паттернов за %.1fms", len(rows), (time.time() - t0) * 1000)
        return len(rows)

    def load_patterns(self) -> list[dict]:
        """Загрузить все паттерны из базы."""
        if not self._conn:
            return []
        cursor = self._conn.execute(
            "SELECT hash, word, pattern, fitness, frequency FROM patterns"
        )
        result = []
        for row in cursor:
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
        if not self._conn:
            return 0
        t0 = time.time()
        rows = []
        for (src, tgt), edge in edges.items():
            weight = getattr(edge, "weight", 1.0)
            cooc = getattr(edge, "cooccurrence", 1)
            rows.append((src, tgt, weight, cooc, 1))
        self._conn.executemany(
            """INSERT OR REPLACE INTO edges
               (source_hash, target_hash, weight, cooccurrence, version)
               VALUES (?, ?, ?, ?, ?)""",
            rows,
        )
        self._conn.commit()
        log.info("Сохранено %d рёбер за %.1fms", len(rows), (time.time() - t0) * 1000)
        return len(rows)

    def load_edges(self) -> list[dict]:
        """Загрузить все рёбра."""
        if not self._conn:
            return []
        cursor = self._conn.execute(
            "SELECT source_hash, target_hash, weight, cooccurrence FROM edges"
        )
        return [
            {
                "source_hash": row[0],
                "target_hash": row[1],
                "weight": row[2],
                "cooccurrence": row[3],
            }
            for row in cursor
        ]

    # ------------------------------------------------------------------
    # Метаданные
    # ------------------------------------------------------------------

    def set_meta(self, key: str, value: str) -> None:
        if not self._conn:
            return
        self._conn.execute(
            "INSERT OR REPLACE INTO metadata (key, value) VALUES (?, ?)",
            (key, value),
        )
        self._conn.commit()

    def get_meta(self, key: str, default: str = "") -> str:
        if not self._conn:
            return default
        cursor = self._conn.execute(
            "SELECT value FROM metadata WHERE key = ?", (key,)
        )
        row = cursor.fetchone()
        return row[0] if row else default

    # ------------------------------------------------------------------
    # Статистика
    # ------------------------------------------------------------------

    def stats(self) -> dict:
        """Получить статистику базы."""
        if not self._conn:
            return {"enabled": False}
        patterns_count = self._conn.execute(
            "SELECT COUNT(*) FROM patterns"
        ).fetchone()[0]
        edges_count = self._conn.execute(
            "SELECT COUNT(*) FROM edges"
        ).fetchone()[0]
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


def get_db() -> KolibriDB:
    """Получить singleton экземпляр базы данных."""
    global _db_instance
    if _db_instance is None:
        _db_instance = KolibriDB()
        _db_instance.connect()
    return _db_instance
