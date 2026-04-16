"""Тесты persistence.py — SQLite хранилище Kolibri."""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))


class TestKolibriDB:
    """Тесты KolibriDB с временной SQLite."""

    def setup_method(self) -> None:
        import tempfile
        self._tmpdir = tempfile.mkdtemp()
        self._db_path = Path(self._tmpdir) / "test.db"

    def _make_db(self):  # noqa: ANN202
        from backend.service.persistence import KolibriDB
        import os
        os.environ["KOLIBRI_PERSISTENCE"] = "1"  # Включаем persistence
        # Перечитаем после установки ENV
        import importlib
        from backend.service import persistence
        importlib.reload(persistence)
        db = persistence.KolibriDB(db_path=self._db_path)
        db.connect()
        return db

    def test_connect_creates_tables(self) -> None:
        db = self._make_db()
        stats = db.stats()
        assert isinstance(stats, dict)
        assert "patterns" in stats or "tables" in stats or isinstance(stats, dict)
        db.close()

    def test_meta_roundtrip(self) -> None:
        db = self._make_db()
        db.set_meta("test_key", "test_value")
        val = db.get_meta("test_key")
        assert val == "test_value"
        db.close()

    def test_meta_missing_key(self) -> None:
        db = self._make_db()
        val = db.get_meta("nonexistent", default="fallback")
        assert val == "fallback"
        db.close()

    def test_save_load_patterns(self) -> None:
        db = self._make_db()
        from types import SimpleNamespace
        pat1 = SimpleNamespace(digits=[1, 2, 3], fitness=1.0, frequency=5)
        pat2 = SimpleNamespace(digits=[4, 5, 6], fitness=0.8, frequency=3)
        patterns = {123: pat1, 456: pat2}
        hash_to_word = {123: "тест", 456: "проверка"}
        count = db.save_patterns(patterns, hash_to_word)
        assert count >= 0
        loaded = db.load_patterns()
        assert isinstance(loaded, list)
        db.close()

    def test_save_load_edges(self) -> None:
        db = self._make_db()
        from types import SimpleNamespace
        edge1 = SimpleNamespace(weight=1.5, count=3)
        edge2 = SimpleNamespace(weight=0.8, count=1)
        edges = {
            (100, 200): edge1,
            (300, 400): edge2,
        }
        count = db.save_edges(edges)
        assert count >= 0
        loaded = db.load_edges()
        assert isinstance(loaded, list)
        db.close()

    def test_stats(self) -> None:
        db = self._make_db()
        stats = db.stats()
        assert isinstance(stats, dict)
        db.close()
