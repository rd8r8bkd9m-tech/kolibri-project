from __future__ import annotations

import uuid
from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from ...models import Site
from ...schemas import SiteCreate, SiteOut
from ..deps import get_db_session

router = APIRouter(prefix="/sites", tags=["sites"])


@router.post("", response_model=SiteOut)
def create_site(payload: SiteCreate, db: Session = Depends(get_db_session)):
    site = Site(id=str(uuid.uuid4()), **payload.model_dump())
    db.add(site)
    db.commit()
    db.refresh(site)
    return site


@router.get("", response_model=list[SiteOut])
def list_sites(db: Session = Depends(get_db_session)):
    return db.query(Site).order_by(Site.created_at.desc()).all()
