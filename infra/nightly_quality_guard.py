#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import statistics
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def _request_json(method: str, url: str, payload: dict[str, Any] | None = None, timeout: float = 120.0) -> tuple[dict[str, Any], float]:
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


def _post_json(url: str, payload: dict[str, Any], timeout: float = 20.0) -> tuple[int, str]:
    req = urllib.request.Request(
        url,
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={"Content-Type": "application/json", "Accept": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        body = resp.read().decode("utf-8", errors="replace")
        return int(resp.status), body[:400]


def _history_p95_avg(base_url: str, timeout: float, limit: int) -> float:
    try:
        body, _ = _request_json(
            "GET",
            f"{base_url.rstrip('/')}/api/v1/ai/quality/benchmark/history?limit={max(1, int(limit))}",
            payload=None,
            timeout=max(10.0, float(timeout)),
        )
        if isinstance(body, dict):
            items = body.get("items", [])
            if isinstance(items, list):
                values: list[float] = []
                for item in items:
                    if not isinstance(item, dict):
                        continue
                    p95 = _safe_float(item.get("latency_p95_ms"), 0.0)
                    if p95 > 0:
                        values.append(p95)
                if values:
                    values.sort()
                    mid = len(values) // 2
                    if len(values) % 2:
                        return float(values[mid])
                    return float((values[mid - 1] + values[mid]) / 2.0)
            trend = body.get("trend", {})
            if isinstance(trend, dict):
                return _safe_float(trend.get("latency_p95_ms_avg"), 0.0)
    except Exception:
        return 0.0
    return 0.0


def _safe_float(value: Any, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _load_json(path: Path) -> dict[str, Any] | None:
    try:
        if not path.exists():
            return None
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None


def _save_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    tmp.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser(description="Nightly quality guard for Kolibri backend.")
    parser.add_argument("--base-url", default="http://127.0.0.1:8001")
    parser.add_argument("--runs", type=int, default=2)
    parser.add_argument("--min-score", type=float, default=0.85)
    parser.add_argument("--request-timeout", type=float, default=300.0)
    parser.add_argument("--max-p95-regression-ratio", type=float, default=1.35)
    parser.add_argument("--p95-regression-min-delta-ms", type=float, default=350.0)
    parser.add_argument("--max-gate-fail-runs", type=int, default=0)
    parser.add_argument("--history-baseline-limit", type=int, default=30)
    parser.add_argument("--report-dir", default="docs/reports")
    parser.add_argument("--baseline-file", default="docs/reports/quality_baseline.json")
    parser.add_argument("--no-require-gates-pass", action="store_true")
    parser.add_argument("--alert-webhook", default=os.getenv("KOLIBRI_QUALITY_ALERT_WEBHOOK", "").strip())
    parser.add_argument("--alert-telegram-bot-token", default=os.getenv("KOLIBRI_QUALITY_ALERT_TELEGRAM_BOT_TOKEN", "").strip())
    parser.add_argument("--alert-telegram-chat-id", default=os.getenv("KOLIBRI_QUALITY_ALERT_TELEGRAM_CHAT_ID", "").strip())
    parser.add_argument("--alert-timeout", type=float, default=20.0)
    parser.add_argument("--alert-on-success", action="store_true")
    args = parser.parse_args()

    base_url = args.base_url.rstrip("/")
    run_url = f"{base_url}/api/v1/ai/quality/benchmark/run"
    runs = max(1, int(args.runs))
    require_gates_pass = not bool(args.no_require_gates_pass)

    run_items: list[dict[str, Any]] = []
    score_values: list[float] = []
    pass_rate_values: list[float] = []
    p95_values: list[float] = []
    gate_fail_runs = 0
    errors: list[str] = []

    for i in range(runs):
        t0 = time.perf_counter()
        try:
            body, _ = _request_json(
                "POST",
                run_url,
                payload={},
                timeout=max(60.0, float(args.request_timeout)),
            )
            wall_ms = (time.perf_counter() - t0) * 1000.0
            score = _safe_float(body.get("score"), 0.0)
            pass_rate = _safe_float(body.get("pass_rate"), 0.0)
            p95 = _safe_float(body.get("latency_p95_ms"), 0.0)
            gates = body.get("gates", {})
            if not isinstance(gates, dict):
                gates = {}
            gates_ok = bool(gates.get("overall_pass", False))
            if not gates_ok:
                gate_fail_runs += 1

            item = {
                "run_index": i + 1,
                "run_id": str(body.get("run_id", "")),
                "score": score,
                "pass_rate": pass_rate,
                "latency_p95_ms": p95,
                "duration_ms": _safe_float(body.get("duration_ms"), wall_ms),
                "gates_ok": gates_ok,
                "gates": gates,
            }
            run_items.append(item)
            score_values.append(score)
            pass_rate_values.append(pass_rate)
            if p95 > 0:
                p95_values.append(p95)
        except urllib.error.HTTPError as exc:
            raw = exc.read().decode("utf-8", errors="replace")
            errors.append(f"http:{exc.code}:{raw[:300]}")
        except Exception as exc:  # noqa: BLE001
            errors.append(f"error:{exc}")

    now = datetime.now(timezone.utc)
    stamp = now.strftime("%Y%m%d_%H%M%S")
    report_dir = Path(args.report_dir)
    baseline_path = Path(args.baseline_file)
    report_path = report_dir / f"nightly_quality_guard_{stamp}.json"
    latest_path = report_dir / "nightly_quality_guard_latest.json"

    summary = {
        "runs": runs,
        "score_avg": round(statistics.fmean(score_values), 4) if score_values else 0.0,
        "pass_rate_avg": round(statistics.fmean(pass_rate_values), 4) if pass_rate_values else 0.0,
        "latency_p95_ms_avg": round(statistics.fmean(p95_values), 1) if p95_values else 0.0,
        "gate_fail_runs": gate_fail_runs,
    }

    baseline = _load_json(baseline_path) or {}
    baseline_p95_file = _safe_float((baseline or {}).get("latency_p95_ms_avg"), 0.0)
    baseline_p95_history = _history_p95_avg(base_url, timeout=max(10.0, float(args.request_timeout) * 0.5), limit=int(args.history_baseline_limit))
    baseline_p95 = max(baseline_p95_file, baseline_p95_history)
    stable_p95_values = [float(item.get("latency_p95_ms", 0.0) or 0.0) for item in run_items if bool(item.get("gates_ok", False))]
    stable_p95_values = [v for v in stable_p95_values if v > 0.0]
    if stable_p95_values:
        current_p95 = round(statistics.fmean(stable_p95_values), 1)
    else:
        current_p95 = _safe_float(summary.get("latency_p95_ms_avg"), 0.0)
    p95_ratio = (current_p95 / baseline_p95) if baseline_p95 > 0 and current_p95 > 0 else 1.0
    p95_delta = (current_p95 - baseline_p95) if current_p95 > 0 and baseline_p95 > 0 else 0.0

    reasons: list[str] = []
    if errors:
        reasons.append("benchmark_request_error")
    if summary["score_avg"] < float(args.min_score):
        reasons.append("score_below_min")
    if require_gates_pass and gate_fail_runs > int(args.max_gate_fail_runs):
        reasons.append("gates_failed")
    if (
        baseline_p95 > 0
        and current_p95 > baseline_p95 * float(args.max_p95_regression_ratio)
        and p95_delta > float(args.p95_regression_min_delta_ms)
    ):
        reasons.append("latency_p95_regression")

    status_ok = len(reasons) == 0

    report = {
        "timestamp_utc": now.isoformat(),
        "base_url": base_url,
        "status": {
            "ok": status_ok,
            "reasons": reasons,
            "errors": errors,
        },
        "config": {
            "runs": runs,
            "min_score": float(args.min_score),
            "require_gates_pass": require_gates_pass,
            "max_gate_fail_runs": int(args.max_gate_fail_runs),
            "max_p95_regression_ratio": float(args.max_p95_regression_ratio),
            "p95_regression_min_delta_ms": float(args.p95_regression_min_delta_ms),
            "history_baseline_limit": int(args.history_baseline_limit),
            "request_timeout": float(args.request_timeout),
        },
        "summary": summary,
        "baseline": {
            "path": str(baseline_path),
            "latency_p95_ms_avg_file": baseline_p95_file,
            "latency_p95_ms_avg_history": baseline_p95_history,
            "latency_p95_ms_avg": baseline_p95,
            "regression_eval_latency_p95_ms": current_p95,
            "current_to_baseline_p95_ratio": round(p95_ratio, 4),
            "current_minus_baseline_p95_ms": round(p95_delta, 1),
        },
        "runs_data": run_items,
        "alerts": {"attempted": False, "results": []},
    }

    should_alert = (not status_ok) or bool(args.alert_on_success)
    alert_results: list[dict[str, Any]] = []
    if should_alert:
        severity = "ok" if status_ok else "critical"
        alert_payload = {
            "source": "kolibri-nightly-quality-guard",
            "severity": severity,
            "timestamp_utc": now.isoformat(),
            "base_url": base_url,
            "status_ok": status_ok,
            "reasons": reasons,
            "summary": summary,
            "baseline_ratio_p95": round(p95_ratio, 4),
        }

        webhook_url = str(args.alert_webhook or "").strip()
        if webhook_url:
            try:
                code, preview = _post_json(webhook_url, alert_payload, timeout=max(3.0, float(args.alert_timeout)))
                alert_results.append({"channel": "webhook", "ok": code < 400, "status": code, "preview": preview})
            except Exception as exc:  # noqa: BLE001
                alert_results.append({"channel": "webhook", "ok": False, "error": str(exc)})

        tg_token = str(args.alert_telegram_bot_token or "").strip()
        tg_chat = str(args.alert_telegram_chat_id or "").strip()
        if tg_token and tg_chat:
            tg_url = f"https://api.telegram.org/bot{tg_token}/sendMessage"
            tg_text = (
                "Kolibri nightly quality guard\n"
                f"Status: {'OK' if status_ok else 'FAIL'}\n"
                f"Base: {base_url}\n"
                f"Score avg: {summary['score_avg']}\n"
                f"Pass rate avg: {summary['pass_rate_avg']}\n"
                f"P95 avg: {summary['latency_p95_ms_avg']} ms\n"
                f"Gate fail runs: {summary['gate_fail_runs']}\n"
                f"Reasons: {', '.join(reasons) if reasons else '-'}\n"
                f"UTC: {now.isoformat()}"
            )
            tg_payload = {
                "chat_id": tg_chat,
                "text": tg_text,
                "disable_web_page_preview": True,
            }
            try:
                code, preview = _post_json(tg_url, tg_payload, timeout=max(3.0, float(args.alert_timeout)))
                alert_results.append({"channel": "telegram", "ok": code < 400, "status": code, "preview": preview})
            except Exception as exc:  # noqa: BLE001
                alert_results.append({"channel": "telegram", "ok": False, "error": str(exc)})

    report["alerts"] = {"attempted": bool(should_alert), "results": alert_results}

    _save_json(report_path, report)
    _save_json(latest_path, report)

    if status_ok:
        new_baseline = {
            "updated_at_utc": now.isoformat(),
            "source_report": str(report_path),
            "score_avg": summary["score_avg"],
            "pass_rate_avg": summary["pass_rate_avg"],
            "latency_p95_ms_avg": summary["latency_p95_ms_avg"],
            "gate_fail_runs": summary["gate_fail_runs"],
        }
        _save_json(baseline_path, new_baseline)

    print(json.dumps({"report_path": str(report_path), "status": report["status"], "summary": summary}, ensure_ascii=False))
    return 0 if status_ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
