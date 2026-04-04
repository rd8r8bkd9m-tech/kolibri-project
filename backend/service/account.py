from __future__ import annotations

import time
from typing import Optional

from fastapi import APIRouter, Query, Request
from pydantic import BaseModel, Field

from .ai_engine import get_engine
from .auth import resolve_request_actor

router = APIRouter(prefix="/api/v1/account", tags=["account"])


class AccountProfileResponse(BaseModel):
    account_id: str
    authenticated: bool
    user: Optional[str] = None
    role: Optional[str] = None
    name: str = ""
    facts: list[str] = Field(default_factory=list)
    documents_count: int = 0
    updated_at: float = 0.0


class AccountProfileUpdateRequest(BaseModel):
    name: Optional[str] = Field(default=None, max_length=120)
    facts: Optional[list[str]] = None


class AccountPreferencesResponse(BaseModel):
    account_id: str
    authenticated: bool
    user: Optional[str] = None
    role: Optional[str] = None
    theme: str = "system"
    persona: str = "assistant"
    memory_enabled: bool = True
    model: Optional[str] = None
    updated_at: float = 0.0


class AccountPreferencesUpdateRequest(BaseModel):
    theme: Optional[str] = Field(default=None, pattern="^(system|light|dark)$")
    persona: Optional[str] = Field(default=None, pattern="^(assistant|romantic|storyteller)$")
    memory_enabled: Optional[bool] = None
    model: Optional[str] = Field(default=None, max_length=120)


def _resolve_actor(request: Request, client_id: str | None = None) -> dict[str, object]:
    actor = resolve_request_actor(request, client_id)
    actor["account_key"] = str(actor.get("account_key", "global") or "global")
    return actor


def _load_profile_for_actor(account_key: str) -> dict[str, object]:
    engine = get_engine()
    profile = engine._load_user_profile(account_key)
    if not isinstance(profile, dict):
        return engine._new_user_profile()
    return dict(profile)


def _save_profile_for_actor(account_key: str, profile: dict[str, object]) -> None:
    engine = get_engine()
    engine._save_user_profile(client_id=account_key, profile=profile)


def _preferences_from_profile(profile: dict[str, object]) -> dict[str, object]:
    raw = profile.get("preferences")
    prefs = dict(raw) if isinstance(raw, dict) else {}
    theme = str(prefs.get("theme", "system") or "system").strip().lower()
    if theme not in {"system", "light", "dark"}:
        theme = "system"
    persona = str(prefs.get("persona", "assistant") or "assistant").strip().lower()
    if persona not in {"assistant", "romantic", "storyteller"}:
        persona = "assistant"
    memory_enabled = bool(prefs.get("memory_enabled", True))
    model = str(prefs.get("model", "") or "").strip() or None
    updated_at = float(prefs.get("updated_at", profile.get("updated_at", 0.0)) or 0.0)
    return {
        "theme": theme,
        "persona": persona,
        "memory_enabled": memory_enabled,
        "model": model,
        "updated_at": updated_at,
    }


@router.get("/profile", response_model=AccountProfileResponse)
async def get_account_profile(
    request: Request,
    client_id: Optional[str] = Query(default=None, max_length=120),
) -> AccountProfileResponse:
    actor = _resolve_actor(request, client_id)
    profile = _load_profile_for_actor(actor["account_key"])
    facts = [str(item).strip() for item in (profile.get("facts", []) or []) if str(item).strip()]
    documents = [item for item in (profile.get("documents", []) or []) if isinstance(item, dict)]
    return AccountProfileResponse(
        account_id=str(actor["account_key"]),
        authenticated=bool(actor["authenticated"]),
        user=actor["user"] if actor["authenticated"] else None,
        role=actor["role"] if actor["authenticated"] else None,
        name=str(profile.get("name", "") or ""),
        facts=facts[:32],
        documents_count=len(documents),
        updated_at=float(profile.get("updated_at", 0.0) or 0.0),
    )


@router.put("/profile", response_model=AccountProfileResponse)
async def update_account_profile(
    req: AccountProfileUpdateRequest,
    request: Request,
    client_id: Optional[str] = Query(default=None, max_length=120),
) -> AccountProfileResponse:
    actor = _resolve_actor(request, client_id)
    profile = _load_profile_for_actor(actor["account_key"])
    if req.name is not None:
        profile["name"] = str(req.name or "").strip()[:120]
    if req.facts is not None:
        profile["facts"] = [str(item).strip() for item in req.facts if str(item).strip()][:32]
    profile["updated_at"] = time.time()
    _save_profile_for_actor(str(actor["account_key"]), profile)
    return await get_account_profile(request, client_id=client_id)


@router.get("/preferences", response_model=AccountPreferencesResponse)
async def get_account_preferences(
    request: Request,
    client_id: Optional[str] = Query(default=None, max_length=120),
) -> AccountPreferencesResponse:
    actor = _resolve_actor(request, client_id)
    profile = _load_profile_for_actor(actor["account_key"])
    preferences = _preferences_from_profile(profile)
    return AccountPreferencesResponse(
        account_id=str(actor["account_key"]),
        authenticated=bool(actor["authenticated"]),
        user=actor["user"] if actor["authenticated"] else None,
        role=actor["role"] if actor["authenticated"] else None,
        theme=str(preferences["theme"]),
        persona=str(preferences["persona"]),
        memory_enabled=bool(preferences["memory_enabled"]),
        model=preferences["model"] if isinstance(preferences["model"], str) or preferences["model"] is None else None,
        updated_at=float(preferences["updated_at"] or 0.0),
    )


@router.put("/preferences", response_model=AccountPreferencesResponse)
async def update_account_preferences(
    req: AccountPreferencesUpdateRequest,
    request: Request,
    client_id: Optional[str] = Query(default=None, max_length=120),
) -> AccountPreferencesResponse:
    actor = _resolve_actor(request, client_id)
    profile = _load_profile_for_actor(actor["account_key"])
    current = _preferences_from_profile(profile)
    next_preferences = {
        "theme": req.theme if req.theme is not None else current["theme"],
        "persona": req.persona if req.persona is not None else current["persona"],
        "memory_enabled": req.memory_enabled if req.memory_enabled is not None else current["memory_enabled"],
        "model": (req.model.strip()[:120] if isinstance(req.model, str) and req.model.strip() else current["model"]),
        "updated_at": time.time(),
    }
    profile["preferences"] = next_preferences
    profile["updated_at"] = float(next_preferences["updated_at"])
    _save_profile_for_actor(str(actor["account_key"]), profile)
    return await get_account_preferences(request, client_id=client_id)
