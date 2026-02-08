from __future__ import annotations

import uuid
from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from ...models import SourceVideo
from ...schemas import SourceVideoCollectRequest, SourceVideoOut
from ...services.best_video_finder import find_best_videos
from ..deps import get_db_session

router = APIRouter(prefix="/source-videos", tags=["source-videos"])


@router.post("/collect", response_model=list[SourceVideoOut])
def collect_source_videos(payload: SourceVideoCollectRequest, db: Session = Depends(get_db_session)):
    scored = find_best_videos(payload.niche, payload.limit)
    videos: list[SourceVideo] = []
    for item in scored:
        video = SourceVideo(
            id=str(uuid.uuid4()),
            workspace_id=payload.workspace_id,
            platform="youtube",
            url=item.url,
            title=item.title,
            transcript="",
            snapshot_json={"score": item.score, "rationale": item.rationale},
        )
        db.add(video)
        videos.append(video)
    db.commit()
    return videos


@router.get("", response_model=list[SourceVideoOut])
def list_source_videos(db: Session = Depends(get_db_session)):
    return db.query(SourceVideo).order_by(SourceVideo.collected_at.desc()).all()
