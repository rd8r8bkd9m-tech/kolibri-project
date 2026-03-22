#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request


def _request_json(method: str, url: str, payload: dict | None = None, timeout: float = 120.0) -> tuple[dict, float]:
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    t0 = time.perf_counter()
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        body = json.loads(resp.read().decode("utf-8"))
    elapsed_ms = (time.perf_counter() - t0) * 1000.0
    return body, elapsed_ms


def main() -> int:
    parser = argparse.ArgumentParser(description="Run/read Kolibri uniqueness proof suite.")
    parser.add_argument("--base-url", default="https://kolibriai.ru")
    parser.add_argument("--latest", action="store_true", help="Read latest report instead of running a new one")
    parser.add_argument("--min-score", type=float, default=0.85)
    args = parser.parse_args()

    base = args.base_url.rstrip("/")
    run_url = base + "/api/v1/ai/quality/uniqueness/run"
    latest_url = base + "/api/v1/ai/quality/uniqueness"

    try:
        if args.latest:
            report, wall_ms = _request_json("GET", latest_url, payload=None, timeout=60.0)
        else:
            report, wall_ms = _request_json("POST", run_url, payload={}, timeout=180.0)
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="ignore")
        print(json.dumps({"status": exc.code, "error": body}, ensure_ascii=False, indent=2))
        return 1
    except Exception as exc:
        print(json.dumps({"error": str(exc)}, ensure_ascii=False, indent=2))
        return 1

    score = float(report.get("score", 0.0) or 0.0)
    passed = int(report.get("passed", 0) or 0)
    total = int(report.get("total", 0) or 0)
    output = {
        "base_url": base,
        "run_id": report.get("run_id"),
        "score": score,
        "passed": passed,
        "total": total,
        "fingerprint": report.get("fingerprint", ""),
        "claims": report.get("claims", []),
        "request_wall_ms": round(wall_ms, 1),
        "details": report.get("details", []),
    }
    print(json.dumps(output, ensure_ascii=False, indent=2))
    return 0 if score >= args.min_score else 2


if __name__ == "__main__":
    sys.exit(main())

