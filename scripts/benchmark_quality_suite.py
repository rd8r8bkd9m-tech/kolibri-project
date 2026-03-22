#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import statistics
import time
import urllib.error
import urllib.request
from collections import defaultdict


def _request_json(method: str, url: str, payload: dict | None = None, timeout: float = 300.0) -> tuple[dict, float]:
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
    parser = argparse.ArgumentParser(description="Run Kolibri quality benchmark multiple times and aggregate results.")
    parser.add_argument("--base-url", default="https://kolibriai.ru")
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--min-score", type=float, default=0.75)
    parser.add_argument("--request-timeout", type=float, default=900.0)
    parser.add_argument(
        "--require-gates-pass",
        action="store_true",
        help="Fail run when benchmark report gate `gates.overall_pass` is false in any run.",
    )
    args = parser.parse_args()

    base = args.base_url.rstrip("/")
    run_url = base + "/api/v1/ai/quality/benchmark/run"
    runs = max(1, int(args.runs))

    score_values: list[float] = []
    duration_values: list[float] = []
    weighted_values: list[float] = []
    pass_rate_values: list[float] = []
    latency_p95_values: list[float] = []
    placeholder_rate_values: list[float] = []
    hallucination_rate_values: list[float] = []
    gate_fail_runs = 0
    case_stats: dict[str, dict[str, object]] = {}
    by_category_pass: dict[str, int] = defaultdict(int)
    by_category_total: dict[str, int] = defaultdict(int)

    for i in range(runs):
        try:
            report, wall_ms = _request_json(
                "POST",
                run_url,
                payload={},
                timeout=max(60.0, float(args.request_timeout)),
            )
        except urllib.error.HTTPError as exc:
            body = exc.read().decode("utf-8", errors="ignore")
            print(json.dumps({"status": exc.code, "error": body}, ensure_ascii=False, indent=2))
            return 1
        except Exception as exc:  # noqa: BLE001
            print(json.dumps({"error": str(exc)}, ensure_ascii=False, indent=2))
            return 1

        score = float(report.get("score", 0.0) or 0.0)
        weighted_score = float(report.get("score", 0.0) or 0.0)
        duration_ms = float(report.get("duration_ms", 0.0) or wall_ms)
        pass_rate = float(report.get("pass_rate", 0.0) or 0.0)
        latency_p95_ms = float(report.get("latency_p95_ms", 0.0) or 0.0)
        placeholder_rate = float(report.get("placeholder_rate", 0.0) or 0.0)
        hallucination_rate = float(report.get("hallucination_proxy_rate", 0.0) or 0.0)
        gates = report.get("gates", {}) or {}
        gate_overall_pass = bool(gates.get("overall_pass", True if not gates else False))
        if not gate_overall_pass:
            gate_fail_runs += 1

        score_values.append(score)
        weighted_values.append(weighted_score)
        duration_values.append(duration_ms)
        pass_rate_values.append(pass_rate)
        if latency_p95_ms > 0:
            latency_p95_values.append(latency_p95_ms)
        placeholder_rate_values.append(placeholder_rate)
        hallucination_rate_values.append(hallucination_rate)

        for item in report.get("details", []) or []:
            if not isinstance(item, dict):
                continue
            cid = str(item.get("id", "") or "").strip()
            if not cid:
                continue
            passed = bool(item.get("passed", False))
            category = str(item.get("category", "general") or "general")
            state = case_stats.setdefault(
                cid,
                {"id": cid, "category": category, "passed": 0, "total": 0, "last_reason": ""},
            )
            state["total"] = int(state.get("total", 0)) + 1
            if passed:
                state["passed"] = int(state.get("passed", 0)) + 1
            state["last_reason"] = str(item.get("reason", "") or "")
            by_category_total[category] += 1
            if passed:
                by_category_pass[category] += 1

        print(
            json.dumps(
                {
                    "run_index": i + 1,
                    "run_id": report.get("run_id"),
                    "score": score,
                    "pass_rate": pass_rate,
                    "duration_ms": round(duration_ms, 1),
                    "latency_p95_ms": round(latency_p95_ms, 1),
                    "gates_ok": gate_overall_pass,
                },
                ensure_ascii=False,
            )
        )

    case_rows = sorted(
        [
            {
                **row,
                "pass_rate": round((int(row["passed"]) / max(1, int(row["total"]))), 4),
            }
            for row in case_stats.values()
        ],
        key=lambda r: (r["pass_rate"], r["id"]),
    )
    category_rows = []
    for category in sorted(by_category_total):
        total = by_category_total[category]
        passed = by_category_pass[category]
        category_rows.append(
            {
                "category": category,
                "passed": passed,
                "total": total,
                "pass_rate": round((passed / max(1, total)), 4),
            }
        )

    summary = {
        "base_url": base,
        "runs": runs,
        "score_avg": round(statistics.fmean(score_values), 4) if score_values else 0.0,
        "score_min": round(min(score_values), 4) if score_values else 0.0,
        "score_max": round(max(score_values), 4) if score_values else 0.0,
        "duration_ms_avg": round(statistics.fmean(duration_values), 1) if duration_values else 0.0,
        "pass_rate_avg": round(statistics.fmean(pass_rate_values), 4) if pass_rate_values else 0.0,
        "latency_p95_ms_avg": round(statistics.fmean(latency_p95_values), 1) if latency_p95_values else 0.0,
        "weighted_score_avg": round(statistics.fmean(weighted_values), 4) if weighted_values else 0.0,
        "placeholder_rate_avg": round(statistics.fmean(placeholder_rate_values), 4) if placeholder_rate_values else 0.0,
        "hallucination_proxy_rate_avg": round(statistics.fmean(hallucination_rate_values), 4) if hallucination_rate_values else 0.0,
        "gate_fail_runs": gate_fail_runs,
        "categories": category_rows,
        "cases": case_rows,
    }
    print(json.dumps(summary, ensure_ascii=False, indent=2))

    score_ok = summary["score_avg"] >= float(args.min_score)
    gates_ok = gate_fail_runs == 0
    status = {
        "score_ok": score_ok,
        "gates_ok": gates_ok,
        "require_gates_pass": bool(args.require_gates_pass),
    }
    print(json.dumps({"status": status}, ensure_ascii=False))

    if not score_ok:
        return 2
    if args.require_gates_pass and not gates_ok:
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
