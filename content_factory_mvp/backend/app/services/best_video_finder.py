from __future__ import annotations

import random
from dataclasses import dataclass


@dataclass
class VideoCandidate:
    title: str
    url: str
    channel: str
    views: int
    engagement_rate: float
    retention_proxy: float
    query_match: float
    sentiment: float
    recency_days: int


@dataclass
class ScoredVideo:
    title: str
    url: str
    channel: str
    views: int
    engagement_rate: float
    score: float
    rationale: dict[str, float]


WEIGHTS = {
    "view_velocity": 0.25,
    "engagement_rate": 0.20,
    "retention_proxy": 0.20,
    "query_match": 0.15,
    "sentiment": 0.10,
    "recency": 0.10,
}


def _score_candidate(candidate: VideoCandidate) -> tuple[float, dict[str, float]]:
    view_velocity = min(candidate.views / 100_000, 1.0)
    engagement = min(candidate.engagement_rate / 15.0, 1.0)
    retention = min(candidate.retention_proxy / 100.0, 1.0)
    query_match = candidate.query_match
    sentiment = (candidate.sentiment + 1) / 2
    recency = max(0.0, 1 - candidate.recency_days / 30)

    contributions = {
        "view_velocity": view_velocity * WEIGHTS["view_velocity"],
        "engagement_rate": engagement * WEIGHTS["engagement_rate"],
        "retention_proxy": retention * WEIGHTS["retention_proxy"],
        "query_match": query_match * WEIGHTS["query_match"],
        "sentiment": sentiment * WEIGHTS["sentiment"],
        "recency": recency * WEIGHTS["recency"],
    }
    score = round(sum(contributions.values()) * 100, 2)
    return score, contributions


def find_best_videos(niche: str, limit: int = 5) -> list[ScoredVideo]:
    candidates: list[VideoCandidate] = []
    for index in range(limit * 2):
        candidates.append(
            VideoCandidate(
                title=f"{niche} — эталон #{index + 1}",
                url=f"https://youtube.com/watch?v=mock-{index + 1}",
                channel=f"Channel {index + 1}",
                views=random.randint(50_000, 2_000_000),
                engagement_rate=round(random.uniform(2.0, 12.0), 2),
                retention_proxy=round(random.uniform(30.0, 75.0), 2),
                query_match=round(random.uniform(0.6, 1.0), 2),
                sentiment=round(random.uniform(-0.2, 0.8), 2),
                recency_days=random.randint(1, 25),
            )
        )

    scored: list[ScoredVideo] = []
    for candidate in candidates:
        score, contributions = _score_candidate(candidate)
        scored.append(
            ScoredVideo(
                title=candidate.title,
                url=candidate.url,
                channel=candidate.channel,
                views=candidate.views,
                engagement_rate=candidate.engagement_rate,
                score=score,
                rationale=contributions,
            )
        )

    scored.sort(key=lambda item: item.score, reverse=True)
    return scored[:limit]
