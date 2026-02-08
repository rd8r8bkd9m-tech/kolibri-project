from __future__ import annotations

from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates
import httpx

app = FastAPI(title="Content Factory UI", version="0.1.0")

templates = Jinja2Templates(directory="/app/templates")
API_BASE = "http://backend:8000/api"


@app.get("/", response_class=HTMLResponse)
async def index(request: Request):
    async with httpx.AsyncClient() as client:
        items = (await client.get(f"{API_BASE}/content-items")).json()
    return templates.TemplateResponse("kanban.html", {"request": request, "items": items})
