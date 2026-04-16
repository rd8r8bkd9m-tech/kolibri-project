#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path


def pick_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def request_json(base_url: str, path: str, payload: dict[str, object] | None = None) -> tuple[dict[str, object], float]:
    cmd = ["curl", "-sS", "-w", "\n%{http_code}", base_url + path]
    if payload is not None:
        cmd[1:1] = ["-X", "POST", "-H", "Content-Type: application/json"]
        cmd.extend(["-d", json.dumps(payload, ensure_ascii=False, separators=(",", ":"))])

    t0 = time.perf_counter()
    completed = subprocess.run(
        cmd,
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    elapsed_ms = (time.perf_counter() - t0) * 1000.0
    body, status = completed.stdout.rsplit("\n", 1)
    if status != "200":
        raise RuntimeError(f"{path} returned HTTP {status}: {body}")
    return json.loads(body), elapsed_ms


def wait_until_ready(base_url: str) -> None:
    last_error: Exception | None = None
    for _ in range(50):
        try:
            payload, _ = request_json(base_url, "/api/v1/health")
            if payload.get("status") == "ok":
                return
        except (OSError, RuntimeError, json.JSONDecodeError, subprocess.CalledProcessError) as exc:
            last_error = exc
        time.sleep(0.2)
    raise RuntimeError(f"server did not become ready: {last_error}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_case(case_id: str, category: str, fn) -> dict[str, object]:
    started = time.perf_counter()
    try:
        details = fn()
        passed = True
        reason = ""
    except Exception as exc:  # noqa: BLE001
        details = {}
        passed = False
        reason = str(exc)
    duration_ms = (time.perf_counter() - started) * 1000.0
    return {
        "id": case_id,
        "category": category,
        "passed": passed,
        "reason": reason,
        "duration_ms": round(duration_ms, 1),
        "details": details,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Reproducible Phase 1 benchmark for the C HTTP runtime.")
    parser.add_argument("server_bin", help="Path to kolibri_http_server binary")
    parser.add_argument("--output-json", help="Optional path to write benchmark report JSON")
    args = parser.parse_args()

    server_bin = Path(args.server_bin).resolve()
    repo_root = Path(__file__).resolve().parents[1]
    port = pick_free_port()
    base_url = f"http://127.0.0.1:{port}"

    proc = subprocess.Popen(
        [str(server_bin), str(port), "frontend/dist"],
        cwd=repo_root,
        env={**os.environ, "KOLIBRI_HTTP_SMOKE": "1"},
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )

    try:
        wait_until_ready(base_url)

        def case_exact_arithmetic() -> dict[str, object]:
            chat, elapsed_ms = request_json(
                base_url,
                "/api/v1/ai/chat",
                {"message": "сколько будет 7 * 8", "conversation_id": "phase1-bench-arithmetic"},
            )
            require(chat.get("method") == "math_calc", f"expected math_calc, got {chat.get('method')}")
            require("56" in str(chat.get("response", "")), "exact arithmetic response does not contain 56")
            require(chat.get("runtime_query_kind") == "math", "runtime_query_kind should be math")
            require("verification_passed" in chat, "exact arithmetic chat is missing verification metadata")
            return {
                "method": chat.get("method"),
                "response": chat.get("response"),
                "elapsed_ms": round(elapsed_ms, 1),
                "confidence": chat.get("confidence"),
            }

        def case_algebra_solver() -> dict[str, object]:
            chat, elapsed_ms = request_json(
                base_url,
                "/api/v1/ai/chat",
                {"message": "реши уравнение 2x+3=7", "conversation_id": "phase1-bench-algebra"},
            )
            require(chat.get("method") == "math_linear", f"expected math_linear, got {chat.get('method')}")
            require("x = 2" in str(chat.get("response", "")), "solver response does not contain x = 2")
            require(chat.get("verification_passed") is True, "solver chat should be verified")
            require(int(chat.get("explanation_steps", 0)) >= 3, "solver chat explanation is too shallow")
            return {
                "method": chat.get("method"),
                "response": chat.get("response"),
                "elapsed_ms": round(elapsed_ms, 1),
                "verification_confidence": chat.get("verification_confidence"),
            }

        def case_reasoning_endpoint() -> dict[str, object]:
            reason, elapsed_ms = request_json(
                base_url,
                "/api/v1/ai/reason",
                {"query": "что если Земля не вращалась"},
            )
            require(reason.get("type") == "Counterfactual", f"expected Counterfactual, got {reason.get('type')}")
            answer = str(reason.get("answer", ""))
            require("измен" in answer or "затронуты" in answer, "counterfactual answer is missing causal impact")
            require(float(reason.get("confidence", 0.0)) >= 0.5, "counterfactual reasoning confidence too low")
            require(int(reason.get("steps", 0)) >= 5, "counterfactual reasoning should expose multiple steps")
            return {
                "type": reason.get("type"),
                "answer": answer,
                "elapsed_ms": round(elapsed_ms, 1),
                "confidence": reason.get("confidence"),
                "steps": reason.get("steps"),
            }

        def case_contradiction_verify() -> dict[str, object]:
            verified_ok, _ = request_json(
                base_url,
                "/api/v1/ai/verify",
                {"query": "реши уравнение 2x+3=7", "answer": "x = 2"},
            )
            verified_bad, elapsed_ms = request_json(
                base_url,
                "/api/v1/ai/verify",
                {"query": "реши уравнение 2x+3=7", "answer": "x = 5"},
            )
            require(verified_ok.get("verified") is True, "reference verification should pass")
            require(verified_bad.get("verified") is False, "wrong answer should fail verification")
            require(
                float(verified_bad.get("confidence", 1.0)) < float(verified_ok.get("confidence", 0.0)),
                "wrong answer confidence should drop below the correct answer confidence",
            )
            return {
                "verified_good": verified_ok.get("verified"),
                "verified_bad": verified_bad.get("verified"),
                "confidence_good": verified_ok.get("confidence"),
                "confidence_bad": verified_bad.get("confidence"),
                "contradictions_bad": verified_bad.get("contradictions"),
                "elapsed_ms": round(elapsed_ms, 1),
            }

        def case_explanation_fidelity() -> dict[str, object]:
            explain, elapsed_ms = request_json(
                base_url,
                "/api/v1/ai/explain",
                {"query": "реши уравнение 2x+3=7", "answer": "x = 2"},
            )
            explanation = str(explain.get("explanation", ""))
            require(int(explain.get("steps", 0)) >= 3, "explanation should have at least 3 steps")
            require(bool(explain.get("formula")), "explanation formula is empty")
            require("x" in explanation or "шаг" in explanation.lower(), "explanation lost math structure")
            return {
                "steps": explain.get("steps"),
                "formula": explain.get("formula"),
                "confidence": explain.get("confidence"),
                "elapsed_ms": round(elapsed_ms, 1),
            }

        def case_followup_continuity() -> dict[str, object]:
            conv = "phase1-bench-continuity"
            first, _ = request_json(
                base_url,
                "/api/v1/ai/chat",
                {"message": "реши уравнение 2x+3=7", "conversation_id": conv},
            )
            require(first.get("method") == "math_linear", "continuity anchor should start with math_linear")

            followups = [
                "А подробнее",
                "Почему?",
                "Покажи шаги",
                "Приведи пример",
                "Что ещё важного?",
            ]
            chain: list[dict[str, object]] = []
            for idx, message in enumerate(followups, start=2):
                reply, elapsed_ms = request_json(
                    base_url,
                    "/api/v1/ai/chat",
                    {"message": message, "conversation_id": conv},
                )
                require(reply.get("method") == "dialog-context", f"turn {idx}: expected dialog-context")
                require(reply.get("memory_linked") is True, f"turn {idx}: memory_linked must be true")
                require(reply.get("runtime_query_kind") == "followup", f"turn {idx}: expected followup kind")
                require(int(reply.get("conversation_turns", 0)) >= idx, f"turn {idx}: turn counter did not advance")
                response_text = str(reply.get("response", "")).lower()
                require(
                    "x = 2" in response_text or "реш" in response_text or "шаг" in response_text or "пример" in response_text,
                    f"turn {idx}: followup drifted off the anchor topic",
                )
                chain.append(
                    {
                        "turn": idx,
                        "message": message,
                        "method": reply.get("method"),
                        "elapsed_ms": round(elapsed_ms, 1),
                        "conversation_turns": reply.get("conversation_turns"),
                    }
                )

            return {
                "anchor_method": first.get("method"),
                "final_turns": chain[-1]["conversation_turns"],
                "turns": chain,
            }

        cases = [
            run_case("exact_arithmetic", "math", case_exact_arithmetic),
            run_case("algebra_solver", "algebra", case_algebra_solver),
            run_case("reasoning_endpoint", "reasoning", case_reasoning_endpoint),
            run_case("contradiction_verify", "trust", case_contradiction_verify),
            run_case("explanation_fidelity", "explain", case_explanation_fidelity),
            run_case("followup_continuity_5turn", "continuity", case_followup_continuity),
        ]

        passed = sum(1 for case in cases if case["passed"])
        overall_pass = passed == len(cases)
        report = {
            "suite": "kolibri_http_phase1",
            "backend": "C-core",
            "cases_total": len(cases),
            "cases_passed": passed,
            "pass_rate": round(passed / max(1, len(cases)), 4),
            "overall_pass": overall_pass,
            "duration_ms_total": round(sum(float(case["duration_ms"]) for case in cases), 1),
            "cases": cases,
        }

        if args.output_json:
            output_path = Path(args.output_json)
            output_path.parent.mkdir(parents=True, exist_ok=True)
            output_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 0 if overall_pass else 1
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
