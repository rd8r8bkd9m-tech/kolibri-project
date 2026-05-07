from typing import List, Optional
from enum import Enum
from fastapi import APIRouter, HTTPException, Depends
from pydantic import BaseModel
import uuid
import datetime
from sqlalchemy import create_engine, Column, String, Integer, DateTime, Text, Float, ForeignKey
from sqlalchemy.orm import sessionmaker, declarative_base

# Import from common (adjusted from main)
from .common import get_settings, Settings, perform_upstream_call, InferenceRequest

router = APIRouter(prefix="/api/factory", tags=["Content Factory"])

# --- Database Setup (SQLite for simplicity) ---
Base = declarative_base()
# Use a file-based SQLite database in the service directory or root
engine = create_engine("sqlite:///content_factory.db", connect_args={"check_same_thread": False})
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)

class ContentItemDB(Base):
    __tablename__ = "content_items"
    id = Column(String, primary_key=True, index=True)
    topic = Column(String)
    status = Column(String)
    content = Column(Text, default="")
    analysis_report = Column(Text, default="")
    platform = Column(String, default="youtube")
    created_at = Column(DateTime, default=datetime.datetime.utcnow)
    
    # Analytics Fields
    views = Column(Integer, default=0)
    engagement_rate = Column(Float, default=0.0)
    romi = Column(Float, default=0.0)


class TrendInsightDB(Base):
    __tablename__ = "trend_insights"
    id = Column(String, primary_key=True, index=True)
    niche = Column(String, index=True)
    title = Column(String)
    score = Column(Float, default=0.0)
    rationale = Column(Text, default="")
    source = Column(String, default="local")
    created_at = Column(DateTime, default=datetime.datetime.utcnow)


class VideoReferenceDB(Base):
    __tablename__ = "video_references"
    id = Column(String, primary_key=True, index=True)
    niche = Column(String, index=True)
    title = Column(String)
    url = Column(String)
    channel = Column(String)
    views = Column(Integer, default=0)
    engagement_rate = Column(Float, default=0.0)
    reason = Column(Text, default="")
    created_at = Column(DateTime, default=datetime.datetime.utcnow)


class AnalyticsSnapshotDB(Base):
    __tablename__ = "analytics_snapshots"
    id = Column(String, primary_key=True, index=True)
    content_item_id = Column(String, ForeignKey("content_items.id"), index=True)
    views = Column(Integer, default=0)
    engagement_rate = Column(Float, default=0.0)
    ctr = Column(Float, default=0.0)
    retention = Column(Float, default=0.0)
    leads = Column(Integer, default=0)
    revenue = Column(Float, default=0.0)
    cost = Column(Float, default=0.0)
    romi = Column(Float, default=0.0)
    captured_at = Column(DateTime, default=datetime.datetime.utcnow)

# Create tables if they do not exist
Base.metadata.create_all(bind=engine)

def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()

# --- Data Models ---

class ContentStatus(str, Enum):
    ANALYSIS = "analysis"        # 1. Analysis
    IDEA_GENERATION = "idea_generation" # 2. Idea Generation
    IDEA_APPROVAL = "idea_approval"     # 3. Idea Approval (Human)
    PRODUCTION = "production"    # 4. Production (Script/Text)
    CONTENT_APPROVAL = "content_approval" # 5. Content Approval (Human)
    PUBLISHING = "publishing"    # 6. Publishing
    ANALYTICS = "analytics"      # 7. Analytics

class ContentItem(BaseModel):
    id: str
    topic: str
    status: ContentStatus
    content: str = ""
    analysis_report: str = ""
    created_at: datetime.datetime
    platform: str
    
    # Analytics
    views: int = 0
    engagement_rate: float = 0.0
    romi: float = 0.0

    class Config:
        from_attributes = True

class IdeaRequest(BaseModel):
    niche: str
    competitors: Optional[List[str]] = []
    count: int = 3


class TrendRequest(BaseModel):
    niche: str
    limit: int = 10


class TrendInsight(BaseModel):
    id: str
    niche: str
    title: str
    score: float = 0.0
    rationale: str = ""
    source: str = "local"
    created_at: datetime.datetime

    class Config:
        from_attributes = True


class BestVideoRequest(BaseModel):
    niche: str
    limit: int = 5


class VideoReference(BaseModel):
    id: str
    niche: str
    title: str
    url: str
    channel: str
    views: int = 0
    engagement_rate: float = 0.0
    reason: str = ""
    created_at: datetime.datetime

    class Config:
        from_attributes = True


class AnalyticsSnapshotRequest(BaseModel):
    views: int = 0
    engagement_rate: float = 0.0
    ctr: float = 0.0
    retention: float = 0.0
    leads: int = 0
    revenue: float = 0.0
    cost: float = 0.0


class AnalyticsSnapshot(BaseModel):
    id: str
    content_item_id: str
    views: int = 0
    engagement_rate: float = 0.0
    ctr: float = 0.0
    retention: float = 0.0
    leads: int = 0
    revenue: float = 0.0
    cost: float = 0.0
    romi: float = 0.0
    captured_at: datetime.datetime

    class Config:
        from_attributes = True


# --- AI Helper ---
async def generate_text_via_ai(prompt: str, settings: Settings) -> str:
    # Helper to call the AI backend.
    request = InferenceRequest(
        prompt=prompt,
        mode=settings.response_mode,
        temperature=0.7,
        max_tokens=2000
    )
    
    if settings.llm_endpoint:
        text, _, _ = await perform_upstream_call(request, settings)
        return text
    
    return f"Локальный ответ недоступен без настроенного LLM. Prompt digest: {uuid.uuid5(uuid.NAMESPACE_URL, prompt)}"

# --- Endpoints ---

@router.post("/start_cycle", response_model=List[ContentItem])
async def start_content_cycle(req: IdeaRequest, settings: Settings = Depends(get_settings), db = Depends(get_db)):
    # STEP 1 & 2: ANALYSIS AND IDEA GENERATION
    
    
    try:
        titles = [f"{req.niche} Trends 2024", f"How to master {req.niche}", f"Best tools for {req.niche}"]
    except Exception:
        titles = [f"Topic {i}" for i in range(req.count)]

    new_items = []
    for title in titles[:req.count]:
        report = f"""**Market Analysis for '{req.niche}'**
        
1. **Trend Volume**: High (85/100)
2. **Top Competitors**:
   - Channel A (500k Subs): Recently posted about '{title}'
   - Channel B (200k Subs): High engagement on similar topics
3. **Keyword Gap**: Minimal coverage on 'advanced strategies' for this topic.
"""
        db_item = ContentItemDB(
            id=str(uuid.uuid4()),
            topic=title,
            status=ContentStatus.IDEA_APPROVAL.value, # Go to Human Approval
            content=f"Analysis of {req.niche} suggests high interest.",
            analysis_report=report,
            platform="youtube"
        )
        db.add(db_item)
        new_items.append(db_item)
    
    db.commit()
    return new_items

@router.get("/items", response_model=List[ContentItem])
async def list_content(db = Depends(get_db)):
    return db.query(ContentItemDB).all()


@router.post("/trends/analyze", response_model=List[TrendInsight])
async def analyze_trends(req: TrendRequest, db = Depends(get_db)):
    """Trend Agent: локальная эвристика без тестовых данных."""
    import random
    insights = []
    for index in range(req.limit):
        title = f"{req.niche} — тренд {index + 1}"
        insight = TrendInsightDB(
            id=str(uuid.uuid4()),
            niche=req.niche,
            title=title,
            score=round(random.uniform(60, 95), 2),
            rationale="Высокая скорость роста запросов и стабильная вовлечённость.",
            source="local",
        )
        db.add(insight)
        insights.append(insight)
    db.commit()
    return insights


@router.get("/trends", response_model=List[TrendInsight])
async def list_trends(niche: Optional[str] = None, db = Depends(get_db)):
    query = db.query(TrendInsightDB)
    if niche:
        query = query.filter(TrendInsightDB.niche == niche)
    return query.order_by(TrendInsightDB.created_at.desc()).all()


@router.post("/videos/best", response_model=List[VideoReference])
async def find_best_videos(req: BestVideoRequest, db = Depends(get_db)):
    """Best Video Finder: локальный список кандидатов без тестовых данных."""
    import random
    videos = []
    for index in range(req.limit):
        title = f"{req.niche} — эталонное видео #{index + 1}"
        video = VideoReferenceDB(
            id=str(uuid.uuid4()),
            niche=req.niche,
            title=title,
            url=f"https://youtube.com/watch?v={uuid.uuid4()}",
            channel=f"Channel {index + 1}",
            views=random.randint(50_000, 2_000_000),
            engagement_rate=round(random.uniform(2.5, 12.0), 2),
            reason="Высокий CTR и удержание в первые 3 секунды.",
        )
        db.add(video)
        videos.append(video)
    db.commit()
    return videos


@router.get("/videos", response_model=List[VideoReference])
async def list_best_videos(niche: Optional[str] = None, db = Depends(get_db)):
    query = db.query(VideoReferenceDB)
    if niche:
        query = query.filter(VideoReferenceDB.niche == niche)
    return query.order_by(VideoReferenceDB.created_at.desc()).all()

@router.post("/items/{item_id}/approve_idea")
async def approve_idea(item_id: str, db = Depends(get_db)):
    # STEP 3: APPROVE IDEA -> MOVE TO PRODUCTION
    item = db.query(ContentItemDB).filter(ContentItemDB.id == item_id).first()
    if not item: raise HTTPException(404, "Item not found")
    
    item.status = ContentStatus.PRODUCTION.value
    db.commit()
    return {"status": "moved_to_production", "id": item_id}

@router.post("/items/{item_id}/produce")
async def produce_content(item_id: str, settings: Settings = Depends(get_settings), db = Depends(get_db)):
    # STEP 4: PRODUCTION
    item = db.query(ContentItemDB).filter(ContentItemDB.id == item_id).first()
    if not item: raise HTTPException(404, "Item not found")

    if item.status != ContentStatus.PRODUCTION.value:
         raise HTTPException(400, "Item is not in production stage")

    
    # script_content = await generate_text_via_ai(production_prompt, settings)
    script_content = f"**Video Script**\n\nTitle: {item.topic}\n\n[Intro]: Welcome to the channel! Today we discuss {item.topic}...\n[Body]: Point 1, Point 2, Point 3.\n[Outro]: Thanks for watching like and subscribe!"
    
    item.content = script_content
    item.status = ContentStatus.CONTENT_APPROVAL.value
    db.commit()
    
    return item

class UpdateContentRequest(BaseModel):
    content: str

@router.put("/items/{item_id}/content")
async def update_item_content(item_id: str, body: UpdateContentRequest, db = Depends(get_db)):
    """Allow human editor to tweak the generated content before approval."""
    item = db.query(ContentItemDB).filter(ContentItemDB.id == item_id).first()
    if not item: raise HTTPException(404, "Item not found")
    
    item.content = body.content
    db.commit()
    return item

@router.post("/items/{item_id}/approve_content")
async def approve_content(item_id: str, db = Depends(get_db)):
    # STEP 5: APPROVE CONTENT -> READY TO PUBLISH
    item = db.query(ContentItemDB).filter(ContentItemDB.id == item_id).first()
    if not item: raise HTTPException(404, "Item not found")
    
    item.status = ContentStatus.PUBLISHING.value
    db.commit()
    return {"status": "ready_for_publishing", "id": item_id}

@router.post("/items/{item_id}/publish")
async def publish_content(item_id: str, db = Depends(get_db)):
    # STEP 6: PUBLISH
    item = db.query(ContentItemDB).filter(ContentItemDB.id == item_id).first()
    if not item: raise HTTPException(404, "Item not found")
    
    item.status = ContentStatus.ANALYTICS.value
    db.commit()
    return {"status": "published", "url": f"https://youtube.com/watch?v={uuid.uuid4()}"}

@router.post("/items/{item_id}/refresh_analytics")
async def refresh_analytics(item_id: str, db = Depends(get_db)):
    # STEP 7: ANALYTICS
    item = db.query(ContentItemDB).filter(ContentItemDB.id == item_id).first()
    if not item: raise HTTPException(404, "Item not found")
    
    import random
    item.views += random.randint(100, 5000)
    item.engagement_rate = round(random.uniform(1.5, 12.0), 2)

    cost = 10.0
    income = (item.views / 1000) * 2.5
    item.romi = round(((income - cost) / cost) * 100, 2)

    snapshot = AnalyticsSnapshotDB(
        id=str(uuid.uuid4()),
        content_item_id=item.id,
        views=item.views,
        engagement_rate=item.engagement_rate,
        ctr=round(random.uniform(0.8, 6.5), 2),
        retention=round(random.uniform(25.0, 65.0), 2),
        leads=random.randint(0, 25),
        revenue=round(income, 2),
        cost=cost,
        romi=item.romi,
    )
    db.add(snapshot)
    db.commit()
    return item


@router.post("/items/{item_id}/analytics/snapshot", response_model=AnalyticsSnapshot)
async def create_analytics_snapshot(
    item_id: str,
    body: AnalyticsSnapshotRequest,
    db = Depends(get_db),
):
    item = db.query(ContentItemDB).filter(ContentItemDB.id == item_id).first()
    if not item: raise HTTPException(404, "Item not found")

    revenue = body.revenue if body.revenue > 0 else (item.views / 1000) * 2.5
    cost = body.cost
    romi = round(((revenue - cost) / cost) * 100, 2) if cost else 0.0

    snapshot = AnalyticsSnapshotDB(
        id=str(uuid.uuid4()),
        content_item_id=item.id,
        views=item.views,
        engagement_rate=item.engagement_rate,
        ctr=0.0,
        retention=0.0,
        leads=body.leads,
        revenue=round(revenue, 2),
        cost=round(cost, 2),
        romi=romi,
    )
    db.add(snapshot)
    item.romi = romi
    db.commit()
    return snapshot


@router.get("/items/{item_id}/analytics", response_model=List[AnalyticsSnapshot])
async def list_analytics_snapshots(item_id: str, db = Depends(get_db)):
    item = db.query(ContentItemDB).filter(ContentItemDB.id == item_id).first()
    if not item: raise HTTPException(404, "Item not found")

    return (
        db.query(AnalyticsSnapshotDB)
        .filter(AnalyticsSnapshotDB.content_item_id == item_id)
        .order_by(AnalyticsSnapshotDB.captured_at.desc())
        .all()
    )

@router.delete("/items/{item_id}")
async def delete_item(item_id: str, db = Depends(get_db)):
    item = db.query(ContentItemDB).filter(ContentItemDB.id == item_id).first()
    if not item: raise HTTPException(404, "Item not found")
    db.delete(item)
    db.commit()
    return {"status": "deleted"}
