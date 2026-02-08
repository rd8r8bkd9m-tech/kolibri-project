from __future__ import annotations

import uuid
from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from ...models import Workspace
from ...schemas import WorkspaceCreate, WorkspaceOut
from ..deps import get_db_session

router = APIRouter(prefix="/workspaces", tags=["workspaces"])


@router.post("", response_model=WorkspaceOut)
def create_workspace(payload: WorkspaceCreate, db: Session = Depends(get_db_session)):
    workspace = Workspace(id=str(uuid.uuid4()), name=payload.name)
    db.add(workspace)
    db.commit()
    db.refresh(workspace)
    return workspace


@router.get("", response_model=list[WorkspaceOut])
def list_workspaces(db: Session = Depends(get_db_session)):
    return db.query(Workspace).order_by(Workspace.created_at.desc()).all()
