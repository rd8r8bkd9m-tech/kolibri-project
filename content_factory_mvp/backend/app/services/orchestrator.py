from __future__ import annotations

import datetime as dt

CONTENT_FLOW = [
    "analysis",
    "idea_approval",
    "production",
    "content_approval",
    "publishing",
    "analytics",
    "done",
]


class StateError(RuntimeError):
    pass


def next_state(current: str) -> str:
    if current not in CONTENT_FLOW:
        raise StateError(f"Unknown state: {current}")
    idx = CONTENT_FLOW.index(current)
    if idx == len(CONTENT_FLOW) - 1:
        return current
    return CONTENT_FLOW[idx + 1]


def can_advance(current: str, target: str) -> bool:
    if current not in CONTENT_FLOW or target not in CONTENT_FLOW:
        return False
    return CONTENT_FLOW.index(target) >= CONTENT_FLOW.index(current)


def stamp_updated() -> dt.datetime:
    return dt.datetime.utcnow()
