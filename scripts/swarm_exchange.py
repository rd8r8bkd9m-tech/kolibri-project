#!/usr/bin/env python3
"""Fan-out Kolibri knowledge between nodes according to swarm topology."""
from __future__ import annotations

import argparse
import json
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path


def load_topology(path: Path) -> dict[str, dict[str, object]]:
    if not path.exists():
        raise FileNotFoundError(f"topology file not found: {path}")
    with path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    nodes = payload.get("nodes")
    if not isinstance(nodes, list):
        raise ValueError("topology JSON must contain a list under 'nodes'")
    node_map: dict[str, dict[str, object]] = {}
    for entry in nodes:
        name = entry.get("name")
        if not isinstance(name, str) or not name:
            raise ValueError("each node requires a non-empty 'name'")
        if name in node_map:
            raise ValueError(f"duplicate node name: {name}")
        node_map[name] = entry
    return node_map


def _read_offset(path: Path) -> int:
    try:
        if not path.exists():
            return 0
        raw = path.read_text(encoding="utf-8").strip()
        return int(raw) if raw else 0
    except Exception:
        return 0


def relay_once(relay_bin: Path, source: dict[str, object], target: dict[str, object], offset: Path) -> dict:
    genome = Path(source.get("genome", ""))
    if not genome.exists():
        print(f"[swarm] skip {source['name']} -> {target['name']}: genome {genome} missing")
        return {
            "ts": datetime.now(timezone.utc).isoformat(),
            "source": source.get("name"),
            "target": target.get("name"),
            "ok": False,
            "reason": "missing_genome",
        }
    targets_dir = Path(target.get("receive_dir", ""))
    if not targets_dir.exists():
        targets_dir.mkdir(parents=True, exist_ok=True)
    cmd = [str(relay_bin), "--source", str(genome), "--targets-dir", str(targets_dir)]
    key_inline = target.get("hmac_key_inline")
    key_file = target.get("hmac_key_file")
    if key_inline:
        cmd.extend(["--target-key-inline", str(key_inline)])
    elif key_file:
        cmd.extend(["--target-key", str(key_file)])
    else:
        raise ValueError(f"target node {target['name']} missing hmac key (inline or file)")
    offset.parent.mkdir(parents=True, exist_ok=True)
    cmd.extend(["--offset", str(offset)])
    print(f"[swarm] {source['name']} -> {target['name']} ({targets_dir})")
    before = _read_offset(offset)
    t0 = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True)
    dt = time.perf_counter() - t0
    after = _read_offset(offset)
    delta = max(0, after - before)
    if proc.returncode != 0:
        print(proc.stderr.strip())
        return {
            "ts": datetime.now(timezone.utc).isoformat(),
            "source": source.get("name"),
            "target": target.get("name"),
            "ok": False,
            "duration_sec": dt,
            "bytes": delta,
            "stderr": proc.stderr.strip(),
        }
    print(proc.stdout.strip())
    return {
        "ts": datetime.now(timezone.utc).isoformat(),
        "source": source.get("name"),
        "target": target.get("name"),
        "ok": True,
        "duration_sec": dt,
        "bytes": delta,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Kolibri swarm knowledge exchange")
    parser.add_argument("--topology", type=Path, default=Path("swarm/nodes.json"), help="Path to swarm topology JSON")
    parser.add_argument("--relay-binary", type=Path, default=Path("build/kolibri_knowledge_relay"))
    parser.add_argument("--offset-dir", type=Path, default=Path("build/swarm/offsets"))
    parser.add_argument("--metrics-file", type=Path, default=Path("build/swarm/metrics.jsonl"))
    parser.add_argument("--analyze", action="store_true", help="Analyze metrics and print forecasts after exchange")
    parser.add_argument("--analyze-only", action="store_true", help="Only analyze metrics; skip exchange step")
    return parser.parse_args()


def _append_metrics(path: Path, records: list[dict]) -> None:
    if not records:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as fh:
        for rec in records:
            fh.write(json.dumps(rec, ensure_ascii=False) + "\n")


def _analyze_metrics(path: Path) -> None:
    if not path.exists():
        print(f"[swarm] no metrics to analyze at {path}")
        return
    per_edge: dict[tuple[str, str], list[dict]] = {}
    with path.open("r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except Exception:
                continue
            src = rec.get("source") or "?"
            dst = rec.get("target") or "?"
            per_edge.setdefault((src, dst), []).append(rec)

    def ewma(values: list[float], alpha: float = 0.5) -> float:
        if not values:
            return 0.0
        s = values[0]
        for v in values[1:]:
            s = alpha * v + (1 - alpha) * s
        return s

    print("[swarm] analysis summary (pattern detection + simple forecast)")
    highlights = []
    for (src, dst), recs in per_edge.items():
        ok = [r for r in recs if r.get("ok")]
        bytes_list = [float(r.get("bytes", 0)) for r in ok]
        dur_list = [float(r.get("duration_sec", 0.0)) for r in ok if r.get("duration_sec")]
        total_b = int(sum(bytes_list))
        total_n = len(recs)
        succ = len(ok)
        sr = (succ / total_n) if total_n else 0.0
        throughput = (sum(b/d if d else 0 for b, d in zip(bytes_list, dur_list)) / len(dur_list)) if dur_list else 0.0
        forecast_b = ewma(bytes_list[-10:]) if bytes_list else 0.0
        forecast_d = ewma(dur_list[-10:]) if dur_list else 0.0
        forecast_tp = (forecast_b / forecast_d) if forecast_d else 0.0
        issue = ""
        if sr < 0.5 and total_n >= 4:
            issue = "low success"
        elif succ >= 3 and sum(1 for r in recs[-3:] if not r.get("ok")) >= 2:
            issue = "recent failures"
        highlights.append({
            "src": src,
            "dst": dst,
            "success_rate": round(sr, 3),
            "total_bytes": total_b,
            "avg_throughput_bps": round(throughput, 2),
            "forecast_bytes_next": int(forecast_b),
            "forecast_throughput_bps": round(forecast_tp, 2),
            "issue": issue,
        })

    # Print top 5 edges likely to yield best next throughput
    highlights.sort(key=lambda r: r["forecast_throughput_bps"], reverse=True)
    for h in highlights[:5]:
        note = f" - {h['issue']}" if h['issue'] else ""
        print(
            f"  {h['src']} -> {h['dst']}: sr={h['success_rate']}, total={h['total_bytes']}B, "
            f"avg_tp={h['avg_throughput_bps']}B/s, forecast: {h['forecast_bytes_next']}B @ {h['forecast_throughput_bps']}B/s{note}"
        )


def main() -> int:
    args = parse_args()
    if args.analyze_only:
        _analyze_metrics(args.metrics_file)
        return 0

    node_map = load_topology(args.topology)
    batch: list[dict] = []
    for node in node_map.values():
        peers = node.get("peers", [])
        if not peers:
            continue
        if not isinstance(peers, list):
            raise ValueError(f"node {node['name']} has invalid 'peers' value")
        for peer_name in peers:
            if peer_name not in node_map:
                raise ValueError(f"node {node['name']} references unknown peer {peer_name}")
            peer = node_map[peer_name]
            offset = args.offset_dir / f"{node['name']}__to__{peer_name}.offset"
            rec = relay_once(args.relay_binary, node, peer, offset)
            batch.append(rec)

    _append_metrics(args.metrics_file, batch)
    if args.analyze:
        _analyze_metrics(args.metrics_file)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
