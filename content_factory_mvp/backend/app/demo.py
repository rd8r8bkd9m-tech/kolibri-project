from __future__ import annotations

import datetime as dt
from pathlib import Path
from sqlalchemy.orm import Session
from .models import ContentItem, Publication
from .services.script_storyboard import generate_script
from .services.production import produce_assets
from .services.publisher import publish_stub
from .services.analytics import collect_metrics, calc_romi
from .services.orchestrator import stamp_updated

ASSETS_BASE = "/data/assets"


def run_demo(db: Session) -> dict[str, str]:
    item = db.query(ContentItem).first()
    if not item:
        return {"status": "no_content_items"}

    # Script
    bundle = generate_script("Demo", "Hook", "Angle", "CTA")
    item.script = bundle.script
    item.storyboard_json = bundle.storyboard_json
    item.updated_at = stamp_updated()

    # Production
    artifacts = produce_assets(Path(ASSETS_BASE), item.id, item.script)
    item.assets_json = {
        "subtitles": artifacts.subtitles_path,
        "thumbnail": artifacts.thumbnail_path,
        "metadata": artifacts.metadata_path,
        "render_stub": artifacts.render_stub_path,
    }
    item.render_path = artifacts.render_stub_path
    item.status = "content_approval"

    # Publish
    publication = Publication(
        id=item.id,
        content_item_id=item.id,
        platform="youtube_shorts",
        scheduled_at=dt.datetime.utcnow(),
    )
    db.add(publication)
    result = publish_stub(item.id)
    publication.platform_post_id = result.platform_post_id
    publication.published_at = result.published_at

    # Metrics + ROMI
    metrics = collect_metrics()
    attribution = calc_romi(leads=10, revenue=250.0, margin=150.0, cost=30.0)

    db.commit()
    return {
        "status": "done",
        "render_stub": item.render_path,
        "publication_url": result.url,
        "views": str(metrics.views),
        "romi": str(attribution.romi),
    }
