from __future__ import annotations

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from .api.routes import (
    workspaces_router,
    brands_router,
    channels_router,
    competitors_router,
    source_videos_router,
    ideas_router,
    content_items_router,
    publications_router,
    dashboard_router,
    demo_router,
)


app = FastAPI(title="Content Factory MVP", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(workspaces_router, prefix="/api")
app.include_router(brands_router, prefix="/api")
app.include_router(channels_router, prefix="/api")
app.include_router(competitors_router, prefix="/api")
app.include_router(source_videos_router, prefix="/api")
app.include_router(ideas_router, prefix="/api")
app.include_router(content_items_router, prefix="/api")
app.include_router(publications_router, prefix="/api")
app.include_router(dashboard_router, prefix="/api")
app.include_router(demo_router, prefix="/api")


@app.get("/api/health")
def health():
    return {"status": "ok"}
