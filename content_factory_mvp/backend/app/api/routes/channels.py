from __future__ import annotations

import uuid
from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from ...models import Channel
from ...schemas import ChannelCreate, ChannelOut
from ..deps import get_db_session

router = APIRouter(prefix="/channels", tags=["channels"])


@router.post("", response_model=ChannelOut)
def create_channel(payload: ChannelCreate, db: Session = Depends(get_db_session)):
    channel = Channel(id=str(uuid.uuid4()), **payload.model_dump())
    db.add(channel)
    db.commit()
    db.refresh(channel)
    return channel


@router.get("", response_model=list[ChannelOut])
def list_channels(db: Session = Depends(get_db_session)):
    return db.query(Channel).order_by(Channel.created_at.desc()).all()
