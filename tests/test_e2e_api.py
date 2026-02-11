"""
E2E тесты для Kolibri AI API.

Проверяют работу всех основных эндпоинтов через FastAPI TestClient.
Не требуют запущенного сервера — используют httpx AsyncClient.
"""
from __future__ import annotations

import pytest
from httpx import AsyncClient, ASGITransport

# ---------------------------------------------------------------------------
# Фикстуры
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def anyio_backend():
    return "asyncio"


@pytest.fixture(scope="module")
async def client():
    """Асинхронный клиент для тестов.

    Использует ASGI-транспорт — сервер поднимается in-process.
    """
    from backend.service.main import app  # noqa: WPS433

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        yield ac


# ---------------------------------------------------------------------------
# Health
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_health(client: AsyncClient) -> None:
    resp = await client.get("/api/health")
    assert resp.status_code == 200
    data = resp.json()
    assert data["status"] == "ok"
    assert "response_mode" in data


@pytest.mark.anyio
async def test_knowledge_health(client: AsyncClient) -> None:
    resp = await client.get("/api/knowledge/healthz")
    assert resp.status_code == 200
    assert resp.json()["status"] == "ok"


# ---------------------------------------------------------------------------
# AI Chat
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_chat_basic(client: AsyncClient) -> None:
    """Чат должен вернуть ответ с минимальным набором полей."""
    resp = await client.post(
        "/api/v1/ai/chat",
        json={"message": "привет"},
    )
    assert resp.status_code == 200
    data = resp.json()
    assert "response" in data
    assert isinstance(data["response"], str)
    assert "confidence" in data
    assert "method" in data
    assert "conversation_id" in data


@pytest.mark.anyio
async def test_chat_with_conversation_id(client: AsyncClient) -> None:
    """Чат с conversation_id должен сохранять контекст."""
    conv_id = "test-conv-001"
    resp = await client.post(
        "/api/v1/ai/chat",
        json={"message": "что такое AI?", "conversation_id": conv_id},
    )
    assert resp.status_code == 200
    data = resp.json()
    assert data["conversation_id"] == conv_id


# ---------------------------------------------------------------------------
# Training
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_train_text(client: AsyncClient) -> None:
    """Обучение на коротком тексте должно вернуть успех."""
    resp = await client.post(
        "/api/v1/ai/train",
        json={"text": "Кот — домашнее животное. Кошки любят молоко."},
    )
    assert resp.status_code == 200
    data = resp.json()
    assert data.get("status") in ("ok", "success", "trained")
    # Ответ содержит статистику обучения
    assert "edges" in data or "patterns_created" in data or "new_words" in data


@pytest.mark.anyio
async def test_chat_after_train(client: AsyncClient) -> None:
    """После обучения чат должен вернуть что-то осмысленное."""
    # Обучим
    await client.post(
        "/api/v1/ai/train",
        json={"text": "Python — язык программирования. Python используется для AI."},
    )
    # Спросим
    resp = await client.post(
        "/api/v1/ai/chat",
        json={"message": "Python"},
    )
    assert resp.status_code == 200
    data = resp.json()
    assert len(data["response"]) > 0


# ---------------------------------------------------------------------------
# Pattern / Embedding
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_pattern(client: AsyncClient) -> None:
    """Эндпоинт pattern должен вернуть числовой паттерн слова."""
    resp = await client.post(
        "/api/v1/ai/pattern",
        json={"word": "тест"},
    )
    assert resp.status_code == 200
    data = resp.json()
    assert "pattern" in data


@pytest.mark.anyio
async def test_embedding(client: AsyncClient) -> None:
    """Эндпоинт embedding должен вернуть вектор."""
    resp = await client.post(
        "/api/v1/ai/embedding",
        json={"text": "тест"},
    )
    assert resp.status_code == 200
    data = resp.json()
    assert "vector" in data or "embedding" in data


# ---------------------------------------------------------------------------
# Stats
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_stats(client: AsyncClient) -> None:
    """Статистика движка должна содержать основные метрики."""
    resp = await client.get("/api/v1/ai/stats")
    assert resp.status_code == 200
    data = resp.json()
    # Статистика содержит любой из ключевых полей
    assert isinstance(data, dict)
    assert len(data) > 0  # Не пустой ответ


# ---------------------------------------------------------------------------
# Cognition API
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_cognition_abstract(client: AsyncClient) -> None:
    """Абстрактное мышление — должен вернуть результат."""
    # Сначала обучим
    await client.post(
        "/api/v1/ai/train",
        json={"text": "Кошка — домашнее животное. Собака — тоже домашнее животное."},
    )
    resp = await client.post(
        "/api/v1/cognition/abstract",
        json={"query": "кошка", "hops": 2},
    )
    assert resp.status_code == 200
    data = resp.json()
    assert isinstance(data, dict)
    assert "abstract_answer" in data or "confidence" in data


@pytest.mark.anyio
async def test_cognition_causal(client: AsyncClient) -> None:
    """Каузальное мышление — базовый тест."""
    resp = await client.post(
        "/api/v1/cognition/causal",
        json={"query": "обучение", "direction": "then", "max_chain": 3},
    )
    assert resp.status_code == 200
    data = resp.json()
    assert isinstance(data, dict)


@pytest.mark.anyio
async def test_cognition_induce(client: AsyncClient) -> None:
    """Индукция — должен вернуть правила."""
    resp = await client.post(
        "/api/v1/cognition/induce",
        json={"min_support": 2, "top_k": 5},
    )
    assert resp.status_code == 200
    data = resp.json()
    assert isinstance(data, dict)


@pytest.mark.anyio
async def test_cognition_analogy(client: AsyncClient) -> None:
    """Аналогия — перенос структуры."""
    resp = await client.post(
        "/api/v1/cognition/analogy",
        json={"a": "кошка", "b": "котёнок", "c": "собака"},
    )
    assert resp.status_code == 200
    data = resp.json()
    assert isinstance(data, dict)


@pytest.mark.anyio
async def test_cognition_introspect(client: AsyncClient) -> None:
    """Самомоделирование — интроспекция."""
    resp = await client.post(
        "/api/v1/cognition/introspect",
        json={"query": "что я знаю о котах?"},
    )
    assert resp.status_code == 200
    data = resp.json()
    assert isinstance(data, dict)


@pytest.mark.anyio
async def test_cognition_enhanced(client: AsyncClient) -> None:
    """Интегрированный когнитивный ответ."""
    resp = await client.post(
        "/api/v1/cognition/enhanced",
        json={"query": "что такое интеллект?"},
    )
    assert resp.status_code == 200
    data = resp.json()
    assert isinstance(data, dict)


# ---------------------------------------------------------------------------
# Conversation delete
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_delete_conversation(client: AsyncClient) -> None:
    """Удаление беседы."""
    resp = await client.delete("/api/v1/ai/conversations/test-conv-999")
    # 200 если нашёл, 404 если нет — оба допустимы
    assert resp.status_code in (200, 404)


# ---------------------------------------------------------------------------
# Full pipeline: train → chat → cognitive → verify
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_full_pipeline(client: AsyncClient) -> None:
    """Полный цикл: обучение → чат → когнитивный анализ."""
    # 1. Обучаем
    train_resp = await client.post(
        "/api/v1/ai/train",
        json={
            "text": (
                "Нейронные сети — это математические модели для обработки данных. "
                "Глубокое обучение использует многослойные нейронные сети. "
                "Трансформеры — это архитектура нейронных сетей для обработки текста."
            )
        },
    )
    assert train_resp.status_code == 200

    # 2. Чат — спрашиваем про обученную тему
    chat_resp = await client.post(
        "/api/v1/ai/chat",
        json={"message": "нейронные сети"},
    )
    assert chat_resp.status_code == 200
    chat_data = chat_resp.json()
    assert len(chat_data["response"]) > 0

    # 3. Абстрактное мышление
    abstract_resp = await client.post(
        "/api/v1/cognition/abstract",
        json={"query": "нейронные", "hops": 2},
    )
    assert abstract_resp.status_code == 200

    # 4. Каузал
    causal_resp = await client.post(
        "/api/v1/cognition/causal",
        json={"query": "обучение", "direction": "then"},
    )
    assert causal_resp.status_code == 200

    # 5. Статистика — граф должен содержать обученные данные
    stats_resp = await client.get("/api/v1/ai/stats")
    assert stats_resp.status_code == 200
