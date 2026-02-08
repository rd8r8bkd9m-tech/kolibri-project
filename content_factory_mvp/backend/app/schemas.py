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


class SourceVideoOut(BaseModel):
    id: str
    workspace_id: str
    platform: str
    url: str
    title: str
    transcript: str
    snapshot_json: dict[str, Any]
    collected_at: dt.datetime

    class Config:
        from_attributes = True


class SourceVideoCollectRequest(BaseModel):
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


class IdeaApprovalRequest(BaseModel):
    decision: str
    comment: str = ""


class ContentItemOut(BaseModel):
    id: str
    workspace_id: str
    idea_id: str | None
    status: str
    script: str
    storyboard_json: dict[str, Any]
    assets_json: dict[str, Any]
    render_path: str
    metadata_json: dict[str, Any]
    version: int
    created_at: dt.datetime
    updated_at: dt.datetime

    class Config:
        from_attributes = True


class ContentCreateRequest(BaseModel):
    workspace_id: str
    idea_id: str


class ContentActionRequest(BaseModel):
    reviewer: str = "system"
    comment: str = ""


class PublicationOut(BaseModel):
    id: str
    content_item_id: str
    platform: str
    platform_post_id: str
    scheduled_at: dt.datetime | None
    published_at: dt.datetime | None
    utm_json: dict[str, Any]
    caption: str
    tags: list[str]

    class Config:
        from_attributes = True


class PublicationScheduleRequest(BaseModel):
    scheduled_at: dt.datetime


class MetricsDailyOut(BaseModel):
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
    total_content_items: int
    total_publications: int
    avg_romi: float
