"""Тесты auth.py — JWT аутентификация Kolibri."""
from __future__ import annotations

import sys
import time
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


class TestAuthTokens:
    """Тесты create_token / decode_token."""

    def test_roundtrip(self) -> None:
        from backend.service.auth import create_token, decode_token

        token = create_token("testuser", role="admin")
        payload = decode_token(token)
        assert payload is not None
        assert payload["sub"] == "testuser"
        assert payload["role"] == "admin"

    def test_invalid_token(self) -> None:
        from fastapi import HTTPException
        from backend.service.auth import decode_token

        with pytest.raises(HTTPException) as exc_info:
            decode_token("invalid.jwt.token")
        assert exc_info.value.status_code == 401

    def test_expired_token(self, monkeypatch: pytest.MonkeyPatch) -> None:
        from fastapi import HTTPException
        from backend.service import auth
        from backend.service.auth import create_token, decode_token

        # Устанавливаем минимальный TTL
        monkeypatch.setattr(auth, "_JWT_EXPIRE_SECONDS", 1)
        token = create_token("user", role="user")
        import time
        time.sleep(1.5)
        with pytest.raises(HTTPException) as exc_info:
            decode_token(token)
        assert exc_info.value.status_code == 401


class TestPasswordHashing:
    """Тесты хеширования паролей."""

    def test_hash_verify_correct(self) -> None:
        from backend.service.auth import _hash_password, _verify_password

        hashed = _hash_password("my-secret-pwd")
        assert _verify_password("my-secret-pwd", hashed)

    def test_hash_verify_wrong(self) -> None:
        from backend.service.auth import _hash_password, _verify_password

        hashed = _hash_password("correct")
        assert not _verify_password("wrong", hashed)


class TestAuthEndpoints:
    """Тесты API эндпоинтов auth."""

    def test_login_wrong_credentials(self, client: TestClient) -> None:
        resp = client.post(
            "/api/v1/auth/login",
            json={"username": "nonexistent", "password": "bad"},
        )
        assert resp.status_code in (401, 403)

    def test_auth_status_is_public(self, client: TestClient) -> None:
        """Статус auth публичный и нужен фронтенду до логина."""
        resp = client.get("/api/v1/auth/status")
        assert resp.status_code == 200
        data = resp.json()
        assert "auth_enabled" in data
        assert "authenticated" in data
        assert "account_id" in data

    def test_auth_status_with_token(self, client: TestClient) -> None:
        """Статус auth с валидным токеном."""
        from backend.service.auth import create_token
        token = create_token("admin", role="admin")
        resp = client.get(
            "/api/v1/auth/status",
            headers={"Authorization": f"Bearer {token}"},
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["authenticated"] is True
        assert data["user"] == "admin"
        assert data["role"] == "admin"
