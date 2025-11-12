#!/usr/bin/env python3
"""Kolibri sentinel: verify genome integrity and promote approved copies."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path


def read_last_hash(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8").strip()


def write_last_hash(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(value, encoding="utf-8")


def compute_hash(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def run_health(node_binary: Path, genome_path: Path, hmac_inline: str | None, hmac_file: Path | None) -> dict[str, object]:
    cmd = [str(node_binary), "--health", "--genome", str(genome_path)]
    if hmac_inline:
        cmd.extend(["--hmac-key", hmac_inline])
    elif hmac_file:
        cmd.extend(["--hmac-key", f"@{hmac_file}"])
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"kolibri_node --health failed: {proc.stderr.strip()}")
    output = proc.stdout.strip()
    json_start = output.find("{")
    if json_start == -1:
        raise RuntimeError(f"unexpected health output: {output}")
    payload = output[json_start:]
    return json.loads(payload)


def promote(pending: Path, approved_target: Path, node_binary: Path, hmac_inline: str | None, hmac_file: Path | None) -> str:
    if not pending.exists():
        raise FileNotFoundError(f"pending genome not found: {pending}")
    report = run_health(node_binary, pending, hmac_inline, hmac_file)
    if report.get("status") != "ok":
        raise RuntimeError(f"genome health check failed: {report}")
    digest = compute_hash(pending)
    approved_target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(pending, approved_target)
    return digest


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Kolibri sentinel guard")
    parser.add_argument("--pending", type=Path, default=Path("build/knowledge/knowledge_genome.dat"))
    parser.add_argument("--approved", type=Path, default=Path("build/knowledge/approved/knowledge_genome_approved.dat"))
    parser.add_argument("--node-binary", type=Path, default=Path("build/kolibri_node"))
    parser.add_argument("--hash-record", type=Path, default=Path("build/knowledge/approved/knowledge_genome_approved.hash"))
    parser.add_argument("--hmac-inline", type=str, default="kolibri-knowledge")
    parser.add_argument("--hmac-file", type=Path, default=None)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    last_hash = read_last_hash(args.hash_record)
    current_hash = compute_hash(args.pending)
    if current_hash == last_hash and args.approved.exists():
        print(f"[sentinel] genome unchanged ({current_hash[:8]}...), skipping promotion")
        return 0
    digest = promote(args.pending, args.approved, args.node_binary, args.hmac_inline, args.hmac_file)
    write_last_hash(args.hash_record, digest)
    print(f"[sentinel] promoted genome ({digest[:8]}...) to {args.approved}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
