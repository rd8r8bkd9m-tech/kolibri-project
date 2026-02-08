#!/usr/bin/env python3
"""CLI-tool that delegates to the C knowledge indexer."""

from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def run() -> None:
    parser = argparse.ArgumentParser(description="Kolibri knowledge ingestion helper")
    parser.add_argument("query", nargs="?", default="", help="Search query to execute")
    parser.add_argument("--limit", type=int, default=5, help="Number of snippets to return")
    parser.add_argument(
        "--export",
        type=Path,
        help="Path to write JSON snapshot of the knowledge index",
    )

    args = parser.parse_args()

    roots = [str(project_root() / "docs"), str(project_root() / "data")]
    indexer_path = project_root() / "build" / "kolibri_indexer"

    if args.export:
        with tempfile.TemporaryDirectory(prefix="kolibri_index_") as tmpdir:
            subprocess.run(
                [str(indexer_path), "build", "--output", tmpdir, *roots],
                check=True,
            )
            payload = Path(tmpdir) / "index.json"
            if payload.exists():
                args.export.write_text(payload.read_text(encoding="utf-8"), encoding="utf-8")
                print(f"Exported index snapshot to {args.export}")

    query = args.query.strip()
    if not query:
        return

    completed = subprocess.run(
        [
            str(indexer_path),
            "search",
            "--query",
            query,
            "--limit",
            str(args.limit),
            *roots,
        ],
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        print(completed.stderr.strip() or "Поиск не выполнен")
        return

    lines = [line for line in completed.stdout.splitlines() if line.strip()]
    if not lines:
        print("Знания не найдены.")
        return

    for line in lines:
        parts = line.split("\t", 2)
        if len(parts) == 3:
            score, doc_id, title = parts
            print(f"[{float(score):.4f}] {title} ({doc_id})")
        else:
            print(line)


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    run()
