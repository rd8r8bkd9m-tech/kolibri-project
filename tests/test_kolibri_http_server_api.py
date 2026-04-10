#!/usr/bin/env python3
from __future__ import annotations

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


def request_json(base_url: str, path: str, payload: dict[str, object] | None = None) -> dict[str, object]:
    cmd = ["curl", "-sS", "-w", "\n%{http_code}", base_url + path]
    if payload is not None:
        cmd[1:1] = ["-X", "POST", "-H", "Content-Type: application/json"]
        cmd.extend(["-d", json.dumps(payload, ensure_ascii=False, separators=(",", ":"))])

    completed = subprocess.run(
        cmd,
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    body, status = completed.stdout.rsplit("\n", 1)
    if status != "200":
        raise RuntimeError(f"{path} returned HTTP {status}: {body}")
    return json.loads(body)


def wait_until_ready(base_url: str) -> None:
    last_error: Exception | None = None
    for _ in range(50):
        try:
            payload = request_json(base_url, "/api/v1/health")
            if payload.get("status") == "ok":
                return
        except (OSError, RuntimeError, json.JSONDecodeError, subprocess.CalledProcessError) as exc:
            last_error = exc
        time.sleep(0.2)
    raise RuntimeError(f"server did not become ready: {last_error}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_kolibri_http_server_api.py <kolibri_http_server>", file=sys.stderr)
        return 2

    server_bin = Path(sys.argv[1]).resolve()
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

        chat = request_json(
            base_url,
            "/api/v1/ai/chat",
            {"message": "реши уравнение 2x+3=7", "conversation_id": "smoke-http-server"},
        )
        assert isinstance(chat.get("response"), str) and chat["response"]
        assert chat.get("conversation_id") == "smoke-http-server"
        assert chat.get("method") == "math_linear"
        assert "x = 2" in chat["response"]
        assert chat.get("runtime_query_kind") == "math"
        assert chat.get("runtime_digit_winner") == "math_linear"
        assert chat.get("runtime_digit_votes", 0) >= 2
        assert chat.get("memory_linked") is False
        assert chat.get("semantic_word_count", 0) >= 1
        assert "verification_passed" in chat
        assert "verification_confidence" in chat
        assert "explanation" in chat
        assert "formula" in chat

        followup = request_json(
            base_url,
            "/api/v1/ai/chat",
            {"message": "А подробнее", "conversation_id": "smoke-http-server"},
        )
        assert followup.get("conversation_id") == "smoke-http-server"
        assert followup.get("method") == "dialog-context"
        assert followup.get("runtime_query_kind") == "followup"
        assert followup.get("runtime_digit_winner") == "dialog-context"
        assert followup.get("memory_linked") is True
        assert followup.get("conversation_turns", 0) >= 2
        assert followup.get("runtime_digit_votes", 0) >= 2
        assert "x = 2" in followup["response"] or "решени" in followup["response"].lower()
        assert "explanation" in followup

        verify = request_json(
            base_url,
            "/api/v1/ai/verify",
            {"query": "реши уравнение 2x+3=7", "answer": "x = 2"},
        )
        assert verify["verified"] is True
        assert verify["methods"] >= 2
        assert isinstance(verify.get("recommendation"), str) and verify["recommendation"]

        verify_wrong = request_json(
            base_url,
            "/api/v1/ai/verify",
            {"query": "реши уравнение 2x+3=7", "answer": "x = 5"},
        )
        assert verify_wrong["verified"] is False
        assert verify_wrong["confidence"] < verify["confidence"]

        explain = request_json(
            base_url,
            "/api/v1/ai/explain",
            {"query": "реши уравнение 2x+3=7", "answer": "x = 2"},
        )
        assert explain["steps"] >= 3
        assert isinstance(explain.get("formula"), str) and explain["formula"]
        assert isinstance(explain.get("explanation"), str) and explain["explanation"]

        estimate = request_json(
            base_url,
            "/api/v1/ai/chat",
            {"message": "составь смету на ремонт квартиры 60 м2", "conversation_id": "smoke-estimator"},
        )
        assert estimate.get("conversation_id") == "smoke-estimator"
        assert estimate.get("method") == "estimator_construction"
        assert estimate.get("runtime_query_kind") == "project"
        assert estimate.get("product_mode") == "estimator"
        assert estimate.get("project_active") is True
        assert estimate.get("domain_mode") == "construction"
        assert estimate.get("estimate_stage") == "draft_ready"
        assert estimate.get("project_kind") == "ремонт квартиры"
        assert abs(float(estimate.get("project_area_m2", 0.0)) - 60.0) < 0.01
        assert "Черновая смета" in estimate["response"]
        assert "Итого" in estimate["response"]

        estimate_followup = request_json(
            base_url,
            "/api/v1/ai/chat",
            {"message": "распиши этапы и риски", "conversation_id": "smoke-estimator"},
        )
        assert estimate_followup.get("conversation_id") == "smoke-estimator"
        assert estimate_followup.get("method") == "estimator_construction"
        assert estimate_followup.get("runtime_query_kind") == "project"
        assert estimate_followup.get("product_mode") == "estimator"
        assert estimate_followup.get("project_active") is True
        assert estimate_followup.get("estimate_stage") == "project_plan"
        assert estimate_followup.get("memory_linked") is True
        assert estimate_followup.get("conversation_turns", 0) >= 2
        assert "Этапы проекта" in estimate_followup["response"]
        assert "Риски" in estimate_followup["response"]
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)

    if proc.returncode not in (0, -15):
        output = proc.stdout.read() if proc.stdout else ""
        raise RuntimeError(f"server exited unexpectedly with {proc.returncode}\n{output}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
