"""
Тесты для delta_sync — дельта-синхронизация нод.
"""
from __future__ import annotations

import pytest

from backend.service.delta_sync import (
    PatternDelta,
    EdgeDelta,
)


class TestPatternDelta:
    def test_creation(self) -> None:
        pd = PatternDelta(
            word="тест",
            hash=12345,
            pattern=[1, 2, 3],
            fitness=0.5,
            frequency=10,
            version=1,
        )
        assert pd.word == "тест"
        assert pd.hash == 12345
        assert pd.fitness == 0.5
        assert pd.version == 1


class TestEdgeDelta:
    def test_creation(self) -> None:
        ed = EdgeDelta(
            source_hash=111,
            target_hash=222,
            weight=0.8,
            cooccurrence=5,
            version=1,
        )
        assert ed.source_hash == 111
        assert ed.target_hash == 222
        assert ed.weight == 0.8
        assert ed.version == 1
