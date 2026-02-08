from __future__ import annotations

import random
from dataclasses import dataclass


@dataclass
class Metrics:
    views: int
    watch_time: float
    retention_proxy: float
    likes: int
    comments: int
    shares: int
    ctr_proxy: float
    site_clicks: int


@dataclass
class Attribution:
    leads: int
    revenue: float
    margin: float
    cost: float
    romi: float
    roas: float


def collect_metrics() -> Metrics:
    views = random.randint(500, 50000)
    return Metrics(
        views=views,
        watch_time=round(views * random.uniform(0.3, 0.8), 2),
        retention_proxy=round(random.uniform(25.0, 65.0), 2),
        likes=random.randint(10, 1000),
        comments=random.randint(2, 120),
        shares=random.randint(1, 200),
        ctr_proxy=round(random.uniform(0.8, 6.0), 2),
        site_clicks=random.randint(0, 500),
    )


def calc_romi(leads: int, revenue: float, margin: float, cost: float) -> Attribution:
    safe_cost = cost if cost else 1.0
    romi = round(((margin - cost) / safe_cost) * 100, 2)
    roas = round(revenue / safe_cost, 2)
    return Attribution(
        leads=leads,
        revenue=revenue,
        margin=margin,
        cost=cost,
        romi=romi,
        roas=roas,
    )
