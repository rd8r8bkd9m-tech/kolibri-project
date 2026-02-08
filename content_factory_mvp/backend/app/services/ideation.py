from __future__ import annotations

from dataclasses import dataclass


@dataclass
class IdeaDraft:
    title: str
    hook: str
    angle: str
    cta: str
    format: str
    funnel_stage: str


def generate_ideas(niche: str, limit: int = 10) -> list[IdeaDraft]:
    ideas: list[IdeaDraft] = []
    for index in range(limit):
        ideas.append(
            IdeaDraft(
                title=f"{niche}: идея #{index + 1}",
                hook="Проблема за 3 секунды: почему вы теряете X",
                angle="Короткий разбор + быстрый результат",
                cta="Сохраните и подпишитесь за чек‑листом",
                format="Problem→Solution" if index % 2 == 0 else "Case/Proof",
                funnel_stage="TOFU" if index % 3 == 0 else "MOFU",
            )
        )
    return ideas
