from __future__ import annotations

import asyncio
import datetime as dt
from fastapi import FastAPI, Request, Form, Response
from fastapi.responses import HTMLResponse, RedirectResponse
from fastapi.templating import Jinja2Templates
import httpx

app = FastAPI(title="Content Factory UI", version="0.2.0")

templates = Jinja2Templates(directory="/app/templates")
API_BASE = "http://backend:8000/api"
BACKEND_BASE = "http://backend:8000"


def asset_url(path: str) -> str:
    if not path:
        return ""
    prefix = "/data/assets/"
    if path.startswith(prefix):
        return f"/assets/{path[len(prefix):]}"
    return f"/assets/{path.lstrip('/')}"


templates.env.globals["asset_url"] = asset_url


async def fetch_json(client: httpx.AsyncClient, url: str, default):
    try:
        response = await client.get(url)
        response.raise_for_status()
        return response.json()
    except Exception:
        return default


def redirect_with_message(message: str) -> RedirectResponse:
    return RedirectResponse(url=f"/?msg={httpx.QueryParams({'msg': message})['msg']}", status_code=303)


@app.get("/", response_class=HTMLResponse)
async def kanban(request: Request):
    async with httpx.AsyncClient(timeout=8) as client:
        packages_task = fetch_json(client, f"{API_BASE}/packages", [])
        ideas_task = fetch_json(client, f"{API_BASE}/ideas", [])
        workspaces_task = fetch_json(client, f"{API_BASE}/workspaces", [])
        packages, ideas, workspaces = await asyncio.gather(packages_task, ideas_task, workspaces_task)

    status_counts: dict[str, int] = {}
    for item in packages:
        status_counts[item["status"]] = status_counts.get(item["status"], 0) + 1

    return templates.TemplateResponse(
        "kanban_studio.html",
        {
            "request": request,
            "packages": packages,
            "ideas": ideas,
            "workspaces": workspaces,
            "status_counts": status_counts,
            "message": request.query_params.get("msg"),
        },
    )


@app.post("/actions/ideas/generate")
async def generate_ideas(workspace_id: str = Form(""), niche: str = Form("demo"), limit: int = Form(5)):
    if not workspace_id:
        return redirect_with_message("Нет workspace — запустите seed или demo.")
    async with httpx.AsyncClient(timeout=10) as client:
        await client.post(
            f"{API_BASE}/ideas/generate",
            json={"workspace_id": workspace_id, "niche": niche, "limit": limit},
        )
    return redirect_with_message("Идеи сгенерированы")


@app.post("/actions/ideas/{idea_id}/approve")
async def approve_idea(idea_id: str):
    async with httpx.AsyncClient(timeout=10) as client:
        await client.post(f"{API_BASE}/ideas/{idea_id}/approve", json={"reviewer": "ui"})
    return redirect_with_message("Идея одобрена")


@app.post("/actions/ideas/{idea_id}/package")
async def create_package(idea_id: str, workspace_id: str = Form("")):
    if not workspace_id:
        return redirect_with_message("Нет workspace для создания пакета")
    async with httpx.AsyncClient(timeout=10) as client:
        await client.post(
            f"{API_BASE}/packages/create-from-idea",
            json={"workspace_id": workspace_id, "idea_id": idea_id},
        )
    return redirect_with_message("Пакет создан")


@app.post("/actions/packages/{package_id}/seo")
async def generate_seo(package_id: str):
    async with httpx.AsyncClient(timeout=10) as client:
        await client.post(f"{API_BASE}/packages/{package_id}/generate-seo")
    return redirect_with_message("SEO статья сгенерирована")


@app.post("/actions/packages/{package_id}/script")
async def generate_script(package_id: str):
    async with httpx.AsyncClient(timeout=10) as client:
        await client.post(f"{API_BASE}/packages/{package_id}/generate-video-script")
    return redirect_with_message("Видео‑сценарий сгенерирован")


@app.post("/actions/packages/{package_id}/render")
async def render_package(package_id: str, profile: str = Form("youtube_shorts")):
    async with httpx.AsyncClient(timeout=20) as client:
        await client.post(
            f"{API_BASE}/packages/{package_id}/render",
            json={"profiles": [profile]},
        )
    return redirect_with_message("Рендер завершён")


@app.post("/actions/packages/{package_id}/auto")
async def auto_build(package_id: str):
    async with httpx.AsyncClient(timeout=30) as client:
        await client.post(f"{API_BASE}/packages/{package_id}/generate-seo")
        await client.post(f"{API_BASE}/packages/{package_id}/generate-video-script")
        await client.post(
            f"{API_BASE}/packages/{package_id}/render",
            json={"profiles": ["youtube_shorts"]},
        )
    return redirect_with_message("Пакет собран")


@app.post("/actions/packages/{package_id}/approve")
async def approve_package(package_id: str):
    async with httpx.AsyncClient(timeout=10) as client:
        await client.post(f"{API_BASE}/packages/{package_id}/approve", json={"reviewer": "ui"})
    return redirect_with_message("Этап подтверждён")


@app.post("/actions/packages/{package_id}/schedule")
async def schedule_package(package_id: str):
    scheduled_at = (dt.datetime.utcnow() + dt.timedelta(minutes=10)).isoformat()
    async with httpx.AsyncClient(timeout=10) as client:
        await client.post(
            f"{API_BASE}/packages/{package_id}/schedule",
            json={"scheduled_at": scheduled_at},
        )
    return redirect_with_message("Публикация запланирована")


@app.post("/actions/packages/{package_id}/publish")
async def publish_package(package_id: str):
    async with httpx.AsyncClient(timeout=15) as client:
        await client.post(f"{API_BASE}/packages/{package_id}/publish")
    return redirect_with_message("Опубликовано")


@app.get("/assets/{path:path}")
async def proxy_assets(path: str):
    async with httpx.AsyncClient(timeout=20) as client:
        response = await client.get(f"{BACKEND_BASE}/assets/{path}")
    if response.status_code != 200:
        return Response(status_code=404)
    return Response(content=response.content, media_type=response.headers.get("content-type"))
