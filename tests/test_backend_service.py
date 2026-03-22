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
        "KOLIBRI_LOCAL_ONLY",
        "KOLIBRI_VISION_MODE",
        "KOLIBRI_LLM_ENDPOINT",
        "KOLIBRI_LLM_API_KEY",
        "KOLIBRI_LLM_MODEL",
        "KOLIBRI_LLM_TIMEOUT",
        "OPENAI_API_KEY",
    ):
        monkeypatch.delenv(key, raising=False)
    get_settings.cache_clear()


@pytest.fixture()
def client() -> TestClient:
    return TestClient(app)


def test_health_reports_response_mode(monkeypatch: pytest.MonkeyPatch, client: TestClient) -> None:
    monkeypatch.setenv("KOLIBRI_RESPONSE_MODE", "script")
    get_settings.cache_clear()

    response = client.get("/api/health")
    assert response.status_code == 200
    payload = response.json()
    assert payload["response_mode"] == "script"
    assert payload["local_only"] is True


def test_infer_disabled_when_not_llm(monkeypatch: pytest.MonkeyPatch, client: TestClient) -> None:
    monkeypatch.setenv("KOLIBRI_RESPONSE_MODE", "script")
    get_settings.cache_clear()

    response = client.post("/api/v1/infer", json={"prompt": "ping"})
    assert response.status_code == 503
    assert response.json()["detail"] == "LLM mode is disabled"


def test_infer_missing_endpoint(monkeypatch: pytest.MonkeyPatch, client: TestClient) -> None:
    monkeypatch.setenv("KOLIBRI_RESPONSE_MODE", "llm")
    monkeypatch.setenv("KOLIBRI_LOCAL_ONLY", "0")
    get_settings.cache_clear()

    response = client.post("/api/v1/infer", json={"prompt": "ping"})
    assert response.status_code == 503
    assert "endpoint" in response.json()["detail"].lower()


def test_infer_blocked_in_local_only(monkeypatch: pytest.MonkeyPatch, client: TestClient) -> None:
    monkeypatch.setenv("KOLIBRI_RESPONSE_MODE", "llm")
    monkeypatch.setenv("KOLIBRI_LOCAL_ONLY", "1")
    monkeypatch.setenv("KOLIBRI_LLM_ENDPOINT", "https://example.test/llm")
    get_settings.cache_clear()

    response = client.post("/api/v1/infer", json={"prompt": "ping"})
    assert response.status_code == 503
    assert "local-only" in response.json()["detail"]


class _DummyResponse:
    status_code = 200
    text = "OK"

    def raise_for_status(self) -> None:
        return None

    def json(self) -> Dict[str, Any]:
        return {"response": "pong", "provider": "test-provider"}


class _DummyVisionResponse:
    status_code = 200
    text = "OK"

    def raise_for_status(self) -> None:
        return None

    def json(self) -> Dict[str, Any]:
        return {
            "choices": [
                {
                    "message": {
                        "content": "На изображении виден простой тестовый объект на однородном фоне."
                    }
                }
            ]
        }


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
    monkeypatch.setenv("KOLIBRI_LOCAL_ONLY", "0")
    monkeypatch.setenv("KOLIBRI_LLM_ENDPOINT", "https://example.test/llm")
    get_settings.cache_clear()

    def factory(*args: Any, **kwargs: Any) -> _DummyClient:
        return _DummyClient(*args, **kwargs)

    monkeypatch.setattr("backend.service.common.httpx.AsyncClient", factory)

    response = client.post("/api/v1/infer", json={"prompt": "ping", "mode": "test"})

    assert response.status_code == 200
    payload = response.json()
    assert payload["response"] == "pong"
    assert payload["provider"] == "test-provider"
    assert payload["latency_ms"] >= 0


def test_imagine_local_mode_without_external_calls(
    monkeypatch: pytest.MonkeyPatch,
    client: TestClient,
) -> None:
    monkeypatch.setenv("KOLIBRI_LOCAL_ONLY", "1")
    get_settings.cache_clear()

    def _fail_external_call(*args: Any, **kwargs: Any):
        raise AssertionError("External image provider must not be called in local-only mode")

    monkeypatch.setattr("backend.service.ai_chat.httpx.AsyncClient", _fail_external_call)

    response = client.post("/api/v1/ai/imagine", json={"prompt": "Сделай логотип колибри"})
    assert response.status_code == 200
    payload = response.json()
    assert payload["provider"] == "kolibri-local-imagine"
    assert payload["model"] == "kolibri-svg-v1"
    assert payload["image_url"].startswith("data:image/svg+xml;base64,")


def test_vision_local_mode_returns_metadata_without_external_calls(
    monkeypatch: pytest.MonkeyPatch,
    client: TestClient,
) -> None:
    monkeypatch.setenv("KOLIBRI_LOCAL_ONLY", "1")
    get_settings.cache_clear()

    def _fail_external_call(*args: Any, **kwargs: Any):
        raise AssertionError("External vision provider must not be called in local-only mode")

    monkeypatch.setattr("backend.service.ai_chat.httpx.AsyncClient", _fail_external_call)

    png_1x1 = (
        b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01"
        b"\x08\x02\x00\x00\x00\x90wS\xde\x00\x00\x00\x0cIDAT\x08\x99c```\x00"
        b"\x00\x00\x04\x00\x01\xf6\x178U\x00\x00\x00\x00IEND\xaeB`\x82"
    )

    response = client.post(
        "/api/v1/ai/vision/analyze",
        files={"file": ("tiny.png", png_1x1, "image/png")},
        data={"prompt": "Опиши изображение"},
    )
    assert response.status_code == 200
    payload = response.json()
    assert payload["provider"] == "kolibri-local-vision"
    assert payload["mime_type"] == "image/png"
    assert "Я вижу" in payload["response"] or "Изображение получено." in payload["response"]


def test_vision_prefers_local_analysis_by_default_even_with_provider_key(
    monkeypatch: pytest.MonkeyPatch,
    client: TestClient,
) -> None:
    monkeypatch.setenv("KOLIBRI_LOCAL_ONLY", "0")
    monkeypatch.setenv("OPENAI_API_KEY", "sk-test-openai")
    get_settings.cache_clear()

    def _fail_external_call(*args: Any, **kwargs: Any):
        raise AssertionError("External image provider must not be called in default local vision mode")

    monkeypatch.setattr("backend.service.ai_chat.httpx.AsyncClient", _fail_external_call)
    monkeypatch.setattr(
        "backend.service.ai_chat.analyze_local_image",
        lambda **kwargs: "Я вижу локально разобранное изображение без внешнего провайдера.",
    )

    png_1x1 = (
        b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01"
        b"\x08\x02\x00\x00\x00\x90wS\xde\x00\x00\x00\x0cIDAT\x08\x99c```\x00"
        b"\x00\x00\x04\x00\x01\xf6\x178U\x00\x00\x00\x00IEND\xaeB`\x82"
    )

    response = client.post(
        "/api/v1/ai/vision/analyze",
        files={"file": ("tiny.png", png_1x1, "image/png")},
        data={"prompt": "Опиши изображение"},
    )
    assert response.status_code == 200
    payload = response.json()
    assert payload["provider"] == "kolibri-local-vision"
    assert "локально разобранное изображение" in payload["response"]


def test_vision_uses_openai_fallback_when_configured(
    monkeypatch: pytest.MonkeyPatch,
    client: TestClient,
) -> None:
    monkeypatch.setenv("KOLIBRI_LOCAL_ONLY", "0")
    monkeypatch.setenv("KOLIBRI_VISION_MODE", "provider")
    monkeypatch.setenv("OPENAI_API_KEY", "sk-test-openai")
    get_settings.cache_clear()

    class _VisionClient:
        async def __aenter__(self):
            return self

        async def __aexit__(self, exc_type, exc, tb):
            return None

        async def post(self, *args: Any, **kwargs: Any) -> _DummyVisionResponse:
            return _DummyVisionResponse()

    monkeypatch.setattr("backend.service.ai_chat.httpx.AsyncClient", lambda *args, **kwargs: _VisionClient())

    png_1x1 = (
        b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01"
        b"\x08\x02\x00\x00\x00\x90wS\xde\x00\x00\x00\x0cIDAT\x08\x99c```\x00"
        b"\x00\x00\x04\x00\x01\xf6\x178U\x00\x00\x00\x00IEND\xaeB`\x82"
    )

    response = client.post(
        "/api/v1/ai/vision/analyze",
        files={"file": ("tiny.png", png_1x1, "image/png")},
        data={"prompt": "Опиши изображение"},
    )
    assert response.status_code == 200
    payload = response.json()
    assert payload["provider"] == "openai-vision"
    assert "тестовый объект" in payload["response"]


def test_uniqueness_latest_not_found(monkeypatch: pytest.MonkeyPatch, client: TestClient) -> None:
    class _Engine:
        def get_uniqueness_report(self):
            return None

    monkeypatch.setattr("backend.service.ai_chat.get_engine", lambda: _Engine())
    response = client.get("/api/v1/ai/quality/uniqueness")
    assert response.status_code == 404
    assert "No uniqueness proof report yet" in response.text


def test_uniqueness_run_returns_payload(monkeypatch: pytest.MonkeyPatch, client: TestClient) -> None:
    class _Engine:
        def run_uniqueness_proof(self, trigger: str):
            return {
                "run_id": "proof-1",
                "trigger": trigger,
                "started_at": 1.0,
                "finished_at": 2.0,
                "duration_ms": 1000.0,
                "score": 1.0,
                "passed": 8,
                "total": 8,
                "fingerprint": "abc123",
                "claims": ["numeric-word-encoding-64"],
                "details": [{"id": "numeric_word_encoding_64digits", "passed": True}],
            }

    monkeypatch.setattr("backend.service.ai_chat.get_engine", lambda: _Engine())
    response = client.post("/api/v1/ai/quality/uniqueness/run")
    assert response.status_code == 200
    payload = response.json()
    assert payload["run_id"] == "proof-1"
    assert payload["score"] == 1.0
    assert payload["passed"] == 8
    assert payload["total"] == 8
    assert payload["fingerprint"] == "abc123"


def test_quality_benchmark_run_includes_weighted_fields(
    monkeypatch: pytest.MonkeyPatch,
    client: TestClient,
) -> None:
    class _Engine:
        def run_quality_benchmark(self, trigger: str):
            return {
                "run_id": "bench-1",
                "trigger": trigger,
                "started_at": 1.0,
                "finished_at": 2.0,
                "duration_ms": 1000.0,
                "score": 0.9,
                "passed": 9,
                "total": 10,
                "weighted_passed": 10.5,
                "weighted_total": 11.5,
                "pass_rate": 0.9,
                "latency_p50_ms": 420.0,
                "latency_p95_ms": 980.0,
                "placeholder_rate": 0.1,
                "hallucination_proxy_rate": 0.05,
                "gates": {"overall_pass": True},
                "details": [{"id": "math_words", "passed": True, "category": "math"}],
            }

    monkeypatch.setattr("backend.service.ai_chat.get_engine", lambda: _Engine())
    response = client.post("/api/v1/ai/quality/benchmark/run")
    assert response.status_code == 200
    payload = response.json()
    assert payload["run_id"] == "bench-1"
    assert payload["score"] == 0.9
    assert payload["weighted_passed"] == 10.5
    assert payload["weighted_total"] == 11.5
    assert payload["pass_rate"] == 0.9
    assert payload["latency_p95_ms"] == 980.0
    assert payload["placeholder_rate"] == 0.1
    assert payload["hallucination_proxy_rate"] == 0.05
    assert payload["gates"]["overall_pass"] is True


def test_quality_benchmark_history_returns_trend(
    monkeypatch: pytest.MonkeyPatch,
    client: TestClient,
) -> None:
    class _Engine:
        def get_quality_benchmark_history(self, limit: int):
            assert limit == 2
            return [
                {
                    "run_id": "bench-new",
                    "trigger": "auto",
                    "score": 0.95,
                    "pass_rate": 0.9,
                    "latency_p95_ms": 900.0,
                    "gates": {"overall_pass": True},
                    "categories": [
                        {"category": "domains", "pass_rate": 0.8, "weighted_pass_rate": 0.82},
                        {"category": "math", "pass_rate": 1.0, "weighted_pass_rate": 1.0},
                    ],
                },
                {
                    "run_id": "bench-old",
                    "trigger": "auto",
                    "score": 0.8,
                    "pass_rate": 0.7,
                    "latency_p95_ms": 1200.0,
                    "gates": {"overall_pass": False},
                    "categories": [
                        {"category": "domains", "pass_rate": 0.4, "weighted_pass_rate": 0.45},
                        {"category": "math", "pass_rate": 0.8, "weighted_pass_rate": 0.8},
                    ],
                },
            ]

    monkeypatch.setattr("backend.service.ai_chat.get_engine", lambda: _Engine())
    response = client.get("/api/v1/ai/quality/benchmark/history?limit=2")
    assert response.status_code == 200
    payload = response.json()
    assert payload["count"] == 2
    assert payload["items"][0]["run_id"] == "bench-new"
    assert payload["items"][1]["run_id"] == "bench-old"
    assert payload["trend"]["gate_failures"] == 1
    assert payload["trend"]["score_delta"] == pytest.approx(0.15)
    assert payload["trend"]["latency_p95_ms_delta"] == pytest.approx(-300.0)
    assert payload["trend"]["category_pass_rate_avg"]["domains"] == pytest.approx(0.6)
    assert payload["trend"]["category_weighted_pass_rate_avg"]["domains"] == pytest.approx(0.635)


def test_ai_demo_learn_text_returns_report_and_chat(
    monkeypatch: pytest.MonkeyPatch,
    client: TestClient,
) -> None:
    monkeypatch.setattr(
        "backend.service.ai_chat.run_text_ingest_demo",
        lambda **kwargs: {
            "demo": {
                "domain_delta": [{"domain": "biology", "before": 0, "after": 1, "delta": 1}],
                "knowledge_delta": {
                    "documents_delta": 1,
                    "single_hit_delta": 0.05,
                    "swarm_hit_delta": 0.35,
                },
                "comparison_summary": {
                    "documents_before": 1,
                    "documents_after": 2,
                    "documents_delta": 1,
                    "single_hit_before": 0.1,
                    "single_hit_after": 0.15,
                    "isolated_hit_before": 0.2,
                    "isolated_hit_after": 0.3,
                    "swarm_hit_before": 0.25,
                    "swarm_hit_after": 0.6,
                    "swarm_vs_single_before": 0.15,
                    "swarm_vs_single_after": 0.45,
                    "swarm_vs_isolated_before": 0.05,
                    "swarm_vs_isolated_after": 0.3,
                    "focus_domain": "biology",
                    "focus_domain_documents_delta": 1,
                    "focus_domain_single_hit_delta": 0.1,
                    "focus_domain_swarm_hit_delta": 0.5,
                    "focus_domain_advantage_delta": 0.4,
                },
                "domain_score_delta": [
                    {
                        "domain": "biology",
                        "documents_before": 0,
                        "documents_after": 1,
                        "documents_delta": 1,
                        "single_hit_delta": 0.1,
                        "isolated_hit_delta": 0.2,
                        "swarm_hit_delta": 0.5,
                        "swarm_vs_single_delta_change": 0.4,
                        "swarm_vs_isolated_delta_change": 0.3,
                    }
                ],
            }
        },
    )

    async def _fake_run_engine_chat(req):
        assert req.message == "что такое биология"
        return type(
            "_ChatResult",
            (),
            {
                "response": "Биология — наука о живых организмах.",
                "confidence": 0.91,
                "conversation_id": "conv-demo",
                "sources": ["c-core-formula"],
                "knowledge_hits": 1,
                "method": "c-core-formula",
                "duration_ms": 42.0,
                "model_available": True,
                "formula_data": None,
                "graph_stats": None,
                "cognitive": None,
                "self_check": None,
                "client_id": "demo-client",
            },
        )()

    monkeypatch.setattr("backend.service.ai_chat._run_engine_chat", _fake_run_engine_chat)

    response = client.post(
        "/api/v1/ai/demo/learn/text",
        json={
            "title": "Биология",
            "source": "pytest",
            "category": "biology",
            "text": "Биология изучает живые организмы, их строение, функции и развитие.",
            "question": "что такое биология",
        },
    )
    assert response.status_code == 200
    payload = response.json()
    assert "biology: +1" in payload["report"]
    assert "Сравнение усвоения" in payload["report"]
    assert "1 узел 0.100 -> 0.150" in payload["report"]
    assert "10 узлов 0.250 -> 0.600" in payload["report"]
    assert "рой улучшил hit_ratio" in payload["report"]
    assert payload["chat"]["response"] == "Биология — наука о живых организмах."
    assert payload["chat"]["method"] == "c-core-formula"
    assert payload["demo"]["domain_delta"][0]["domain"] == "biology"


def test_ai_demo_learn_text_can_upgrade_chat_with_c_core(
    monkeypatch: pytest.MonkeyPatch,
    client: TestClient,
) -> None:
    monkeypatch.setattr(
        "backend.service.ai_chat.run_text_ingest_demo",
        lambda **kwargs: {"demo": {"domain_delta": [], "comparison_summary": None, "domain_score_delta": []}},
    )

    async def _fake_run_engine_chat(req):
        return type(
            "_ChatResult",
            (),
            {
                "response": "Старый fallback.",
                "confidence": 0.31,
                "conversation_id": "conv-demo",
                "sources": ["web-reference"],
                "knowledge_hits": 0,
                "method": "web-reference",
                "duration_ms": 30.0,
                "model_available": True,
                "formula_data": None,
                "graph_stats": None,
                "cognitive": None,
                "self_check": None,
                "client_id": "demo-client",
            },
        )()

    class _FakeCInference:
        available = True

        def query(self, message: str, strategy: str = "formula"):
            assert strategy == "formula"
            return {
                "response": "Тестовый домен Codex — знание о сравнении одного узла и роя из десяти узлов.",
                "confidence": 0.88,
                "knowledge_hits": 1,
            }

    class _FakeEngine:
        def __init__(self) -> None:
            self.c_inference = _FakeCInference()

        def _is_valid_c_formula_answer(self, query: str, payload):
            return bool(payload and "Тестовый домен Codex" in str(payload.get("response", "")))

    monkeypatch.setattr("backend.service.ai_chat._run_engine_chat", _fake_run_engine_chat)
    monkeypatch.setattr("backend.service.ai_chat.get_engine", lambda: _FakeEngine())

    response = client.post(
        "/api/v1/ai/demo/learn/text",
        json={
            "title": "Тестовый домен Codex",
            "source": "pytest",
            "category": "manual",
            "text": "Тестовый домен Codex описывает сравнение одного узла и роя.",
            "question": "что такое тестовый домен codex",
        },
    )
    assert response.status_code == 200
    payload = response.json()
    assert payload["chat"]["method"] == "c-core-formula"
    assert "Тестовый домен Codex" in payload["chat"]["response"]


def test_ai_demo_learn_text_can_fallback_to_demo_source_grounding(
    monkeypatch: pytest.MonkeyPatch,
    client: TestClient,
) -> None:
    monkeypatch.setattr(
        "backend.service.ai_chat.run_text_ingest_demo",
        lambda **kwargs: {"demo": {"domain_delta": [], "comparison_summary": None, "domain_score_delta": []}},
    )

    async def _fake_run_engine_chat(req):
        return type(
            "_ChatResult",
            (),
            {
                "response": "Старый fallback.",
                "confidence": 0.31,
                "conversation_id": "conv-demo",
                "sources": ["web-reference"],
                "knowledge_hits": 0,
                "method": "web-reference",
                "duration_ms": 30.0,
                "model_available": True,
                "formula_data": None,
                "graph_stats": None,
                "cognitive": None,
                "self_check": None,
                "client_id": "demo-client",
            },
        )()

    class _FakeCInference:
        available = True

        def query(self, message: str, strategy: str = "formula"):
            return None

    class _FakeEngine:
        def __init__(self) -> None:
            self.c_inference = _FakeCInference()

        def _is_valid_c_formula_answer(self, query: str, payload):
            return False

    monkeypatch.setattr("backend.service.ai_chat._run_engine_chat", _fake_run_engine_chat)
    monkeypatch.setattr("backend.service.ai_chat.get_engine", lambda: _FakeEngine())

    response = client.post(
        "/api/v1/ai/demo/learn/text",
        json={
            "title": "Тестовый домен Codex",
            "source": "pytest",
            "category": "manual",
            "text": "Описывает сравнение одного узла и роя из десяти узлов.",
            "question": "что такое тестовый домен codex",
        },
    )
    assert response.status_code == 200
    payload = response.json()
    assert payload["chat"]["method"] == "demo-source-grounded"
    assert payload["chat"]["response"].startswith("Тестовый домен Codex — Описывает сравнение одного узла")
