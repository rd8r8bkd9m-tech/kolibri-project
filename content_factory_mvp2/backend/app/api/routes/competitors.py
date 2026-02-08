from __future__ import annotations

import uuid
from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from ...models import Competitor
from ...schemas import CompetitorCreate, CompetitorOut
from ..deps import get_db_session

router = APIRouter(prefix="/competitors", tags=["competitors"])


@router.post("", response_model=CompetitorOut)
def create_competitor(payload: CompetitorCreate, db: Session = Depends(get_db_session)):
    competitor = Competitor(id=str(uuid.uuid4()), **payload.model_dump())
    db.add(competitor)
    db.commit()
    db.refresh(competitor)
    return competitor


@router.get("", response_model=list[CompetitorOut])
def list_competitors(db: Session = Depends(get_db_session)):
    return db.query(Competitor).order_by(Competitor.created_at.desc()).all()
