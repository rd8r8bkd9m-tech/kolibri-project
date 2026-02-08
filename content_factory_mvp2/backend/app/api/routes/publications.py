from __future__ import annotations

import uuid
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ...models import Publication, MetricsDaily, Attribution
from ...schemas import MetricsOut, AttributionOut, AttributionCalcRequest
from ...services.analytics import collect_metrics, calc_romi
from ..deps import get_db_session

router = APIRouter(prefix="/publications", tags=["publications"])


@router.post("/{publication_id}/metrics/collect", response_model=MetricsOut)
def collect(publication_id: str, db: Session = Depends(get_db_session)):
    publication = db.query(Publication).filter(Publication.id == publication_id).first()
    if not publication:
        raise HTTPException(status_code=404, detail="Publication not found")
    metrics = collect_metrics()
    record = MetricsDaily(
        id=str(uuid.uuid4()),
        publication_id=publication.id,
        views=metrics.views,
        watch_time=metrics.watch_time,
        retention_proxy=metrics.retention_proxy,
        likes=metrics.likes,
        comments=metrics.comments,
        shares=metrics.shares,
        ctr_proxy=metrics.ctr_proxy,
        site_clicks=metrics.site_clicks,
    )
    db.add(record)
    db.commit()
    db.refresh(record)
    return record


@router.post("/{publication_id}/romi/calc", response_model=AttributionOut)
def calc(publication_id: str, payload: AttributionCalcRequest, db: Session = Depends(get_db_session)):
    publication = db.query(Publication).filter(Publication.id == publication_id).first()
    if not publication:
        raise HTTPException(status_code=404, detail="Publication not found")
    result = calc_romi(payload.leads, payload.revenue, payload.margin, payload.cost)
    record = Attribution(
        id=str(uuid.uuid4()),
        publication_id=publication.id,
        leads=result.leads,
        revenue=result.revenue,
        margin=result.margin,
        cost=result.cost,
        romi=result.romi,
        roas=result.roas,
    )
    db.add(record)
    db.commit()
    db.refresh(record)
    return record
