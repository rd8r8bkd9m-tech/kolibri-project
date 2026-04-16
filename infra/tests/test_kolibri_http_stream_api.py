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


def request_json(base_url: str, path: str) -> dict[str, object]:
    completed = subprocess.run(
        ["curl", "-sS", "-w", "\n%{http_code}", base_url + path],
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


def request_sse(base_url: str, path: str, payload: dict[str, object]) -> list[tuple[str, dict[str, object]]]:
    completed = subprocess.run(
        [
            "curl",
            "-sS",
            "-N",
            "-X",
            "POST",
            "-H",
            "Content-Type: application/json",
            "-d",
            json.dumps(payload, ensure_ascii=False, separators=(",", ":")),
            base_url + path,
        ],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )

    raw = completed.stdout.replace("\r\n", "\n")
    events: list[tuple[str, dict[str, object]]] = []
    for block in raw.split("\n\n"):
        block = block.strip()
        if not block:
            continue
        event_type = ""
        data_raw = ""
        for line in block.split("\n"):
            if line.startswith("event:"):
                event_type = line.split(":", 1)[1].strip()
            elif line.startswith("data:"):
                data_raw = line.split(":", 1)[1].strip()
        if not event_type or not data_raw:
            continue
        events.append((event_type, json.loads(data_raw)))
    return events


def collect_text(events: list[tuple[str, dict[str, object]]]) -> str:
    chunks: list[str] = []
    for event_type, payload in events:
        if event_type != "token":
            continue
        text = payload.get("text") or payload.get("token") or ""
        chunks.append(str(text))
    return "".join(chunks)


def find_done(events: list[tuple[str, dict[str, object]]]) -> dict[str, object]:
    for event_type, payload in events:
        if event_type == "done":
            return payload
    raise RuntimeError("stream response did not include done event")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_kolibri_http_stream_api.py <kolibri_http_server>", file=sys.stderr)
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

        first = request_sse(
            base_url,
            "/api/v1/ai/chat/stream",
            {"message": "реши уравнение 2x+3=7", "conversation_id": "smoke-http-stream"},
        )
        first_text = collect_text(first)
        first_done = find_done(first)
        token_payloads = [payload for event_type, payload in first if event_type == "token"]

        assert token_payloads
        assert all("text" in payload for payload in token_payloads)
        assert all("token" in payload for payload in token_payloads)
        assert "x = 2" in first_text
        assert first_done.get("method") == "math_linear"
        assert first_done.get("runtime_query_kind") == "math"
        assert first_done.get("memory_linked") is False

        followup = request_sse(
            base_url,
            "/api/v1/ai/chat/stream",
            {"message": "А подробнее", "conversation_id": "smoke-http-stream"},
        )
        followup_text = collect_text(followup)
        followup_done = find_done(followup)

        assert "Разбор предыдущего решения по шагам." in followup_text
        assert "Ответ: Решение уравнения" in followup_text or "Ответ: Решение: x = 2" in followup_text
        assert "Ответ: Разбор предыдущего решения по шагам." not in followup_text
        assert followup_text.count("Разбор предыдущего решения по шагам.") == 1
        assert followup_done.get("method") == "dialog-context"
        assert followup_done.get("runtime_query_kind") == "followup"
        assert followup_done.get("memory_linked") is True
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
