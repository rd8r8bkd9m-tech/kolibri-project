from __future__ import annotations

import uuid
from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from ...models import SourceItem
from ...schemas import SourceCollectRequest
from ...services.scoring import score_video, score_article
from ..deps import get_db_session

router = APIRouter(prefix="/sources", tags=["sources"])


@router.post("/collect")
def collect_sources(payload: SourceCollectRequest, db: Session = Depends(get_db_session)):
    items = []
    for idx in range(payload.limit):
        if idx % 2 == 0:
            score = score_video(0.8, 0.6, 0.7, 0.8, 0.6, 0.7)
            item = SourceItem(
                id=str(uuid.uuid4()),
                workspace_id=payload.workspace_id,
                type="video",
                platform="youtube",
                url=f"https://youtube.com/watch?v=mock-{idx}",
                title=f"{payload.niche} video {idx}",
                transcript_or_text="",
                snapshot_json={"score": score.score, "rationale": score.rationale},
            )
        else:
            score = score_article(0.7, 0.5, 0.6, 0.8, 0.7)
            item = SourceItem(
                id=str(uuid.uuid4()),
                workspace_id=payload.workspace_id,
                type="article",
                platform="web",
                url=f"https://example.com/article-{idx}",
                title=f"{payload.niche} статья {idx}",
                transcript_or_text="",
                snapshot_json={"score": score.score, "rationale": score.rationale},
            )
        db.add(item)
        items.append(item)
    db.commit()
    return items
