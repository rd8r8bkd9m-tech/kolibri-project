from __future__ import annotations

import datetime as dt
from typing import Any
from pydantic import BaseModel, Field


class WorkspaceCreate(BaseModel):
    name: str


class WorkspaceOut(BaseModel):
    id: str
    name: str
    created_at: dt.datetime

    class Config:
        from_attributes = True


class BrandGuidelinesCreate(BaseModel):
    workspace_id: str
    tone: str = "neutral"
    banned_phrases: list[str] = Field(default_factory=list)
    forbidden_claims: list[str] = Field(default_factory=list)
    required_disclaimers: list[str] = Field(default_factory=list)
    style_notes: str = ""


class BrandGuidelinesOut(BrandGuidelinesCreate):
    id: str
    created_at: dt.datetime

    class Config:
        from_attributes = True


class SiteCreate(BaseModel):
    workspace_id: str
    cms_type: str = "wordpress"
    base_url: str = "http://localhost"
    auth_stub_json: dict[str, Any] = Field(default_factory=dict)


class SiteOut(SiteCreate):
    id: str
    created_at: dt.datetime

    class Config:
        from_attributes = True


class ChannelCreate(BaseModel):
    workspace_id: str
    platform: str
    auth_stub_json: dict[str, Any] = Field(default_factory=dict)
    default_utm_source: str = "content_factory"
    default_utm_medium: str = "organic"


class ChannelOut(ChannelCreate):
    id: str
    created_at: dt.datetime

    class Config:
        from_attributes = True


class CompetitorCreate(BaseModel):
    workspace_id: str
    name: str
    urls: list[str] = Field(default_factory=list)


class CompetitorOut(CompetitorCreate):
    id: str
    created_at: dt.datetime

    class Config:
        from_attributes = True


class SourceCollectRequest(BaseModel):
    workspace_id: str
    niche: str
    limit: int = 10


class IdeaGenerateRequest(BaseModel):
    workspace_id: str
    niche: str
    limit: int = 10


class IdeaOut(BaseModel):
    id: str
    workspace_id: str
    title: str
    hook: str
    angle: str
    cta: str
    format: str
    funnel_stage: str
    risk_score: int
    status: str
    rationale_json: dict[str, Any]
    created_at: dt.datetime

    class Config:
        from_attributes = True


class PackageCreateRequest(BaseModel):
    workspace_id: str
    idea_id: str


class PackageOut(BaseModel):
    id: str
    workspace_id: str
    idea_id: str | None
    status: str
    seo_article_md: str
    video_script: str
    assets_json: dict[str, Any]
    version: int
    created_at: dt.datetime
    updated_at: dt.datetime

    class Config:
        from_attributes = True


class PackageActionRequest(BaseModel):
    reviewer: str = "system"
    comment: str = ""


class RenderRequest(BaseModel):
    profiles: list[str]


class ScheduleRequest(BaseModel):
    scheduled_at: dt.datetime


class MetricsOut(BaseModel):
    id: str
    publication_id: str
    date: dt.datetime
    views: int
    watch_time: float
    retention_proxy: float
    likes: int
    comments: int
    shares: int
    ctr_proxy: float
    site_clicks: int

    class Config:
        from_attributes = True


class AttributionOut(BaseModel):
    id: str
    publication_id: str
    leads: int
    revenue: float
    margin: float
    cost: float
    romi: float
    roas: float
    calculated_at: dt.datetime

    class Config:
        from_attributes = True


class AttributionCalcRequest(BaseModel):
    leads: int = 0
    revenue: float = 0.0
    margin: float = 0.0
    cost: float = 0.0


class DashboardSummary(BaseModel):
    total_packages: int
    total_publications: int
    avg_romi: float
