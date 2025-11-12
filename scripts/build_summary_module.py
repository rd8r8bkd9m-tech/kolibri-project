#!/usr/bin/env python3
"""Convert KolibriSim JSONL traces into a Markdown knowledge module."""
from __future__ import annotations

import argparse
import json
import re
from collections import defaultdict
from pathlib import Path

CATEGORIES = [
    ("python", "Python Ecosystem"),
    ("license", "Licensing and Freedom"),
    ("network", "Networking"),
    ("security", "Security"),
    ("spec", "Specifications and Standards"),
]


def normalise(text: str) -> str:
    text = text.strip()
    text = re.sub(r"\s+", " ", text)
    return text


def classify(message: str) -> str:
    lower = message.lower()
    for keyword, title in CATEGORIES:
        if keyword in lower:
            return title
    return "General Insights"


def summarize(messages: list[str]) -> str:
    if not messages:
        return ""
    seen = set()
    summary = []
    for msg in messages:
        msg_clean = normalise(msg)
        if not msg_clean or msg_clean in seen:
            continue
        seen.add(msg_clean)
        summary.append(f"- {msg_clean}")
        if len(summary) >= 25:
            break
    return "\n".join(summary)


def build_module(input_path: Path, output_path: Path) -> int:
    if not input_path.exists():
        raise FileNotFoundError(f"trace file not found: {input_path}")

    categories: dict[str, list[str]] = defaultdict(list)

    with input_path.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                payload = json.loads(line)
            except json.JSONDecodeError:
                continue
            message = str(payload.get("soobshenie") or "")
            if not message:
                continue
            category = classify(message)
            categories[category].append(message)

    if not categories:
        raise RuntimeError("trace file did not contain usable messages")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as out:
        out.write("# Kolibri Generated Knowledge Module\n\n")
        out.write(f"_Source trace: {input_path.as_posix()}_\n\n")
        for category in sorted(categories.keys()):
            out.write(f"## {category}\n\n")
            out.write(f"{summarize(categories[category]) or '- (no entries)'}\n\n")

    return len(categories)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Markdown module from Kolibri JSONL trace")
    parser.add_argument("--input", type=Path, default=Path("build/training/auto_trace.jsonl"), help="Path to JSONL trace")
    parser.add_argument("--output", type=Path, default=Path("docs/generated/kolibri_summary_module.md"), help="Output Markdown file")
    args = parser.parse_args()

    try:
        sections = build_module(args.input, args.output)
    except Exception as exc:  # pragma: no cover
        print(f"[build-summary-module] error: {exc}")
        return 1

    print(f"[build-summary-module] wrote {sections} sections to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
