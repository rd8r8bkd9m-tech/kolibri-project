from __future__ import annotations

import uuid
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ...models import Idea
from ...schemas import IdeaGenerateRequest, IdeaOut, PackageActionRequest
from ..deps import get_db_session

router = APIRouter(prefix="/ideas", tags=["ideas"])


@router.post("/generate", response_model=list[IdeaOut])
def generate(payload: IdeaGenerateRequest, db: Session = Depends(get_db_session)):
    ideas: list[Idea] = []
    for idx in range(payload.limit):
        idea = Idea(
            id=str(uuid.uuid4()),
            workspace_id=payload.workspace_id,
            title=f"{payload.niche}: идея {idx + 1}",
            hook="Проблема за 3 секунды",
            angle="Короткое решение + выгода",
            cta="Подпишись и сохрани",
            format="Problem→Solution" if idx % 2 == 0 else "Case/Proof",
            funnel_stage="TOFU",
            risk_score=0,
            status="idea_approval",
            rationale_json={"predict_kpi": {"views": 10000 + idx * 100}},
        )
        db.add(idea)
        ideas.append(idea)
    db.commit()
    return ideas


@router.post("/{idea_id}/approve", response_model=IdeaOut)
def approve(idea_id: str, payload: PackageActionRequest, db: Session = Depends(get_db_session)):
    idea = db.query(Idea).filter(Idea.id == idea_id).first()
    if not idea:
        raise HTTPException(status_code=404, detail="Idea not found")
    idea.status = "approved"
    db.commit()
    db.refresh(idea)
    return idea


@router.get("", response_model=list[IdeaOut])
def list_ideas(db: Session = Depends(get_db_session)):
    return db.query(Idea).order_by(Idea.created_at.desc()).all()
