from __future__ import annotations

CONTENT_FLOW = [
    "analysis",
    "idea_approval",
    "package_build",
    "qa_approval",
    "scheduling",
    "publishing",
    "analytics",
    "done",
]


def next_state(current: str) -> str:
    if current not in CONTENT_FLOW:
        return current
    idx = CONTENT_FLOW.index(current)
    return CONTENT_FLOW[min(idx + 1, len(CONTENT_FLOW) - 1)]
