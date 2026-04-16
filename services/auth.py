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
from .persistence import get_db

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


_db = get_db()


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
_users: dict[str, User] = {}


def _sanitize_account_key(value: str | None) -> str:
    raw = str(value or "").strip()
    if not raw:
        return "global"
    safe = "".join(ch if ch.isalnum() or ch in "._:-" else "-" for ch in raw).strip("-._:")
    return (safe or "global")[:96]


def _load_users_from_db() -> None:
    try:
        rows = _db.list_auth_users()
    except Exception as exc:
        log.warning("Не удалось загрузить пользователей из SQLite: %s", exc)
        rows = []
    for row in rows:
        username = str(row.get("username", "") or "").strip()
        password_hash = str(row.get("password_hash", "") or "").strip()
        if not username or not password_hash:
            continue
        _users[username] = User(
            username=username,
            password_hash=password_hash,
            role=str(row.get("role", "user") or "user"),
        )


def _persist_user(user: User) -> None:
    try:
        _db.upsert_auth_user(
            username=user.username,
            password_hash=user.password_hash,
            role=user.role,
        )
    except Exception as exc:
        log.warning("Не удалось сохранить пользователя %s: %s", user.username, exc)


def _ensure_admin_user() -> None:
    admin = _users.get("admin")
    if admin is None:
        admin = User(
            username="admin",
            password_hash=_hash_password(_ADMIN_PASSWORD),
            role="admin",
        )
        _users["admin"] = admin
        _persist_user(admin)


_load_users_from_db()
_ensure_admin_user()

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


def try_get_request_user(request: Request) -> Optional[dict]:
    """Попытаться распознать пользователя без жёсткой ошибки 401."""
    if not _AUTH_ENABLED:
        return {"sub": "anonymous", "role": "admin"}

    api_key = request.headers.get("X-API-Key", "")
    if api_key and api_key in _API_KEYS:
        return {"sub": f"apikey:{api_key[:8]}...", "role": "admin"}

    auth_header = request.headers.get("Authorization", "").strip()
    if not auth_header.lower().startswith("bearer "):
        return None
    token = auth_header.split(" ", 1)[1].strip()
    if not token:
        return None
    try:
        return decode_token(token)
    except HTTPException:
        return None


def resolve_request_actor(request: Request, explicit_client_id: str | None = None) -> dict[str, object]:
    user = try_get_request_user(request)
    if user and str(user.get("sub", "")).strip() and not str(user.get("sub", "")).startswith("apikey:"):
        account_key = _sanitize_account_key(f"user:{user['sub']}")
        return {
            "authenticated": True,
            "user": str(user.get("sub", "") or ""),
            "role": str(user.get("role", "user") or "user"),
            "account_key": account_key,
        }
    requested_client_id = (
        str(explicit_client_id or "").strip()
        or str(request.headers.get("X-Kolibri-Client-Id", "") or "").strip()
    )
    account_key = _sanitize_account_key(requested_client_id)
    return {
        "authenticated": False,
        "user": None,
        "role": None,
        "account_key": account_key,
    }


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
    authenticated: bool = False
    user: Optional[str] = None
    role: Optional[str] = None
    account_id: Optional[str] = None


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
    _persist_user(_users[req.username])
    token = create_token(req.username, "user")
    log.info("Зарегистрирован пользователь: %s", req.username)
    return TokenResponse(
        access_token=token,
        expires_in=_JWT_EXPIRE_SECONDS,
        role="user",
    )


@router.get("/status", response_model=AuthStatusResponse)
async def auth_status(
    request: Request,
) -> AuthStatusResponse:
    """Проверить статус аутентификации."""
    actor = resolve_request_actor(request)
    return AuthStatusResponse(
        auth_enabled=_AUTH_ENABLED,
        authenticated=bool(actor["authenticated"]),
        user=actor["user"] if actor["authenticated"] else None,
        role=actor["role"] if actor["authenticated"] else None,
        account_id=str(actor["account_key"]),
    )


@router.post("/logout")
async def logout() -> dict:
    """Logout на backend не хранит серверную сессию, но даёт продуктовый endpoint."""
    return {"status": "ok"}
