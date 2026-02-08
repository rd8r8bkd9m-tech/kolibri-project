from __future__ import annotations

import uuid
from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from ...models import BrandGuidelines
from ...schemas import BrandGuidelinesCreate, BrandGuidelinesOut
from ..deps import get_db_session

router = APIRouter(prefix="/brand-guidelines", tags=["brand-guidelines"])


@router.post("", response_model=BrandGuidelinesOut)
def create_brand(payload: BrandGuidelinesCreate, db: Session = Depends(get_db_session)):
    brand = BrandGuidelines(id=str(uuid.uuid4()), **payload.model_dump())
    db.add(brand)
    db.commit()
    db.refresh(brand)
    return brand


@router.get("", response_model=list[BrandGuidelinesOut])
def list_brands(db: Session = Depends(get_db_session)):
    return db.query(BrandGuidelines).order_by(BrandGuidelines.created_at.desc()).all()
