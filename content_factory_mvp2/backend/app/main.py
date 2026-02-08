from __future__ import annotations

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from .api.routes import (
    workspaces_router,
    brand_guidelines_router,
    sites_router,
    channels_router,
    competitors_router,
    sources_router,
    ideas_router,
    packages_router,
    publications_router,
    dashboard_router,
)

app = FastAPI(title="Content Factory 2.0", version="0.2.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(workspaces_router, prefix="/api")
app.include_router(brand_guidelines_router, prefix="/api")
app.include_router(sites_router, prefix="/api")
app.include_router(channels_router, prefix="/api")
app.include_router(competitors_router, prefix="/api")
app.include_router(sources_router, prefix="/api")
app.include_router(ideas_router, prefix="/api")
app.include_router(packages_router, prefix="/api")
app.include_router(publications_router, prefix="/api")
app.include_router(dashboard_router, prefix="/api")

app.mount("/assets", StaticFiles(directory="/data/assets"), name="assets")


@app.get("/api/health")
def health():
    return {"status": "ok"}
