from __future__ import annotations

import uuid
import datetime as dt
from sqlalchemy.orm import Session

from .models import Workspace, BrandGuidelines, Site, Channel, Competitor, Idea, ContentPackage


def seed(db: Session) -> dict[str, str]:
    workspace = Workspace(id=str(uuid.uuid4()), name="Content Factory 2.0")
    db.add(workspace)

    brand = BrandGuidelines(
        id=str(uuid.uuid4()),
        workspace_id=workspace.id,
        tone="friendly",
        banned_phrases=["гарантированный доход"],
        forbidden_claims=["100% результат"],
        required_disclaimers=["Не является финансовым советом"],
        style_notes="Короткие фразы, деловой тон",
    )
    db.add(brand)

    site = Site(
        id=str(uuid.uuid4()),
        workspace_id=workspace.id,
        cms_type="wordpress",
        base_url="https://example.com",
        auth_stub_json={"token": "stub"},
    )
    db.add(site)

    channel = Channel(
        id=str(uuid.uuid4()),
        workspace_id=workspace.id,
        platform="youtube_shorts",
        auth_stub_json={"token": "stub"},
    )
    db.add(channel)

    competitor = Competitor(
        id=str(uuid.uuid4()),
        workspace_id=workspace.id,
        name="Competitor A",
        urls=["https://youtube.com/@competitor"],
    )
    db.add(competitor)

    idea = Idea(
        id=str(uuid.uuid4()),
        workspace_id=workspace.id,
        title="Demo Idea",
        hook="Проблема за 3 секунды",
        angle="Разбор + решение",
        cta="Подпишись",
        format="Problem→Solution",
        funnel_stage="TOFU",
        risk_score=0,
        status="idea_approval",
        rationale_json={"seed": True},
    )
    db.add(idea)

    package = ContentPackage(
        id=str(uuid.uuid4()),
        workspace_id=workspace.id,
        idea_id=idea.id,
        status="package_build",
        created_at=dt.datetime.utcnow(),
        updated_at=dt.datetime.utcnow(),
    )
    db.add(package)

    db.commit()
    return {"workspace_id": workspace.id, "package_id": package.id}
