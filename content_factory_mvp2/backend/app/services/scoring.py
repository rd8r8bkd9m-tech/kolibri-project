from __future__ import annotations

from dataclasses import dataclass


@dataclass
class ScoringResult:
    score: float
    rationale: dict[str, float]


VIDEO_WEIGHTS = {
    "view_velocity": 0.25,
    "engagement_rate": 0.2,
    "retention_proxy": 0.2,
    "query_match": 0.15,
    "sentiment": 0.1,
    "recency": 0.1,
}

ARTICLE_WEIGHTS = {
    "estimated_traffic_proxy": 0.25,
    "backlink_proxy": 0.15,
    "readability": 0.2,
    "intent_match": 0.2,
    "recency": 0.2,
}


def score_video(
    view_velocity: float,
    engagement_rate: float,
    retention_proxy: float,
    query_match: float,
    sentiment: float,
    recency: float,
) -> ScoringResult:
    parts = {
        "view_velocity": view_velocity * VIDEO_WEIGHTS["view_velocity"],
        "engagement_rate": engagement_rate * VIDEO_WEIGHTS["engagement_rate"],
        "retention_proxy": retention_proxy * VIDEO_WEIGHTS["retention_proxy"],
        "query_match": query_match * VIDEO_WEIGHTS["query_match"],
        "sentiment": sentiment * VIDEO_WEIGHTS["sentiment"],
        "recency": recency * VIDEO_WEIGHTS["recency"],
    }
    score = round(sum(parts.values()) * 100, 2)
    return ScoringResult(score=score, rationale=parts)


def score_article(
    estimated_traffic_proxy: float,
    backlink_proxy: float,
    readability: float,
    intent_match: float,
    recency: float,
) -> ScoringResult:
    parts = {
        "estimated_traffic_proxy": estimated_traffic_proxy * ARTICLE_WEIGHTS["estimated_traffic_proxy"],
        "backlink_proxy": backlink_proxy * ARTICLE_WEIGHTS["backlink_proxy"],
        "readability": readability * ARTICLE_WEIGHTS["readability"],
        "intent_match": intent_match * ARTICLE_WEIGHTS["intent_match"],
        "recency": recency * ARTICLE_WEIGHTS["recency"],
    }
    score = round(sum(parts.values()) * 100, 2)
    return ScoringResult(score=score, rationale=parts)
