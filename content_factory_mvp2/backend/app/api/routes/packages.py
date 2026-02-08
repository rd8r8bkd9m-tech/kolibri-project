from __future__ import annotations

import datetime as dt
import uuid
from pathlib import Path
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ...models import ContentPackage, Idea, Approval, Publication, RenderArtifact, RenderProfile
from ...schemas import PackageCreateRequest, PackageOut, PackageActionRequest, RenderRequest, ScheduleRequest
from ...services.seo import generate_seo_article
from ...services.renderers import render_stub
from ...services.publisher import publish_wordpress_stub, publish_youtube_stub
from ...services.orchestrator import next_state
from ..deps import get_db_session

router = APIRouter(prefix="/packages", tags=["packages"])
ASSETS_BASE = Path("/data/assets")


@router.post("/create-from-idea", response_model=PackageOut)
def create_from_idea(payload: PackageCreateRequest, db: Session = Depends(get_db_session)):
    idea = db.query(Idea).filter(Idea.id == payload.idea_id).first()
    if not idea:
        raise HTTPException(status_code=404, detail="Idea not found")
    package = ContentPackage(
        id=str(uuid.uuid4()),
        workspace_id=payload.workspace_id,
        idea_id=idea.id,
        status="package_build",
        updated_at=dt.datetime.utcnow(),
    )
    db.add(package)
    db.commit()
    db.refresh(package)
    return package


@router.post("/{package_id}/generate-seo", response_model=PackageOut)
def generate_seo(package_id: str, db: Session = Depends(get_db_session)):
    package = db.query(ContentPackage).filter(ContentPackage.id == package_id).first()
    if not package:
        raise HTTPException(status_code=404, detail="Package not found")
    seo = generate_seo_article("Demo", "Hook", "Angle", "CTA")
    package.seo_article_md = seo.article_md
    package.faq_json = seo.faq_json
    package.schema_json = seo.schema_json
    package.internal_links_json = seo.internal_links_json
    package.updated_at = dt.datetime.utcnow()
    db.commit()
    db.refresh(package)
    return package


@router.post("/{package_id}/generate-video-script", response_model=PackageOut)
def generate_video_script(package_id: str, db: Session = Depends(get_db_session)):
    package = db.query(ContentPackage).filter(ContentPackage.id == package_id).first()
    if not package:
        raise HTTPException(status_code=404, detail="Package not found")
    package.video_script = "HOOK/SCENES/CTA"
    package.updated_at = dt.datetime.utcnow()
    db.commit()
    db.refresh(package)
    return package


@router.post("/{package_id}/render", response_model=PackageOut)
def render(package_id: str, payload: RenderRequest, db: Session = Depends(get_db_session)):
    package = db.query(ContentPackage).filter(ContentPackage.id == package_id).first()
    if not package:
        raise HTTPException(status_code=404, detail="Package not found")
    package.assets_json = package.assets_json or {}
    for profile in payload.profiles:
        artifacts = render_stub(ASSETS_BASE, package.id, profile)
        render_profile = db.query(RenderProfile).filter(RenderProfile.id == profile).first()
        if not render_profile:
            render_profile = RenderProfile(id=profile, workspace_id=package.workspace_id, target=profile, spec_json={})
            db.add(render_profile)
        artifact = RenderArtifact(
            id=str(uuid.uuid4()),
            content_package_id=package.id,
            render_profile_id=render_profile.id,
            type="bundle",
            path=str(Path(artifacts.mp4_path).parent),
            meta_json={"mp4": artifacts.mp4_path, "srt": artifacts.srt_path, "png": artifacts.png_path},
        )
        db.add(artifact)
        package.assets_json[profile] = artifact.meta_json
    package.status = "qa_approval"
    package.updated_at = dt.datetime.utcnow()
    db.commit()
    db.refresh(package)
    return package


@router.post("/{package_id}/approve", response_model=PackageOut)
def approve(package_id: str, payload: PackageActionRequest, db: Session = Depends(get_db_session)):
    package = db.query(ContentPackage).filter(ContentPackage.id == package_id).first()
    if not package:
        raise HTTPException(status_code=404, detail="Package not found")
    approval = Approval(
        id=str(uuid.uuid4()),
        content_package_id=package.id,
        stage=package.status,
        reviewer=payload.reviewer,
        decision="approved",
        comment=payload.comment,
    )
    db.add(approval)
    package.status = next_state(package.status)
    package.updated_at = dt.datetime.utcnow()
    db.commit()
    db.refresh(package)
    return package


@router.post("/{package_id}/schedule", response_model=dict)
def schedule(package_id: str, payload: ScheduleRequest, db: Session = Depends(get_db_session)):
    package = db.query(ContentPackage).filter(ContentPackage.id == package_id).first()
    if not package:
        raise HTTPException(status_code=404, detail="Package not found")
    publication = Publication(
        id=str(uuid.uuid4()),
        content_package_id=package.id,
        channel_or_site_id="site",
        target="wordpress",
        scheduled_at=payload.scheduled_at,
        utm_json={"utm_source": "content_factory", "utm_medium": "organic"},
        canonical_url="https://example.com/demo",
    )
    db.add(publication)
    package.status = "publishing"
    package.updated_at = dt.datetime.utcnow()
    db.commit()
    return {"publication_id": publication.id}


@router.post("/{package_id}/publish", response_model=dict)
def publish(package_id: str, db: Session = Depends(get_db_session)):
    publication = db.query(Publication).filter(Publication.content_package_id == package_id).first()
    if not publication:
        raise HTTPException(status_code=404, detail="Publication not found")
    wp = publish_wordpress_stub("Demo", "Content")
    yt = publish_youtube_stub()
    publication.platform_post_id = wp.platform_post_id
    publication.published_at = wp.published_at
    publication.payload_json = {"wp_url": wp.url, "yt_url": yt.url}
    package = db.query(ContentPackage).filter(ContentPackage.id == package_id).first()
    if package:
        package.status = "analytics"
        package.updated_at = dt.datetime.utcnow()
    db.commit()
    return {"wp_url": wp.url, "yt_url": yt.url}


@router.get("", response_model=list[PackageOut])
def list_packages(db: Session = Depends(get_db_session)):
    return db.query(ContentPackage).order_by(ContentPackage.created_at.desc()).all()
