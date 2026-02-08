from __future__ import annotations

import datetime as dt
from dataclasses import dataclass


@dataclass
class PublishResult:
    platform_post_id: str
    url: str
    published_at: dt.datetime


def publish_wordpress_stub(title: str, content: str) -> PublishResult:
    now = dt.datetime.utcnow()
    return PublishResult(
        platform_post_id=f"wp_{int(now.timestamp())}",
        url=f"https://example.com/demo/{int(now.timestamp())}",
        published_at=now,
    )


def publish_youtube_stub() -> PublishResult:
    now = dt.datetime.utcnow()
    return PublishResult(
        platform_post_id=f"yt_{int(now.timestamp())}",
        url=f"https://youtube.com/shorts/{int(now.timestamp())}",
        published_at=now,
    )
