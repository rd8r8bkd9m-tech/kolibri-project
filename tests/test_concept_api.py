from __future__ import annotations

import pytest
from httpx import ASGITransport, AsyncClient


@pytest.fixture(scope="module")
def anyio_backend():
    return "asyncio"


@pytest.fixture(scope="module")
async def client():
    from backend.service.main import app  # noqa: WPS433

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        yield ac


@pytest.mark.anyio
async def test_concept_run_returns_integrated_pipeline(client: AsyncClient) -> None:
    response = await client.post(
        "/api/v1/concept/run",
        json={
            "query": "что такое python",
            "corpus": [
                "Python язык программирования для автоматизации и анализа данных.",
                "Язык программирования Python часто используют для AI и обучения моделей.",
                "Автоматизация и анализ данных требуют инструментов, библиотек и кода.",
            ],
            "peer_count": 3,
            "swarm_rounds": 1,
            "formula_generations": 3,
            "cognition_depth": 2,
            "seed": 42,
        },
    )

    assert response.status_code == 200
    payload = response.json()

    assert payload["query"] == "что такое python"
    assert payload["intent"]["source"] == "agent://concept/runtime"
    assert payload["decimal_layer"]["decoded_text"] == "что такое python"
    assert payload["knowledge_layer"]["trained_documents"] == 3
    assert payload["formula_layer"]["semantic_pairs"] > 0
    assert payload["genome_layer"]["valid"] is True
    assert payload["swarm_layer"]["peer_count"] == 3
    assert isinstance(payload["human_response"], str)
    assert payload["human_response"]
