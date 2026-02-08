from __future__ import annotations

from dataclasses import dataclass
from typing import Generator

from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker, declarative_base, Session


@dataclass
class Settings:
    database_url: str = "postgresql+psycopg2://content_factory:content_factory@db:5432/content_factory"


Base = declarative_base()

def get_engine(database_url: str | None = None):
    url = database_url or Settings().database_url
    return create_engine(url, pool_pre_ping=True)


engine = get_engine()
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)


def get_db() -> Generator[Session, None, None]:
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
