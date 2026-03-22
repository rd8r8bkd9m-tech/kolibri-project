#!/usr/bin/env python3
from __future__ import annotations

import json
import socket
import sys
import time
import urllib.error
import urllib.request


BASE_URL = "https://kolibriai.ru"


def request_json(path: str, method: str = "GET", payload: dict | None = None, timeout: float = 25.0) -> tuple[int, dict, float]:
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(BASE_URL + path, data=data, headers=headers, method=method)
    started = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read().decode("utf-8", errors="replace")
            elapsed_ms = (time.perf_counter() - started) * 1000
            try:
                parsed = json.loads(body)
            except json.JSONDecodeError:
                parsed = {"_raw": body[:1000]}
            return resp.status, parsed, elapsed_ms
    except urllib.error.HTTPError as e:
        elapsed_ms = (time.perf_counter() - started) * 1000
        body = e.read().decode("utf-8", errors="replace")
        try:
            parsed = json.loads(body)
        except json.JSONDecodeError:
            parsed = {"_raw": body[:1000]}
        return e.code, parsed, elapsed_ms
    except (urllib.error.URLError, TimeoutError, socket.timeout) as e:
        elapsed_ms = (time.perf_counter() - started) * 1000
        return 0, {"error": str(e)}, elapsed_ms


def main() -> int:
    report: dict[str, object] = {"base_url": BASE_URL, "checks": [], "timestamp": int(time.time())}
    checks: list[dict[str, object]] = []

    for model_path in ("/api/v1/ai/models", "/api/v1/ai/stats"):
        code, body, ms = request_json(model_path)
        checks.append(
            {
                "name": f"models:{model_path}",
                "ok": code == 200,
                "status": code,
                "latency_ms": round(ms, 2),
                "sample": body,
            }
        )

    code, body, ms = request_json(
        "/api/v1/ai/chat",
        method="POST",
        payload={"message": "Короткая проверка прод-чата", "conversation_id": "prod-smoke"},
        timeout=60.0,
    )
    checks.append(
        {
            "name": "chat:/api/v1/ai/chat",
            "ok": code == 200 and isinstance(body, dict) and "response" in body,
            "status": code,
            "latency_ms": round(ms, 2),
            "response_preview": (body.get("response", "")[:180] if isinstance(body, dict) else ""),
        }
    )

    report["checks"] = checks
    report["all_ok"] = all(c["ok"] for c in checks)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report["all_ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
