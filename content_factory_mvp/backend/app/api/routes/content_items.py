from __future__ import annotations

import uuid
from pathlib import Path
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ...models import ContentItem, Idea, Approval, Publication
from ...schemas import ContentCreateRequest, ContentItemOut, ContentActionRequest, PublicationOut, PublicationScheduleRequest
from ...services.script_storyboard import generate_script
from ...services.production import produce_assets
from ...services.orchestrator import next_state, stamp_updated
from ...services.publisher import publish_stub
from ..deps import get_db_session

router = APIRouter(prefix="/content-items", tags=["content-items"])

ASSETS_BASE = Path("/data/assets")


@router.post("/create-from-idea", response_model=ContentItemOut)
def create_from_idea(payload: ContentCreateRequest, db: Session = Depends(get_db_session)):
    idea = db.query(Idea).filter(Idea.id == payload.idea_id).first()
    if not idea:
        raise HTTPException(status_code=404, detail="Idea not found")
    item = ContentItem(
        id=str(uuid.uuid4()),
        workspace_id=payload.workspace_id,
        idea_id=idea.id,
        status="production",
        updated_at=stamp_updated(),
    )
    db.add(item)
    db.commit()
    db.refresh(item)
    return item


@router.post("/{item_id}/generate-script", response_model=ContentItemOut)
def generate_script_endpoint(item_id: str, db: Session = Depends(get_db_session)):
    item = db.query(ContentItem).filter(ContentItem.id == item_id).first()
    if not item:
        raise HTTPException(status_code=404, detail="Content item not found")
    if not item.idea_id:
        raise HTTPException(status_code=400, detail="Content item missing idea")
    idea = db.query(Idea).filter(Idea.id == item.idea_id).first()
    bundle = generate_script(idea.title, idea.hook, idea.angle, idea.cta)
    item.script = bundle.script
    item.storyboard_json = bundle.storyboard_json
    item.updated_at = stamp_updated()
    db.commit()
    db.refresh(item)
    return item


@router.post("/{item_id}/produce", response_model=ContentItemOut)
def produce(item_id: str, db: Session = Depends(get_db_session)):
    item = db.query(ContentItem).filter(ContentItem.id == item_id).first()
    if not item:
        raise HTTPException(status_code=404, detail="Content item not found")
    artifacts = produce_assets(ASSETS_BASE, item.id, item.script)
    item.assets_json = {
        "subtitles": artifacts.subtitles_path,
        "thumbnail": artifacts.thumbnail_path,
        "metadata": artifacts.metadata_path,
        "render_stub": artifacts.render_stub_path,
    }
    item.render_path = artifacts.render_stub_path
    item.status = "content_approval"
    item.updated_at = stamp_updated()
    db.commit()
    db.refresh(item)
    return item


@router.post("/{item_id}/approve", response_model=ContentItemOut)
def approve(item_id: str, payload: ContentActionRequest, db: Session = Depends(get_db_session)):
    item = db.query(ContentItem).filter(ContentItem.id == item_id).first()
    if not item:
        raise HTTPException(status_code=404, detail="Content item not found")
    approval = Approval(
        id=str(uuid.uuid4()),
        content_item_id=item.id,
        stage=item.status,
        reviewer=payload.reviewer,
        decision="approved",
        comment=payload.comment,
    )
    db.add(approval)
    item.status = next_state(item.status)
    item.updated_at = stamp_updated()
    db.commit()
    db.refresh(item)
    return item


@router.post("/{item_id}/schedule", response_model=PublicationOut)
def schedule(item_id: str, payload: PublicationScheduleRequest, db: Session = Depends(get_db_session)):
    item = db.query(ContentItem).filter(ContentItem.id == item_id).first()
    if not item:
        raise HTTPException(status_code=404, detail="Content item not found")
    publication = Publication(
        id=str(uuid.uuid4()),
        content_item_id=item.id,
        platform="youtube_shorts",
        scheduled_at=payload.scheduled_at,
        utm_json={"utm_source": "content_factory", "utm_medium": "organic"},
        caption=item.metadata_json.get("title", "Demo Short"),
        tags=item.metadata_json.get("tags", ["demo"]),
    )
    db.add(publication)
    db.commit()
    db.refresh(publication)
    return publication


@router.post("/{item_id}/publish", response_model=PublicationOut)
def publish(item_id: str, db: Session = Depends(get_db_session)):
    publication = db.query(Publication).filter(Publication.content_item_id == item_id).first()
    if not publication:
        raise HTTPException(status_code=404, detail="Publication not found")
    result = publish_stub(item_id)
    publication.platform_post_id = result.platform_post_id
    publication.published_at = result.published_at
    db.commit()
    db.refresh(publication)
    return publication


@router.get("", response_model=list[ContentItemOut])
def list_items(db: Session = Depends(get_db_session)):
    return db.query(ContentItem).order_by(ContentItem.created_at.desc()).all()
