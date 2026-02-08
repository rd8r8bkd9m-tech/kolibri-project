from __future__ import annotations

import uuid
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ...models import Idea
from ...schemas import IdeaGenerateRequest, IdeaOut, IdeaApprovalRequest
from ...services.ideation import generate_ideas
from ..deps import get_db_session

router = APIRouter(prefix="/ideas", tags=["ideas"])


@router.post("/generate", response_model=list[IdeaOut])
def generate(payload: IdeaGenerateRequest, db: Session = Depends(get_db_session)):
    drafts = generate_ideas(payload.niche, payload.limit)
    ideas: list[Idea] = []
    for draft in drafts:
        idea = Idea(
            id=str(uuid.uuid4()),
            workspace_id=payload.workspace_id,
            title=draft.title,
            hook=draft.hook,
            angle=draft.angle,
            cta=draft.cta,
            format=draft.format,
            funnel_stage=draft.funnel_stage,
            risk_score=0,
            status="idea_approval",
            rationale_json={"source": "mock"},
        )
        db.add(idea)
        ideas.append(idea)
    db.commit()
    return ideas


@router.post("/{idea_id}/approve", response_model=IdeaOut)
def approve_idea(idea_id: str, payload: IdeaApprovalRequest, db: Session = Depends(get_db_session)):
    idea = db.query(Idea).filter(Idea.id == idea_id).first()
    if not idea:
        raise HTTPException(status_code=404, detail="Idea not found")
    idea.status = "approved" if payload.decision == "ok" else payload.decision
    db.commit()
    db.refresh(idea)
    return idea


@router.get("", response_model=list[IdeaOut])
def list_ideas(db: Session = Depends(get_db_session)):
    return db.query(Idea).order_by(Idea.created_at.desc()).all()
