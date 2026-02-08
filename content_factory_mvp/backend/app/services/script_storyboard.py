from __future__ import annotations

from dataclasses import dataclass


@dataclass
class ScriptBundle:
    script: str
    storyboard_json: dict


def generate_script(title: str, hook: str, angle: str, cta: str) -> ScriptBundle:
    script = (
        f"HOOK: {hook}\n"
        f"ANGLE: {angle}\n"
        "СЦЕНА 1: проблема, 0-3 сек\n"
        "СЦЕНА 2: решение, 3-20 сек\n"
        "СЦЕНА 3: доказательство, 20-45 сек\n"
        f"CTA: {cta}\n"
    )
    storyboard_json = {
        "scenes": [
            {"time": "0-3", "visual": "Титр проблемы"},
            {"time": "3-20", "visual": "Шаги решения"},
            {"time": "20-45", "visual": "Кейс/доказательство"},
        ]
    }
    return ScriptBundle(script=script, storyboard_json=storyboard_json)
