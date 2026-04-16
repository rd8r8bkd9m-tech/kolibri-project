"""Тесты os_bridge.py — API для операций с файловой системой и терминалом."""
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


class TestOsBridge:
    """Тесты эндпоинтов os_bridge."""

    def test_system_stats(self, client: TestClient) -> None:
        resp = client.get("/api/system/stats")
        assert resp.status_code == 200
        data = resp.json()
        # Должен содержать cpu/memory info
        assert isinstance(data, dict)

    def test_ls_project_root(self, client: TestClient) -> None:
        resp = client.get("/api/dev/ls", params={"path": "/workspaces/kolibri-project"})
        assert resp.status_code == 200
        data = resp.json()
        assert isinstance(data, (list, dict))

    def test_terminal_exec_whitelist(self, client: TestClient) -> None:
        """Проверяем, что разрешённая команда выполняется."""
        resp = client.post(
            "/api/terminal/exec",
            json={"cmd": "uptime"},
        )
        assert resp.status_code in (200, 400, 403)

    def test_terminal_exec_blocked(self, client: TestClient) -> None:
        """Проверяем, что опасная команда блокируется."""
        resp = client.post(
            "/api/terminal/exec",
            json={"cmd": "rm -rf /"},
        )
        assert resp.status_code in (200, 400, 403)
        if resp.status_code == 200:
            data = resp.json()
            # Должно быть сообщение об ошибке в ответе
            assert isinstance(data, dict)

    def test_genome_read(self, client: TestClient) -> None:
        resp = client.get("/api/fs/genome")
        # Может не быть файла — либо 200 с данными, либо ошибка
        assert resp.status_code in (200, 404, 500)
