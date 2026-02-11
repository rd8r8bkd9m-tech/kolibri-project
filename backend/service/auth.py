"""
Kolibri Auth — JWT-аутентификация для API.

Режимы работы:
  - KOLIBRI_AUTH_ENABLED=0 (по умолчанию) — без аутентификации
  - KOLIBRI_AUTH_ENABLED=1 — JWT обязателен для всех /api/v1/* эндпоинтов

Токен передаётся в заголовке:  Authorization: Bearer <token>
"""
from __future__ import annotations

import hashlib
import hmac
import logging
import os
import secrets
import time
from dataclasses import dataclass, field
from typing import Optional

import jwt
from fastapi import Depends, HTTPException, Request, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer

log = logging.getLogger("kolibri.auth")

# ---------------------------------------------------------------------------
# Конфигурация
# ---------------------------------------------------------------------------

_JWT_SECRET = os.getenv("KOLIBRI_JWT_SECRET", secrets.token_hex(32))
_JWT_ALGORITHM = "HS256"
_JWT_EXPIRE_SECONDS = int(os.getenv("KOLIBRI_JWT_EXPIRE", "86400"))  # 24h
_AUTH_ENABLED = os.getenv("KOLIBRI_AUTH_ENABLED", "1").strip() == "1"

# Встроенные API-ключи (через ENV, запятая-разделитель)
_API_KEYS: set[str] = set(
    k.strip()
    for k in os.getenv("KOLIBRI_API_KEYS", "").split(",")
    if k.strip()
)

# ---------------------------------------------------------------------------
# Пользователи (in-memory, для production — подключить DB)
# ---------------------------------------------------------------------------


@dataclass
class User:
    username: str
    password_hash: str
    role: str = "user"  # "user" | "admin"


def _hash_password(password: str) -> str:
    """Простой хеш пароля через SHA-256 + salt (для MVP)."""
    salt = secrets.token_hex(16)
    h = hashlib.sha256(f"{salt}:{password}".encode()).hexdigest()
    return f"{salt}:{h}"


def _verify_password(password: str, stored: str) -> bool:
    """Проверить пароль против хеша."""
    parts = stored.split(":", 1)
    if len(parts) != 2:
        return False
    salt, expected = parts
    h = hashlib.sha256(f"{salt}:{password}".encode()).hexdigest()
    return hmac.compare_digest(h, expected)


# Начальный admin-пользователь (пароль ОБЯЗАТЕЛЕН из ENV)
_ADMIN_PASSWORD = os.getenv("KOLIBRI_ADMIN_PASSWORD", "")
if not _ADMIN_PASSWORD:
    _ADMIN_PASSWORD = secrets.token_urlsafe(24)
    log.warning(
        "KOLIBRI_ADMIN_PASSWORD не задан! Сгенерирован временный: %s",
        _ADMIN_PASSWORD,
    )
_users: dict[str, User] = {
    "admin": User(
        username="admin",
        password_hash=_hash_password(_ADMIN_PASSWORD),
        role="admin",
    ),
}

# ---------------------------------------------------------------------------
# JWT операции
# ---------------------------------------------------------------------------


def create_token(username: str, role: str = "user") -> str:
    """Создать JWT-токен для пользователя."""
    payload = {
        "sub": username,
        "role": role,
        "iat": int(time.time()),
        "exp": int(time.time()) + _JWT_EXPIRE_SECONDS,
    }
    return jwt.encode(payload, _JWT_SECRET, algorithm=_JWT_ALGORITHM)


def decode_token(token: str) -> dict:
    """Декодировать и валидировать JWT-токен."""
    try:
        return jwt.decode(token, _JWT_SECRET, algorithms=[_JWT_ALGORITHM])
    except jwt.ExpiredSignatureError:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Токен истёк",
        )
    except jwt.InvalidTokenError as e:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail=f"Невалидный токен: {e}",
        )


# ---------------------------------------------------------------------------
# FastAPI Dependency — опциональная аутентификация
# ---------------------------------------------------------------------------

_bearer_scheme = HTTPBearer(auto_error=False)


async def get_current_user(
    request: Request,
    credentials: Optional[HTTPAuthorizationCredentials] = Depends(_bearer_scheme),
) -> Optional[dict]:
    """
    Dependency для получения текущего пользователя.

    Если KOLIBRI_AUTH_ENABLED=0 — пропускает всех.
    Если KOLIBRI_AUTH_ENABLED=1 — требует валидный JWT или API-ключ.
    """
    if not _AUTH_ENABLED:
        return {"sub": "anonymous", "role": "admin"}

    # 1. Проверяем API-ключ в заголовке X-API-Key
    api_key = request.headers.get("X-API-Key", "")
    if api_key and api_key in _API_KEYS:
        return {"sub": f"apikey:{api_key[:8]}...", "role": "admin"}

    # 2. Проверяем Bearer JWT
    if credentials is None:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Требуется аутентификация. Передайте Authorization: Bearer <token>",
            headers={"WWW-Authenticate": "Bearer"},
        )

    return decode_token(credentials.credentials)


def require_admin(user: dict = Depends(get_current_user)) -> dict:
    """Dependency: требует роль admin."""
    if user.get("role") != "admin":
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Требуется роль admin",
        )
    return user


# ---------------------------------------------------------------------------
# Auth endpoints
# ---------------------------------------------------------------------------

from fastapi import APIRouter
from pydantic import BaseModel, Field

router = APIRouter(prefix="/api/v1/auth", tags=["auth"])


class LoginRequest(BaseModel):
    username: str = Field(min_length=1, max_length=64)
    password: str = Field(min_length=1, max_length=256)


class TokenResponse(BaseModel):
    access_token: str
    token_type: str = "bearer"
    expires_in: int
    role: str


class RegisterRequest(BaseModel):
    username: str = Field(min_length=3, max_length=64)
    password: str = Field(min_length=6, max_length=256)


class AuthStatusResponse(BaseModel):
    auth_enabled: bool
    user: Optional[str] = None
    role: Optional[str] = None


@router.post("/login", response_model=TokenResponse)
async def login(req: LoginRequest) -> TokenResponse:
    """Получить JWT-токен по логину/паролю."""
    user = _users.get(req.username)
    if user is None or not _verify_password(req.password, user.password_hash):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Неверный логин или пароль",
        )
    token = create_token(user.username, user.role)
    return TokenResponse(
        access_token=token,
        expires_in=_JWT_EXPIRE_SECONDS,
        role=user.role,
    )


@router.post("/register", response_model=TokenResponse)
async def register(
    req: RegisterRequest,
    _admin: dict = Depends(require_admin),
) -> TokenResponse:
    """Зарегистрировать нового пользователя (только admin)."""
    if req.username in _users:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=f"Пользователь '{req.username}' уже существует",
        )
    _users[req.username] = User(
        username=req.username,
        password_hash=_hash_password(req.password),
    )
    token = create_token(req.username, "user")
    log.info("Зарегистрирован пользователь: %s", req.username)
    return TokenResponse(
        access_token=token,
        expires_in=_JWT_EXPIRE_SECONDS,
        role="user",
    )


@router.get("/status", response_model=AuthStatusResponse)
async def auth_status(
    user: Optional[dict] = Depends(get_current_user),
) -> AuthStatusResponse:
    """Проверить статус аутентификации."""
    return AuthStatusResponse(
        auth_enabled=_AUTH_ENABLED,
        user=user.get("sub") if user else None,
        role=user.get("role") if user else None,
    )
