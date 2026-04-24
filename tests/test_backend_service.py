from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, Dict

import pytest
from fastapi.testclient import TestClient

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from backend.service.main import app, get_settings


@pytest.fixture(autouse=True)
def clear_settings_cache(monkeypatch: pytest.MonkeyPatch) -> None:
    for key in (
        "KOLIBRI_RESPONSE_MODE",
        "KOLIBRI_LLM_ENDPOINT",
        "KOLIBRI_LLM_API_KEY",
        "KOLIBRI_LLM_MODEL",
        "KOLIBRI_LLM_TIMEOUT",
    ):
        monkeypatch.delenv(key, raising=False)
    get_settings.cache_clear()


@pytest.fixture()
def client() -> TestClient:
    return TestClient(app)


def test_health_reports_response_mode(
    monkeypatch: pytest.MonkeyPatch, client: TestClient
) -> None:
    monkeypatch.setenv("KOLIBRI_RESPONSE_MODE", "script")
    get_settings.cache_clear()

    response = client.get("/api/health")
    assert response.status_code == 200
    payload = response.json()
    assert payload["response_mode"] == "script"


def test_infer_disabled_when_not_llm(
    monkeypatch: pytest.MonkeyPatch, client: TestClient
) -> None:
    monkeypatch.setenv("KOLIBRI_RESPONSE_MODE", "script")
    get_settings.cache_clear()

    response = client.post("/api/v1/infer", json={"prompt": "ping"})
    assert response.status_code == 503
    assert response.json()["detail"] == "LLM mode is disabled"


def test_infer_missing_endpoint(
    monkeypatch: pytest.MonkeyPatch, client: TestClient
) -> None:
    monkeypatch.setenv("KOLIBRI_RESPONSE_MODE", "llm")
    get_settings.cache_clear()

    response = client.post("/api/v1/infer", json={"prompt": "ping"})
    assert response.status_code == 503
    assert "endpoint" in response.json()["detail"].lower()


class _DummyResponse:
    status_code = 200
    text = "OK"

    def raise_for_status(self) -> None:
        return None

    def json(self) -> Dict[str, Any]:
        return {"response": "pong", "provider": "test-provider"}


class _DummyClient:
    def __init__(self, *args: Any, **kwargs: Any) -> None:
        self.args = args
        self.kwargs = kwargs

    async def __aenter__(self) -> "_DummyClient":
        return self

    async def __aexit__(self, exc_type, exc, tb) -> None:  # type: ignore[override]
        return None

    async def post(self, *args: Any, **kwargs: Any) -> _DummyResponse:
        return _DummyResponse()


def test_infer_success(monkeypatch: pytest.MonkeyPatch, client: TestClient) -> None:
    monkeypatch.setenv("KOLIBRI_RESPONSE_MODE", "llm")
    monkeypatch.setenv("KOLIBRI_LLM_ENDPOINT", "https://example.test/llm")
    get_settings.cache_clear()

    def factory(*args: Any, **kwargs: Any) -> _DummyClient:
        return _DummyClient(*args, **kwargs)

    monkeypatch.setattr("backend.service.main.httpx.AsyncClient", factory)

    response = client.post("/api/v1/infer", json={"prompt": "ping", "mode": "test"})

    assert response.status_code == 200
    payload = response.json()
    assert payload["response"] == "pong"
    assert payload["provider"] == "test-provider"
    assert payload["latency_ms"] >= 0
