from __future__ import annotations

import datetime as dt
from sqlalchemy import Column, DateTime, Float, ForeignKey, Integer, String, Text
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


class Site(Base):
    __tablename__ = "sites"
    id = Column(String, primary_key=True)
    workspace_id = Column(String, ForeignKey("workspaces.id"), index=True, nullable=False)
    cms_type = Column(String, default="wordpress")
    base_url = Column(String, default="http://localhost")
    auth_stub_json = Column(JSONB, default=dict)
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


class SourceItem(Base):
    __tablename__ = "source_items"
    id = Column(String, primary_key=True)
    workspace_id = Column(String, ForeignKey("workspaces.id"), index=True, nullable=False)
    type = Column(String, nullable=False)  # video|article
    platform = Column(String, nullable=False)
    url = Column(String, nullable=False)
    title = Column(String, nullable=False)
    transcript_or_text = Column(Text, default="")
    snapshot_json = Column(JSONB, default=dict)
    collected_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)


class TopicCluster(Base):
    __tablename__ = "topic_clusters"
    id = Column(String, primary_key=True)
    workspace_id = Column(String, ForeignKey("workspaces.id"), index=True, nullable=False)
    pillar_topic = Column(String, nullable=False)
    keywords = Column(ARRAY(String), default=list)
    intent = Column(String, default="informational")
    notes = Column(Text, default="")


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
    status = Column(String, default="idea_approval")
    rationale_json = Column(JSONB, default=dict)
    created_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)


class ContentPackage(Base):
    __tablename__ = "content_packages"
    id = Column(String, primary_key=True)
    workspace_id = Column(String, ForeignKey("workspaces.id"), index=True, nullable=False)
    idea_id = Column(String, ForeignKey("ideas.id"), index=True)
    status = Column(String, default="analysis")
    sources_json = Column(JSONB, default=dict)
    seo_article_md = Column(Text, default="")
    faq_json = Column(JSONB, default=dict)
    schema_json = Column(JSONB, default=dict)
    internal_links_json = Column(JSONB, default=dict)
    video_script = Column(Text, default="")
    storyboard_json = Column(JSONB, default=dict)
    assets_json = Column(JSONB, default=dict)
    version = Column(Integer, default=1)
    created_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)
    updated_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)


class RenderProfile(Base):
    __tablename__ = "render_profiles"
    id = Column(String, primary_key=True)
    workspace_id = Column(String, ForeignKey("workspaces.id"), index=True, nullable=False)
    target = Column(String, nullable=False)
    spec_json = Column(JSONB, default=dict)


class RenderArtifact(Base):
    __tablename__ = "render_artifacts"
    id = Column(String, primary_key=True)
    content_package_id = Column(String, ForeignKey("content_packages.id"), index=True, nullable=False)
    render_profile_id = Column(String, ForeignKey("render_profiles.id"), index=True, nullable=False)
    type = Column(String, nullable=False)
    path = Column(String, nullable=False)
    meta_json = Column(JSONB, default=dict)
    created_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)


class Approval(Base):
    __tablename__ = "approvals"
    id = Column(String, primary_key=True)
    content_package_id = Column(String, ForeignKey("content_packages.id"), index=True, nullable=False)
    stage = Column(String, nullable=False)
    reviewer = Column(String, default="system")
    decision = Column(String, nullable=False)
    comment = Column(Text, default="")
    created_at = Column(DateTime, default=dt.datetime.utcnow, nullable=False)


class Publication(Base):
    __tablename__ = "publications"
    id = Column(String, primary_key=True)
    content_package_id = Column(String, ForeignKey("content_packages.id"), index=True, nullable=False)
    channel_or_site_id = Column(String, nullable=False)
    target = Column(String, nullable=False)
    platform_post_id = Column(String, default="")
    scheduled_at = Column(DateTime)
    published_at = Column(DateTime)
    utm_json = Column(JSONB, default=dict)
    canonical_url = Column(String, default="")
    payload_json = Column(JSONB, default=dict)


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
    site_clicks = Column(Integer, default=0)


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
