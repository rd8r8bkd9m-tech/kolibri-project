#!/usr/bin/env python3
"""Builds spectral fingerprints for Kolibri knowledge documents."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Iterable

TEXT_EXT = {".md", ".txt", ".ks", ".json", ".c", ".h", ".py"}

def calc_entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = [0] * 256
    for b in data:
        counts[b] += 1
    entropy = 0.0
    total = len(data)
    for c in counts:
        if not c:
            continue
        p = c / total
        entropy -= p * math.log2(p)
    return round(entropy, 5)

def classify(path: Path, data: bytes) -> str:
    suffix = path.suffix.lower()
    if suffix in {".c", ".h", ".cpp", ".hpp", ".py"}:
        return "code"
    if suffix in {".md", ".txt"}:
        return "docs"
    if suffix in {".ks", ".ksd"}:
        return "kolibriscript"
    if suffix in {".json", ".yaml"}:
        return "data"
    if len(data) > 0 and data[:4] == b"\x89PNG":
        return "image"
    return "binary"

def iter_files(roots: Iterable[Path]) -> Iterable[Path]:
    for root in roots:
        if root.is_file():
            yield root
        elif root.is_dir():
            for path in root.rglob("*"):
                if path.is_file():
                    yield path

def fingerprint(paths: Iterable[Path]) -> list[dict]:
    results = []
    for path in iter_files(paths):
        data = path.read_bytes()
        if path.suffix.lower() not in TEXT_EXT and len(data) > 2 * 1024 * 1024:
            continue
        sha = hashlib.sha256(data).hexdigest()
        entropy = calc_entropy(data)
        profile = {
            "path": str(path),
            "sha256": sha,
            "entropy": entropy,
            "bytes": len(data),
            "class": classify(path, data),
        }
        results.append(profile)
    return results

def main() -> None:
    parser = argparse.ArgumentParser(description="Kolibri spectral fingerprint pipeline")
    parser.add_argument("roots", nargs="+", help="Directories/files to scan")
    parser.add_argument("--output", required=True, help="Destination JSON file")
    args = parser.parse_args()

    root_paths = [Path(r).resolve() for r in args.roots]
    profiles = fingerprint(root_paths)

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as fh:
        json.dump({"profiles": profiles}, fh, ensure_ascii=False, indent=2)

if __name__ == "__main__":
    main()
