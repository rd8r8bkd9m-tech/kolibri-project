"""
Kolibri Rate Limiter — защита API от перегрузок.

Простой in-memory rate limiter на базе token bucket.

Настройка через ENV:
  KOLIBRI_RATE_LIMIT=100       — запросов в минуту по умолчанию
  KOLIBRI_RATE_LIMIT_BURST=20  — максимальный burst
  KOLIBRI_RATE_LIMIT_ENABLED=1 — включить (по умолчанию включён)
"""
from __future__ import annotations

import logging
import os
import time
from collections import defaultdict
from typing import Optional

from fastapi import HTTPException, Request, status
from starlette.middleware.base import BaseHTTPMiddleware, RequestResponseEndpoint
from starlette.responses import Response

log = logging.getLogger("kolibri.ratelimit")

_RATE_LIMIT = int(os.getenv("KOLIBRI_RATE_LIMIT", "100"))  # req/min
_BURST = int(os.getenv("KOLIBRI_RATE_LIMIT_BURST", "20"))
_ENABLED = os.getenv("KOLIBRI_RATE_LIMIT_ENABLED", "1").strip() != "0"

# Пути, освобождённые от rate limit
_EXEMPT_PATHS = {
    "/api/health",
    "/api/knowledge/healthz",
    "/api/v1/auth/login",
    "/api/v1/health/live",
    "/api/v1/health/ready",
}


class TokenBucket:
    """Token bucket rate limiter для одного клиента."""

    __slots__ = ("capacity", "tokens", "refill_rate", "last_refill")

    def __init__(self, capacity: int, refill_per_second: float) -> None:
        self.capacity = capacity
        self.tokens = float(capacity)
        self.refill_rate = refill_per_second
        self.last_refill = time.monotonic()

    def consume(self) -> bool:
        """Попытка потребить 1 токен. Возвращает True если разрешено."""
        now = time.monotonic()
        elapsed = now - self.last_refill
        self.tokens = min(self.capacity, self.tokens + elapsed * self.refill_rate)
        self.last_refill = now

        if self.tokens >= 1.0:
            self.tokens -= 1.0
            return True
        return False

    @property
    def retry_after(self) -> float:
        """Секунд до появления следующего токена."""
        if self.tokens >= 1.0:
            return 0.0
        return (1.0 - self.tokens) / self.refill_rate


class RateLimitMiddleware(BaseHTTPMiddleware):
    """FastAPI middleware для rate limiting по IP."""

    def __init__(self, app: object) -> None:
        super().__init__(app)  # type: ignore[arg-type]
        self._buckets: dict[str, TokenBucket] = defaultdict(
            lambda: TokenBucket(
                capacity=_BURST,
                refill_per_second=_RATE_LIMIT / 60.0,
            )
        )
        # Периодическая очистка неактивных клиентов
        self._last_cleanup = time.monotonic()

    def _get_client_ip(self, request: Request) -> str:
        """Получить IP клиента (с учётом X-Forwarded-For)."""
        forwarded = request.headers.get("X-Forwarded-For")
        if forwarded:
            return forwarded.split(",")[0].strip()
        return request.client.host if request.client else "unknown"

    async def dispatch(
        self, request: Request, call_next: RequestResponseEndpoint,
    ) -> Response:
        if not _ENABLED:
            return await call_next(request)

        path = request.url.path
        if path in _EXEMPT_PATHS:
            return await call_next(request)

        client_ip = self._get_client_ip(request)
        bucket = self._buckets[client_ip]

        if not bucket.consume():
            retry = int(bucket.retry_after) + 1
            log.warning("Rate limit exceeded: %s (%s)", client_ip, path)
            return Response(
                content=f'{{"detail":"Слишком много запросов. Повторите через {retry}с"}}',
                status_code=status.HTTP_429_TOO_MANY_REQUESTS,
                headers={
                    "Retry-After": str(retry),
                    "Content-Type": "application/json",
                },
            )

        # Очистка старых бакетов каждые 5 минут
        now = time.monotonic()
        if now - self._last_cleanup > 300:
            self._cleanup_old_buckets(now)
            self._last_cleanup = now

        response = await call_next(request)
        # Добавляем заголовки rate limit
        response.headers["X-RateLimit-Limit"] = str(_RATE_LIMIT)
        response.headers["X-RateLimit-Remaining"] = str(int(bucket.tokens))
        return response

    def _cleanup_old_buckets(self, now: float) -> None:
        """Удалить бакеты неактивных клиентов."""
        stale = [
            ip for ip, bucket in self._buckets.items()
            if now - bucket.last_refill > 600
        ]
        for ip in stale:
            del self._buckets[ip]
        if stale:
            log.debug("Очищено %d неактивных rate-limit бакетов", len(stale))
