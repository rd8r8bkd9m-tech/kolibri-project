from __future__ import annotations

from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from ...demo import run_demo
from ..deps import get_db_session

router = APIRouter(prefix="/demo", tags=["demo"])


@router.post("/run")
def run(db: Session = Depends(get_db_session)):
    return run_demo(db)
