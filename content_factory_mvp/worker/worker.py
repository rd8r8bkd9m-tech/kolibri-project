from __future__ import annotations

from celery import Celery

celery = Celery(
    "content_factory",
    broker="redis://redis:6379/0",
    backend="redis://redis:6379/1",
)

celery.conf.update(
    task_routes={
        "tasks.*": {"queue": "content_factory"},
    },
    task_default_queue="content_factory",
)


@celery.task(name="tasks.ping")
def ping() -> str:
    return "pong"
