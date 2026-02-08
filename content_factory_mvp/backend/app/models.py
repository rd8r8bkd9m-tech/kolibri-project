from __future__ import annotations

import datetime as dt
from sqlalchemy import (
    Column,
    DateTime,
    Float,
    ForeignKey,
    Integer,
    String,
    Text,
)
from sqlalchemy.dialects.postgresql import ARRAY, JSONB

from .db import Base


class Workspace(Base):
    __tablename__ = "workspaces"

    id = Column(String, primary_key=True)
    name = Column(String, nullable=False)
    created_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)


class BrandGuidelines(Base):
    __tablename__ = "brand_guidelines"

    id = Column(String, primary_key=True)
    workspace_id = Column(String, ForeignKey("workspaces.id"), index=True, nullable=False)
    tone = Column(String, default="neutral")
    banned_phrases = Column(ARRAY(String), default=list)
    forbidden_claims = Column(ARRAY(String), default=list)
    required_disclaimers = Column(ARRAY(String), default=list)
    style_notes = Column(Text, default="")
    created_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)


class Channel(Base):
    __tablename__ = "channels"

    id = Column(String, primary_key=True)
    workspace_id = Column(String, ForeignKey("workspaces.id"), index=True, nullable=False)
    platform = Column(String, nullable=False)
    auth_stub_json = Column(JSONB, default=dict)
    default_utm_source = Column(String, default="content_factory")
    default_utm_medium = Column(String, default="organic")
    created_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)


class Competitor(Base):
    __tablename__ = "competitors"

    id = Column(String, primary_key=True)
    workspace_id = Column(String, ForeignKey("workspaces.id"), index=True, nullable=False)
    name = Column(String, nullable=False)
    urls = Column(ARRAY(String), default=list)
    created_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)


class SourceVideo(Base):
    __tablename__ = "source_videos"

    id = Column(String, primary_key=True)
    workspace_id = Column(String, ForeignKey("workspaces.id"), index=True, nullable=False)
    platform = Column(String, nullable=False)
    url = Column(String, nullable=False)
    title = Column(String, nullable=False)
    transcript = Column(Text, default="")
    snapshot_json = Column(JSONB, default=dict)
    collected_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)


class Idea(Base):
    __tablename__ = "ideas"

    id = Column(String, primary_key=True)
    workspace_id = Column(String, ForeignKey("workspaces.id"), index=True, nullable=False)
    title = Column(String, nullable=False)
    hook = Column(Text, default="")
    angle = Column(Text, default="")
    cta = Column(Text, default="")
    format = Column(String, default="Problem→Solution")
    funnel_stage = Column(String, default="TOFU")
    risk_score = Column(Integer, default=0)
    status = Column(String, default="draft")
    rationale_json = Column(JSONB, default=dict)
    created_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)


class ContentItem(Base):
    __tablename__ = "content_items"

    id = Column(String, primary_key=True)
    workspace_id = Column(String, ForeignKey("workspaces.id"), index=True, nullable=False)
    idea_id = Column(String, ForeignKey("ideas.id"), index=True)
    status = Column(String, default="analysis")
    script = Column(Text, default="")
    storyboard_json = Column(JSONB, default=dict)
    assets_json = Column(JSONB, default=dict)
    render_path = Column(String, default="")
    metadata_json = Column(JSONB, default=dict)
    version = Column(Integer, default=1)
    created_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)
    updated_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)


class Approval(Base):
    __tablename__ = "approvals"

    id = Column(String, primary_key=True)
    content_item_id = Column(String, ForeignKey("content_items.id"), index=True, nullable=False)
    stage = Column(String, nullable=False)
    reviewer = Column(String, default="system")
    decision = Column(String, nullable=False)
    comment = Column(Text, default="")
    created_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)


class Publication(Base):
    __tablename__ = "publications"

    id = Column(String, primary_key=True)
    content_item_id = Column(String, ForeignKey("content_items.id"), index=True, nullable=False)
    platform = Column(String, default="youtube_shorts")
    platform_post_id = Column(String, default="")
    scheduled_at = Column(DateTime)
    published_at = Column(DateTime)
    utm_json = Column(JSONB, default=dict)
    caption = Column(Text, default="")
    tags = Column(ARRAY(String), default=list)


class MetricsDaily(Base):
    __tablename__ = "metrics_daily"

    id = Column(String, primary_key=True)
    publication_id = Column(String, ForeignKey("publications.id"), index=True, nullable=False)
    date = Column(DateTime, default=dt.datetime.utcnow, nullable=False)
    views = Column(Integer, default=0)
    watch_time = Column(Float, default=0.0)
    retention_proxy = Column(Float, default=0.0)
    likes = Column(Integer, default=0)
    comments = Column(Integer, default=0)
    shares = Column(Integer, default=0)
    ctr_proxy = Column(Float, default=0.0)


class Attribution(Base):
    __tablename__ = "attributions"

    id = Column(String, primary_key=True)
    publication_id = Column(String, ForeignKey("publications.id"), index=True, nullable=False)
    leads = Column(Integer, default=0)
    revenue = Column(Float, default=0.0)
    margin = Column(Float, default=0.0)
    cost = Column(Float, default=0.0)
    romi = Column(Float, default=0.0)
    roas = Column(Float, default=0.0)
    calculated_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)
