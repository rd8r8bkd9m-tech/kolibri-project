"""Тесты crawler.py — API для краулинга и управления моделями."""
from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

import pytest
from fastapi.testclient import TestClient

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from backend.service.main import app


@pytest.fixture()
def client() -> TestClient:
    return TestClient(app)


class TestCrawlerAPI:
    """Тесты эндпоинтов crawler."""

    def test_crawl_status(self, client: TestClient) -> None:
        """Статус краулинга без активной задачи."""
        resp = client.get("/api/v1/crawl/status")
        assert resp.status_code == 200
        data = resp.json()
        assert isinstance(data, dict)

    def test_model_list(self, client: TestClient) -> None:
        """Список моделей."""
        resp = client.get("/api/v1/model/list")
        assert resp.status_code == 200
        data = resp.json()
        assert isinstance(data, (list, dict))

    def test_model_stats(self, client: TestClient) -> None:
        """Статистика модели."""
        resp = client.get("/api/v1/model/stats")
        assert resp.status_code == 200
        data = resp.json()
        assert isinstance(data, dict)

    def test_delete_nonexistent_model(self, client: TestClient) -> None:
        """Удаление несуществующей модели."""
        resp = client.delete("/api/v1/model/nonexistent_model_xyz123.klm")
        assert resp.status_code in (404, 400, 200)

    def test_terminal_exec_safe(self, client: TestClient) -> None:
        """Безопасная терминальная команда."""
        resp = client.post(
            "/api/v1/terminal/exec",
            json={"command": "echo test"},
        )
        assert resp.status_code in (200, 400, 403)


class TestCrawlerParseOutput:
    """Тесты парсинга вывода trainer бинарника."""

    def test_parse_trainer_output(self) -> None:
        from backend.service.crawler import _parse_trainer_output

        sample = """
[Колибри Краулер] Начало обхода: https://example.com
[✓] Загружено: https://example.com → 2048 символов
[✓] Паттернов в модели: 1500
[✓] Рёбер в графе: 25000
[INFO] Обучение завершено за 3 эпохи
"""
        result = _parse_trainer_output(sample)
        assert isinstance(result, dict)

    def test_parse_empty_output(self) -> None:
        from backend.service.crawler import _parse_trainer_output

        result = _parse_trainer_output("")
        assert isinstance(result, dict)
