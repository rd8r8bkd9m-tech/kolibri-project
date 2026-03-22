from __future__ import annotations

import sys
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))


@pytest.fixture()
def client(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> TestClient:
    seed_dir = tmp_path / "seed"
    seed_dir.mkdir(parents=True)
    (seed_dir / "math.txt").write_text("# Математика\n\nМатематика изучает числа и структуры.\n", encoding="utf-8")

    live_dir = tmp_path / "live-memory"
    latest_demo_path = tmp_path / "swarm" / "latest_demo.json"
    monkeypatch.setenv("KOLIBRI_ENABLE_SWARM_RUNTIME", "0")
    monkeypatch.setenv("KOLIBRI_LIVE_FORMULA_MEMORY_PATH", str(live_dir))
    monkeypatch.setenv("KOLIBRI_SWARM_LATEST_DEMO_PATH", str(latest_demo_path))

    import backend.service.swarm_live_memory as swarm_live_memory
    import backend.service.swarm_runtime_api as swarm_runtime_api

    monkeypatch.setattr(swarm_live_memory, "get_seed_formula_memory_dir", lambda: seed_dir)
    swarm_runtime_api._manager = None

    from backend.service.main import app

    with TestClient(app) as test_client:
        yield test_client

    swarm_runtime_api._manager = None


def test_swarm_runtime_status_exposes_live_memory(client: TestClient) -> None:
    response = client.get("/api/v1/swarm/runtime/status")
    assert response.status_code == 200
    payload = response.json()
    assert payload["live_memory_path"]
    assert payload["seed_memory_path"]
    assert payload["live_memory_document_count"] >= 1
    assert payload["live_memory_domains"]
    assert payload["live_memory_domains"][0]["documents"] >= 1
    assert payload["last_ingest_domain_delta"] == []
    assert payload["last_knowledge_refresh_delta"] is None
    assert payload["latest_demo"] is None


def test_swarm_runtime_ingest_text_updates_live_memory(client: TestClient) -> None:
    import backend.service.swarm_runtime_api as swarm_runtime_api

    manager = swarm_runtime_api.get_swarm_runtime_manager()
    scheduled = {"count": 0}

    def _schedule_refresh(timeout_sec: int = 180):
        scheduled["count"] += 1
        return manager.status_without_autostart()

    original_schedule = manager.schedule_refresh
    manager.schedule_refresh = _schedule_refresh  # type: ignore[method-assign]
    before = client.get("/api/v1/swarm/runtime/status").json()["live_memory_document_count"]

    try:
        response = client.post(
            "/api/v1/swarm/runtime/ingest/text",
            json={
                "title": "Философия",
                "source": "pytest",
                "category": "tests",
                "text": "Философия исследует общие принципы бытия, познания и мышления.",
            },
        )
        assert response.status_code == 200
        payload = response.json()
        assert payload["ingest"]["kind"] == "text"
        assert payload["ingest"]["saved_documents"] == 1
        assert payload["ingest"]["domain_delta"]
        assert payload["ingest"]["domain_delta"][0]["delta"] >= 1
        assert payload["live_memory_document_count"] == before + 1
        assert payload["last_ingest_kind"] == "text"
        assert payload["last_ingest_domain_delta"]
        assert scheduled["count"] == 1
    finally:
        manager.schedule_refresh = original_schedule  # type: ignore[method-assign]


def test_ingest_text_document_anchors_title_for_definition_queries(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    import backend.service.swarm_live_memory as swarm_live_memory

    monkeypatch.setenv("KOLIBRI_LIVE_FORMULA_MEMORY_PATH", str(tmp_path / "live-memory"))
    monkeypatch.setattr(swarm_live_memory, "get_seed_formula_memory_dir", lambda: tmp_path / "seed")

    target = swarm_live_memory.ingest_text_document(
        "Описывает сравнение одного узла и роя из десяти узлов.",
        title="Тестовый домен Codex",
        source="pytest",
        category="manual",
    )

    content = target.read_text(encoding="utf-8")
    assert content.startswith("# Тестовый домен Codex")
    assert "Тестовый домен Codex — Описывает сравнение одного узла и роя из десяти узлов." in content


def test_swarm_runtime_ingest_url_uses_trainer_bridge(
    client: TestClient,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    import backend.service.swarm_runtime_api as swarm_runtime_api

    monkeypatch.setattr(
        swarm_runtime_api,
        "ingest_urls_via_trainer",
        lambda urls, **kwargs: {
            "command": ["kolibri_formula_trainer"],
            "stdout": "",
            "stderr": "",
            "saved_documents": len(urls),
            "live_memory_path": "/tmp/live",
            "live_memory_document_count": 5,
            "domain_delta": [{"domain": "root", "before": 4, "after": 5, "delta": 1}],
        },
    )

    response = client.post(
        "/api/v1/swarm/runtime/ingest/url",
        json={"url": "https://example.org/knowledge"},
    )
    assert response.status_code == 200
    payload = response.json()
    assert payload["ingest"]["kind"] == "url"
    assert payload["ingest"]["saved_documents"] == 1
    assert payload["last_ingest_kind"] == "url"


def test_swarm_runtime_refresh_forces_recalculation(
    client: TestClient,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    import backend.service.swarm_runtime_api as swarm_runtime_api

    called = {"refresh": 0}

    class _Manager:
        def force_refresh(self, timeout_sec: int = 180):
            called["refresh"] += 1
            return {"running": True, "knowledge_running": True, "latest": {"forced": True}}

    monkeypatch.setattr(swarm_runtime_api, "get_swarm_runtime_manager", lambda: _Manager())

    response = client.post("/api/v1/swarm/runtime/refresh")
    assert response.status_code == 200
    payload = response.json()
    assert called["refresh"] == 1
    assert payload["latest"]["forced"] is True


def test_compute_knowledge_refresh_delta_tracks_growth() -> None:
    import backend.service.swarm_runtime_api as swarm_runtime_api

    delta = swarm_runtime_api._compute_knowledge_refresh_delta(
        {
            "timestamp": 100.0,
            "total_documents": 10,
            "single": {"hit_ratio": 0.1},
            "isolated_final": {"hit_ratio": 0.2},
            "swarm_final": {"hit_ratio": 0.4},
            "comparison": {
                "swarm_vs_single_delta": 0.3,
                "swarm_vs_isolated_delta": 0.2,
            },
        },
        {
            "timestamp": 130.0,
            "total_documents": 13,
            "single": {"hit_ratio": 0.15},
            "isolated_final": {"hit_ratio": 0.25},
            "swarm_final": {"hit_ratio": 0.6},
            "comparison": {
                "swarm_vs_single_delta": 0.45,
                "swarm_vs_isolated_delta": 0.35,
            },
        },
    )

    assert delta == {
        "from_timestamp": 100.0,
        "to_timestamp": 130.0,
        "documents_delta": 3,
        "single_hit_delta": 0.05,
        "isolated_hit_delta": 0.05,
        "swarm_hit_delta": 0.2,
        "swarm_vs_single_delta_change": 0.15,
        "swarm_vs_isolated_delta_change": 0.15,
    }


def test_compute_domain_score_delta_filters_focus_domains() -> None:
    import backend.service.swarm_runtime_api as swarm_runtime_api

    delta = swarm_runtime_api._compute_domain_score_delta(
        {
            "domain_scores": [
                {
                    "domain": "math",
                    "documents": 1,
                    "single_hit_ratio": 0.1,
                    "isolated_hit_ratio": 0.15,
                    "swarm_hit_ratio": 0.2,
                    "swarm_vs_single_delta": 0.1,
                    "swarm_vs_isolated_delta": 0.05,
                },
                {
                    "domain": "medicine",
                    "documents": 2,
                    "single_hit_ratio": 0.2,
                    "isolated_hit_ratio": 0.25,
                    "swarm_hit_ratio": 0.4,
                    "swarm_vs_single_delta": 0.2,
                    "swarm_vs_isolated_delta": 0.15,
                },
            ]
        },
        {
            "domain_scores": [
                {
                    "domain": "math",
                    "documents": 2,
                    "single_hit_ratio": 0.3,
                    "isolated_hit_ratio": 0.35,
                    "swarm_hit_ratio": 0.55,
                    "swarm_vs_single_delta": 0.25,
                    "swarm_vs_isolated_delta": 0.2,
                },
                {
                    "domain": "medicine",
                    "documents": 2,
                    "single_hit_ratio": 0.2,
                    "isolated_hit_ratio": 0.25,
                    "swarm_hit_ratio": 0.4,
                    "swarm_vs_single_delta": 0.2,
                    "swarm_vs_isolated_delta": 0.15,
                },
            ]
        },
        focus_domains={"math"},
    )

    assert delta == [
        {
            "domain": "math",
            "documents_before": 1,
            "documents_after": 2,
            "documents_delta": 1,
            "single_hit_delta": 0.2,
            "isolated_hit_delta": 0.2,
            "swarm_hit_delta": 0.35,
            "swarm_vs_single_delta_change": 0.15,
            "swarm_vs_isolated_delta_change": 0.15,
        }
    ]


def test_swarm_runtime_demo_ingest_text_returns_before_after_report(
    client: TestClient,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    import backend.service.swarm_runtime_api as swarm_runtime_api

    before_status = {
        "live_memory_document_count": 1,
        "live_memory_domains": [{"domain": "root", "documents": 1}],
        "last_knowledge_refresh_delta": None,
        "latest_knowledge": {
            "timestamp": 100.0,
            "total_documents": 4,
            "single": {"hit_ratio": 0.2},
            "isolated_final": {"hit_ratio": 0.3},
            "swarm_final": {"hit_ratio": 0.45},
            "comparison": {
                "swarm_vs_single_delta": 0.25,
                "swarm_vs_isolated_delta": 0.15,
            },
            "domain_scores": [
                {
                    "domain": "root",
                    "documents": 1,
                    "single_hit_ratio": 0.2,
                    "isolated_hit_ratio": 0.25,
                    "swarm_hit_ratio": 0.35,
                    "swarm_vs_single_delta": 0.15,
                    "swarm_vs_isolated_delta": 0.1,
                }
            ],
        },
    }
    after_status = {
        "live_memory_document_count": 2,
        "live_memory_domains": [
            {"domain": "root", "documents": 1},
            {"domain": "biology", "documents": 1},
        ],
        "last_knowledge_refresh_delta": {"documents_delta": 1},
        "latest_knowledge": {
            "timestamp": 140.0,
            "total_documents": 5,
            "single": {"hit_ratio": 0.25},
            "isolated_final": {"hit_ratio": 0.35},
            "swarm_final": {"hit_ratio": 0.65},
            "comparison": {
                "swarm_vs_single_delta": 0.4,
                "swarm_vs_isolated_delta": 0.3,
            },
            "domain_scores": [
                {
                    "domain": "root",
                    "documents": 1,
                    "single_hit_ratio": 0.2,
                    "isolated_hit_ratio": 0.25,
                    "swarm_hit_ratio": 0.35,
                    "swarm_vs_single_delta": 0.15,
                    "swarm_vs_isolated_delta": 0.1,
                },
                {
                    "domain": "biology",
                    "documents": 1,
                    "single_hit_ratio": 0.25,
                    "isolated_hit_ratio": 0.35,
                    "swarm_hit_ratio": 0.65,
                    "swarm_vs_single_delta": 0.4,
                    "swarm_vs_isolated_delta": 0.3,
                },
            ],
        },
    }

    class _Manager:
        def __init__(self) -> None:
            self.recorded: list[tuple[str, list[dict[str, int | str]]]] = []
            self.timeout_sec = 0
            self.latest_demo: dict[str, object] | None = None

        def status_without_autostart(self) -> dict[str, object]:
            return before_status

        def record_ingest_delta(self, kind: str, domain_delta: list[dict[str, int | str]]) -> None:
            self.recorded.append((kind, domain_delta))

        def record_demo_snapshot(self, payload: dict[str, object]) -> None:
            self.latest_demo = payload

        def force_refresh(self, timeout_sec: int = 180) -> dict[str, object]:
            self.timeout_sec = timeout_sec
            return after_status

    manager = _Manager()
    monkeypatch.setattr(swarm_runtime_api, "get_swarm_runtime_manager", lambda: manager)

    response = client.post(
        "/api/v1/swarm/runtime/demo/ingest/text",
        json={
            "title": "Биология",
            "source": "pytest",
            "category": "biology",
            "text": "Биология изучает живые организмы, их строение, функции и развитие.",
            "refresh_timeout_sec": 45,
        },
    )
    assert response.status_code == 200
    payload = response.json()

    assert payload["demo"]["kind"] == "text"
    assert payload["demo"]["saved_documents"] == 1
    assert payload["demo"]["before"]["live_memory_document_count"] == 1
    assert payload["demo"]["after"]["live_memory_document_count"] == 2
    assert payload["demo"]["knowledge_delta"]["documents_delta"] == 1
    assert payload["demo"]["knowledge_delta"]["swarm_vs_single_delta_change"] == 0.15
    assert payload["demo"]["domain_delta"] == [
        {"domain": "biology", "before": 0, "after": 1, "delta": 1}
    ]
    assert payload["demo"]["domain_score_delta"] == [
        {
            "domain": "biology",
            "documents_before": 0,
            "documents_after": 1,
            "documents_delta": 1,
            "single_hit_delta": 0.25,
            "isolated_hit_delta": 0.35,
            "swarm_hit_delta": 0.65,
            "swarm_vs_single_delta_change": 0.4,
            "swarm_vs_isolated_delta_change": 0.3,
        }
    ]
    assert payload["demo"]["comparison_summary"] == {
        "documents_before": 4,
        "documents_after": 5,
        "documents_delta": 1,
        "single_hit_before": 0.2,
        "single_hit_after": 0.25,
        "isolated_hit_before": 0.3,
        "isolated_hit_after": 0.35,
        "swarm_hit_before": 0.45,
        "swarm_hit_after": 0.65,
        "swarm_vs_single_before": 0.25,
        "swarm_vs_single_after": 0.4,
        "swarm_vs_isolated_before": 0.15,
        "swarm_vs_isolated_after": 0.3,
        "focus_domain": "biology",
        "focus_domain_documents_delta": 1,
        "focus_domain_single_hit_delta": 0.25,
        "focus_domain_swarm_hit_delta": 0.65,
        "focus_domain_advantage_delta": 0.4,
    }
    assert payload["latest_demo"] == {
        "created_at": payload["latest_demo"]["created_at"],
        "title": "Биология",
        "source": "pytest",
        "category": "biology",
        "saved_documents": 1,
        "message": "Текст добавлен в живую память, рой принудительно пересчитан, before/after отчёт собран.",
        "domain_delta": [{"domain": "biology", "before": 0, "after": 1, "delta": 1}],
        "knowledge_delta": {
            "from_timestamp": 100.0,
            "to_timestamp": 140.0,
            "documents_delta": 1,
            "single_hit_delta": 0.05,
            "isolated_hit_delta": 0.05,
            "swarm_hit_delta": 0.2,
            "swarm_vs_single_delta_change": 0.15,
            "swarm_vs_isolated_delta_change": 0.15,
        },
        "comparison_summary": {
            "documents_before": 4,
            "documents_after": 5,
            "documents_delta": 1,
            "single_hit_before": 0.2,
            "single_hit_after": 0.25,
            "isolated_hit_before": 0.3,
            "isolated_hit_after": 0.35,
            "swarm_hit_before": 0.45,
            "swarm_hit_after": 0.65,
            "swarm_vs_single_before": 0.25,
            "swarm_vs_single_after": 0.4,
            "swarm_vs_isolated_before": 0.15,
            "swarm_vs_isolated_after": 0.3,
            "focus_domain": "biology",
            "focus_domain_documents_delta": 1,
            "focus_domain_single_hit_delta": 0.25,
            "focus_domain_swarm_hit_delta": 0.65,
            "focus_domain_advantage_delta": 0.4,
        },
    }
    assert len(payload["demo"]["saved_paths"]) == 1
    assert payload["demo"]["saved_paths"][0].endswith(".txt")
    assert manager.recorded == [("text", [{"domain": "biology", "before": 0, "after": 1, "delta": 1}])]
    assert manager.timeout_sec == 45


def test_swarm_runtime_status_reads_persisted_latest_demo(
    client: TestClient,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    import backend.service.swarm_runtime_api as swarm_runtime_api

    manager = swarm_runtime_api.get_swarm_runtime_manager()
    monkeypatch.setattr(manager, "force_refresh", lambda timeout_sec=180: manager.status_without_autostart())

    demo_response = client.post(
        "/api/v1/swarm/runtime/demo/ingest/text",
        json={
            "title": "Тестовый домен",
            "source": "pytest",
            "category": "tests",
            "text": "Тестовый домен описывает сценарий сравнения одного узла и роя из десяти узлов.",
            "refresh_timeout_sec": 45,
        },
    )
    assert demo_response.status_code == 200
    latest_demo = demo_response.json()["latest_demo"]

    status_response = client.get("/api/v1/swarm/runtime/status")
    assert status_response.status_code == 200
    payload = status_response.json()
    assert payload["latest_demo"] is not None
    assert payload["latest_demo"]["title"] == "Тестовый домен"
    assert payload["latest_demo"]["category"] == "tests"
    assert payload["latest_demo"]["comparison_summary"] == latest_demo["comparison_summary"]


def test_swarm_runtime_kpack_export_creates_downloadable_pack(client: TestClient) -> None:
    response = client.post(
        "/api/v1/swarm/runtime/kpack/export",
        json={
            "package_id": "kolibri.math.demo",
            "title": "Математика demo",
            "domains": ["root"],
            "default_query": "что такое математика",
        },
    )
    assert response.status_code == 200
    payload = response.json()
    assert payload["filename"].endswith(".kpack")
    assert payload["documents"] >= 1
    download = client.get(payload["download_url"])
    assert download.status_code == 200
    assert download.headers["content-type"].startswith("application/zip")


def test_swarm_runtime_kpack_import_updates_live_memory(
    client: TestClient,
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> None:
    import backend.service.swarm_runtime_api as swarm_runtime_api
    from backend.service.kpack import export_kpack

    source = tmp_path / "source"
    (source / "history").mkdir(parents=True)
    (source / "history" / "0001.txt").write_text("# История\n\nИстория изучает прошлое человечества.\n", encoding="utf-8")
    pack = export_kpack(
        source_root=source,
        output_path=tmp_path / "history.kpack",
        package_id="kolibri.history.demo",
        title="История demo",
        domains=["history"],
    )

    manager = swarm_runtime_api.get_swarm_runtime_manager()
    forced = {"count": 0}

    def _force_refresh(timeout_sec: int = 180):
        forced["count"] += 1
        return manager.status_without_autostart()

    original_force = manager.force_refresh
    manager.force_refresh = _force_refresh  # type: ignore[method-assign]
    try:
        with open(pack["path"], "rb") as handle:
            response = client.post(
                "/api/v1/swarm/runtime/kpack/import",
                data={"refresh": "true", "refresh_timeout_sec": "45"},
                files={"file": ("history.kpack", handle, "application/zip")},
            )
        assert response.status_code == 200
        payload = response.json()
        assert payload["import"]["imported_documents"] == 1
        assert payload["import"]["domain_delta"] == [
            {"domain": "history", "before": 0, "after": 1, "delta": 1}
        ]
        assert payload["last_ingest_kind"] == "kpack"
        assert forced["count"] == 1
    finally:
        manager.force_refresh = original_force  # type: ignore[method-assign]
