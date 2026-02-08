from __future__ import annotations

import random
from dataclasses import dataclass


@dataclass
class TrendInsight:
    title: str
    score: float
    rationale: str


def analyze_trends(niche: str, limit: int = 5) -> list[TrendInsight]:
    insights: list[TrendInsight] = []
    for index in range(limit):
        score = round(random.uniform(60, 95), 2)
        insights.append(
            TrendInsight(
                title=f"{niche} — тренд {index + 1}",
                score=score,
                rationale="Рост запросов и стабильная вовлеченность в комментариях.",
            )
        )
    return insights
