from __future__ import annotations

from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from ...models import ContentPackage, Publication, Attribution
from ...schemas import DashboardSummary
from ..deps import get_db_session

router = APIRouter(prefix="/dashboard", tags=["dashboard"])


@router.get("/summary", response_model=DashboardSummary)
def summary(db: Session = Depends(get_db_session)):
    total_packages = db.query(ContentPackage).count()
    total_publications = db.query(Publication).count()
    attributions = db.query(Attribution).all()
    avg_romi = 0.0
    if attributions:
        avg_romi = round(sum(item.romi for item in attributions) / len(attributions), 2)
    return DashboardSummary(
        total_packages=total_packages,
        total_publications=total_publications,
        avg_romi=avg_romi,
    )
