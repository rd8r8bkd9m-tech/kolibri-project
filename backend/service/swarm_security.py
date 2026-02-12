"""
swarm_security.py — защита для P2P/роевых эндпоинтов.

Эндпоинты синхронизации знаний (swarm/sync, sync/apply и т.п.) потенциально
могут менять модель. Поэтому даже если общий auth отключён (KOLIBRI_AUTH_ENABLED=0),
роевые операции должны быть защищены отдельным shared-secret.

Использование:
  - В ENV задать KOLIBRI_SWARM_TOKEN=<секрет>
  - Клиенты передают заголовок: X-Kolibri-Swarm-Token: <секрет>
"""

from __future__ import annotations

import hmac
import os

from fastapi import HTTPException, Request, status


_SWARM_TOKEN = os.getenv("KOLIBRI_SWARM_TOKEN", "").strip()


def require_swarm_token(request: Request) -> None:
    """Dependency: требует корректный X-Kolibri-Swarm-Token."""
    if not _SWARM_TOKEN:
        # Без токена рой/синхронизация выключены, чтобы не открывать write-эндпоинты в интернет.
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="Swarm sync is disabled: KOLIBRI_SWARM_TOKEN is not configured",
        )

    got = request.headers.get("X-Kolibri-Swarm-Token", "").strip()
    if not got or not hmac.compare_digest(got, _SWARM_TOKEN):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid swarm token",
        )

