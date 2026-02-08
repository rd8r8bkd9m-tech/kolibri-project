from __future__ import annotations

import datetime as dt
from dataclasses import dataclass


@dataclass
class PublishResult:
    platform_post_id: str
    published_at: dt.datetime
    url: str


def publish_stub(content_id: str) -> PublishResult:
    now = dt.datetime.utcnow()
    return PublishResult(
        platform_post_id=f"yt_short_{content_id[:8]}",
        published_at=now,
        url=f"https://youtube.com/shorts/{content_id}",
    )
