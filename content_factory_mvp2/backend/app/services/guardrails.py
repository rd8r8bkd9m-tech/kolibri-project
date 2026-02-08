from __future__ import annotations

import re
from typing import Iterable

INJECTION_PATTERNS = [
    r"ignore\s+previous",
    r"system\s+prompt",
    r"act\s+as\s+system",
    r"jailbreak",
]

NUMERIC_CLAIM = re.compile(r"\b\d+(?:[\.,]\d+)?%?\b")


def sanitize_input(text: str) -> str:
    cleaned = text
    for pattern in INJECTION_PATTERNS:
        cleaned = re.sub(pattern, "[redacted]", cleaned, flags=re.IGNORECASE)
    return cleaned


def score_risk(text: str, banned: Iterable[str], forbidden: Iterable[str], required: Iterable[str]) -> tuple[int, list[str]]:
    risk = 0
    flags: list[str] = []
    lowered = text.lower()

    for phrase in banned:
        if phrase and phrase.lower() in lowered:
            risk += 20
            flags.append(f"banned_phrase:{phrase}")

    for claim in forbidden:
        if claim and claim.lower() in lowered:
            risk += 30
            flags.append(f"forbidden_claim:{claim}")

    if NUMERIC_CLAIM.search(text):
        risk += 10
        flags.append("numeric_claim_without_source")

    for disclaimer in required:
        if disclaimer and disclaimer.lower() not in lowered:
            risk += 5
            flags.append(f"missing_disclaimer:{disclaimer}")

    return risk, flags
