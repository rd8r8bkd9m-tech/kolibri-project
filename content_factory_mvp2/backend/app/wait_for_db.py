from __future__ import annotations

import os
import time
import psycopg2

DATABASE_URL = os.getenv(
    "DATABASE_URL",
    "postgresql+psycopg2://content_factory:content_factory@db:5432/content_factory",
)


if DATABASE_URL.startswith("postgresql+psycopg2://"):
    DATABASE_URL = DATABASE_URL.replace("postgresql+psycopg2://", "postgresql://", 1)


def wait_for_db(retries: int = 30, delay: float = 1.0) -> None:
    for _ in range(retries):
        try:
            conn = psycopg2.connect(DATABASE_URL)
            conn.close()
            return
        except Exception:
            time.sleep(delay)
    raise SystemExit("DB not ready")


if __name__ == "__main__":
    wait_for_db()
