from __future__ import annotations

import uuid
import datetime as dt
from sqlalchemy.orm import Session

from .models import Workspace, BrandGuidelines, Channel, Competitor, SourceVideo, Idea, ContentItem
from .services.best_video_finder import find_best_videos


def seed(db: Session) -> dict[str, str]:
    workspace = Workspace(id=str(uuid.uuid4()), name="Kolibri Demo")
    db.add(workspace)

    brand = BrandGuidelines(
        id=str(uuid.uuid4()),
        workspace_id=workspace.id,
        tone="friendly",
        banned_phrases=["быстрое обогащение"],
        forbidden_claims=["гарантируем результат"],
        required_disclaimers=["Не является финансовым советом"],
        style_notes="Короткие фразы, деловой тон",
    )
    db.add(brand)

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

    for video in find_best_videos("AI", 20):
        db.add(
            SourceVideo(
                id=str(uuid.uuid4()),
                workspace_id=workspace.id,
                platform="youtube",
                url=video.url,
                title=video.title,
                transcript="",
                snapshot_json={"score": video.score, "rationale": video.rationale},
            )
        )

    ideas: list[Idea] = []
    for idx in range(10):
        idea = Idea(
            id=str(uuid.uuid4()),
            workspace_id=workspace.id,
            title=f"Idea {idx + 1}",
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
        ideas.append(idea)

    for idea in ideas[:3]:
        item = ContentItem(
            id=str(uuid.uuid4()),
            workspace_id=workspace.id,
            idea_id=idea.id,
            status="production",
            created_at=dt.datetime.utcnow(),
            updated_at=dt.datetime.utcnow(),
        )
        db.add(item)

    db.commit()
    return {"workspace_id": workspace.id}
