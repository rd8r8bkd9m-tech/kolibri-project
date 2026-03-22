#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import time
import urllib.request


def stream_ttfb_ms(base_url: str, message: str, timeout: float = 120.0) -> tuple[int, float]:
    payload = json.dumps({"message": message, "conversation_id": "bench-stream"}, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(
        base_url.rstrip("/") + "/api/v1/ai/chat/stream",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    started = time.perf_counter()
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        while True:
            line = resp.readline()
            if not line:
                break
            text = line.decode("utf-8", errors="replace").strip()
            if text.startswith("data:"):
                return resp.status, (time.perf_counter() - started) * 1000
    return 0, (time.perf_counter() - started) * 1000


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="https://kolibriai.ru")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--message", default="Сделай краткий ответ о состоянии системы.")
    args = parser.parse_args()

    times = []
    statuses = []
    for i in range(args.runs):
        code, ms = stream_ttfb_ms(args.base_url, f"{args.message} (stream {i+1})")
        statuses.append(code)
        times.append(ms)

    report = {
        "base_url": args.base_url,
        "runs": args.runs,
        "status_codes": statuses,
        "ttfb_ms": [round(v, 2) for v in times],
        "ttfb_mean_ms": round(sum(times) / len(times), 2) if times else 0.0,
        "ttfb_max_ms": round(max(times), 2) if times else 0.0,
        "timestamp": int(time.time()),
    }
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if all(code == 200 for code in statuses) else 1


if __name__ == "__main__":
    raise SystemExit(main())
