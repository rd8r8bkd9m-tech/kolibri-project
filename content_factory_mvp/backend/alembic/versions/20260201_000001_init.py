"""init schema

Revision ID: 20260201_000001
Revises: 
Create Date: 2026-02-01 00:00:01.000000
"""
from __future__ import annotations

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects import postgresql

revision = "20260201_000001"
down_revision = None
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        "workspaces",
        sa.Column("id", sa.String(), primary_key=True),
        sa.Column("name", sa.String(), nullable=False),
        sa.Column("created_at", sa.DateTime(), nullable=False),
    )

    op.create_table(
        "brand_guidelines",
        sa.Column("id", sa.String(), primary_key=True),
        sa.Column("workspace_id", sa.String(), nullable=False, index=True),
        sa.Column("tone", sa.String(), nullable=True),
        sa.Column("banned_phrases", postgresql.ARRAY(sa.String()), nullable=True),
        sa.Column("forbidden_claims", postgresql.ARRAY(sa.String()), nullable=True),
        sa.Column("required_disclaimers", postgresql.ARRAY(sa.String()), nullable=True),
        sa.Column("style_notes", sa.Text(), nullable=True),
        sa.Column("created_at", sa.DateTime(), nullable=False),
    )

    op.create_table(
        "channels",
        sa.Column("id", sa.String(), primary_key=True),
        sa.Column("workspace_id", sa.String(), nullable=False, index=True),
        sa.Column("platform", sa.String(), nullable=False),
        sa.Column("auth_stub_json", postgresql.JSONB(), nullable=True),
        sa.Column("default_utm_source", sa.String(), nullable=True),
        sa.Column("default_utm_medium", sa.String(), nullable=True),
        sa.Column("created_at", sa.DateTime(), nullable=False),
    )

    op.create_table(
        "competitors",
        sa.Column("id", sa.String(), primary_key=True),
        sa.Column("workspace_id", sa.String(), nullable=False, index=True),
        sa.Column("name", sa.String(), nullable=False),
        sa.Column("urls", postgresql.ARRAY(sa.String()), nullable=True),
        sa.Column("created_at", sa.DateTime(), nullable=False),
    )

    op.create_table(
        "source_videos",
        sa.Column("id", sa.String(), primary_key=True),
        sa.Column("workspace_id", sa.String(), nullable=False, index=True),
        sa.Column("platform", sa.String(), nullable=False),
        sa.Column("url", sa.String(), nullable=False),
        sa.Column("title", sa.String(), nullable=False),
        sa.Column("transcript", sa.Text(), nullable=True),
        sa.Column("snapshot_json", postgresql.JSONB(), nullable=True),
        sa.Column("collected_at", sa.DateTime(), nullable=False),
    )

    op.create_table(
        "ideas",
        sa.Column("id", sa.String(), primary_key=True),
        sa.Column("workspace_id", sa.String(), nullable=False, index=True),
        sa.Column("title", sa.String(), nullable=False),
        sa.Column("hook", sa.Text(), nullable=True),
        sa.Column("angle", sa.Text(), nullable=True),
        sa.Column("cta", sa.Text(), nullable=True),
        sa.Column("format", sa.String(), nullable=True),
        sa.Column("funnel_stage", sa.String(), nullable=True),
        sa.Column("risk_score", sa.Integer(), nullable=True),
        sa.Column("status", sa.String(), nullable=True),
        sa.Column("rationale_json", postgresql.JSONB(), nullable=True),
        sa.Column("created_at", sa.DateTime(), nullable=False),
    )

    op.create_table(
        "content_items",
        sa.Column("id", sa.String(), primary_key=True),
        sa.Column("workspace_id", sa.String(), nullable=False, index=True),
        sa.Column("idea_id", sa.String(), nullable=True, index=True),
        sa.Column("status", sa.String(), nullable=True),
        sa.Column("script", sa.Text(), nullable=True),
        sa.Column("storyboard_json", postgresql.JSONB(), nullable=True),
        sa.Column("assets_json", postgresql.JSONB(), nullable=True),
        sa.Column("render_path", sa.String(), nullable=True),
        sa.Column("metadata_json", postgresql.JSONB(), nullable=True),
        sa.Column("version", sa.Integer(), nullable=True),
        sa.Column("created_at", sa.DateTime(), nullable=False),
        sa.Column("updated_at", sa.DateTime(), nullable=False),
    )

    op.create_table(
        "approvals",
        sa.Column("id", sa.String(), primary_key=True),
        sa.Column("content_item_id", sa.String(), nullable=False, index=True),
        sa.Column("stage", sa.String(), nullable=False),
        sa.Column("reviewer", sa.String(), nullable=True),
        sa.Column("decision", sa.String(), nullable=False),
        sa.Column("comment", sa.Text(), nullable=True),
        sa.Column("created_at", sa.DateTime(), nullable=False),
    )

    op.create_table(
        "publications",
        sa.Column("id", sa.String(), primary_key=True),
        sa.Column("content_item_id", sa.String(), nullable=False, index=True),
        sa.Column("platform", sa.String(), nullable=True),
        sa.Column("platform_post_id", sa.String(), nullable=True),
        sa.Column("scheduled_at", sa.DateTime(), nullable=True),
        sa.Column("published_at", sa.DateTime(), nullable=True),
        sa.Column("utm_json", postgresql.JSONB(), nullable=True),
        sa.Column("caption", sa.Text(), nullable=True),
        sa.Column("tags", postgresql.ARRAY(sa.String()), nullable=True),
    )

    op.create_table(
        "metrics_daily",
        sa.Column("id", sa.String(), primary_key=True),
        sa.Column("publication_id", sa.String(), nullable=False, index=True),
        sa.Column("date", sa.DateTime(), nullable=False),
        sa.Column("views", sa.Integer(), nullable=True),
        sa.Column("watch_time", sa.Float(), nullable=True),
        sa.Column("retention_proxy", sa.Float(), nullable=True),
        sa.Column("likes", sa.Integer(), nullable=True),
        sa.Column("comments", sa.Integer(), nullable=True),
        sa.Column("shares", sa.Integer(), nullable=True),
        sa.Column("ctr_proxy", sa.Float(), nullable=True),
    )

    op.create_table(
        "attributions",
        sa.Column("id", sa.String(), primary_key=True),
        sa.Column("publication_id", sa.String(), nullable=False, index=True),
        sa.Column("leads", sa.Integer(), nullable=True),
        sa.Column("revenue", sa.Float(), nullable=True),
        sa.Column("margin", sa.Float(), nullable=True),
        sa.Column("cost", sa.Float(), nullable=True),
        sa.Column("romi", sa.Float(), nullable=True),
        sa.Column("roas", sa.Float(), nullable=True),
        sa.Column("calculated_at", sa.DateTime(), nullable=False),
    )


def downgrade() -> None:
    op.drop_table("attributions")
    op.drop_table("metrics_daily")
    op.drop_table("publications")
    op.drop_table("approvals")
    op.drop_table("content_items")
    op.drop_table("ideas")
    op.drop_table("source_videos")
    op.drop_table("competitors")
    op.drop_table("channels")
    op.drop_table("brand_guidelines")
    op.drop_table("workspaces")
