#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import statistics
import time
import urllib.request


def call_chat(base_url: str, message: str, timeout: float = 120.0) -> tuple[int, float]:
    payload = json.dumps({"message": message, "conversation_id": "bench-latency"}, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(
        base_url.rstrip("/") + "/api/v1/ai/chat",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    started = time.perf_counter()
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        _ = resp.read()
        elapsed_ms = (time.perf_counter() - started) * 1000
        return resp.status, elapsed_ms


def percentile(sorted_values: list[float], p: float) -> float:
    if not sorted_values:
        return 0.0
    idx = int(round((len(sorted_values) - 1) * p))
    return sorted_values[max(0, min(idx, len(sorted_values) - 1))]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="https://kolibriai.ru")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--message", default="Сделай краткий ответ о состоянии системы.")
    args = parser.parse_args()

    results: list[float] = []
    statuses: list[int] = []
    for i in range(args.runs):
        status, ms = call_chat(args.base_url, f"{args.message} (запуск {i+1})")
        statuses.append(status)
        results.append(ms)

    vals = sorted(results)
    report = {
        "base_url": args.base_url,
        "runs": args.runs,
        "status_codes": statuses,
        "latency_ms": [round(v, 2) for v in results],
        "p50_ms": round(percentile(vals, 0.50), 2),
        "p95_ms": round(percentile(vals, 0.95), 2),
        "mean_ms": round(statistics.mean(results), 2),
        "max_ms": round(max(results), 2),
        "timestamp": int(time.time()),
    }
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if all(code == 200 for code in statuses) else 1


if __name__ == "__main__":
    raise SystemExit(main())
