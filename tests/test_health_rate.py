"""Тесты для rate_limiter.py и health.py."""
from __future__ import annotations

import pytest

# ---------- TokenBucket ----------


def test_token_bucket_consume():
    from backend.service.rate_limiter import TokenBucket

    bucket = TokenBucket(capacity=3, refill_per_second=1.0)
    assert bucket.consume()
    assert bucket.consume()
    assert bucket.consume()
    # 4-й выжрал все токены
    assert not bucket.consume()


def test_token_bucket_retry_after():
    from backend.service.rate_limiter import TokenBucket

    bucket = TokenBucket(capacity=1, refill_per_second=1.0)
    bucket.consume()
    bucket.consume()  # over limit
    assert bucket.retry_after > 0.0


# ---------- Health helpers ----------


def test_health_check_corpus():
    from backend.service.health import _check_corpus

    result = _check_corpus()
    assert "files" in result
    assert "size_kb" in result
    assert result["status"] in ("ok", "missing")


def test_health_check_memory():
    from backend.service.health import _check_memory

    result = _check_memory()
    assert "rss_mb" in result
    assert result["rss_mb"] > 0


def test_health_check_disk():
    from backend.service.health import _check_disk

    result = _check_disk()
    assert "total_gb" in result
    assert result["total_gb"] > 0
