from __future__ import annotations

from celery import Celery

celery = Celery(
    "content_factory2",
    broker="redis://redis:6379/0",
    backend="redis://redis:6379/1",
)

celery.conf.update(task_default_queue="content_factory2")


@celery.task(name="tasks.ping")
def ping() -> str:
    return "pong"
