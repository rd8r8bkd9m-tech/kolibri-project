from __future__ import annotations

import datetime as dt
from pathlib import Path
from sqlalchemy.orm import Session

from .models import ContentPackage, Publication
from .services.seo import generate_seo_article
from .services.renderers import render_stub
from .services.publisher import publish_wordpress_stub, publish_youtube_stub
from .services.analytics import collect_metrics, calc_romi

ASSETS_BASE = Path("/data/assets")


def run_demo(db: Session) -> dict[str, str]:
    package = db.query(ContentPackage).first()
    if not package:
        return {"status": "no_packages"}

    seo = generate_seo_article("Demo", "Hook", "Angle", "CTA")
    package.seo_article_md = seo.article_md
    package.schema_json = seo.schema_json

    artifacts = render_stub(ASSETS_BASE, package.id, "youtube_shorts")
    package.assets_json = {"youtube_shorts": {"mp4": artifacts.mp4_path, "srt": artifacts.srt_path}}

    publication = Publication(
        id=package.id,
        content_package_id=package.id,
        channel_or_site_id="site",
        target="wordpress",
        scheduled_at=dt.datetime.utcnow(),
        utm_json={"utm_source": "content_factory", "utm_medium": "organic"},
        canonical_url="https://example.com/demo",
    )
    db.add(publication)

    wp = publish_wordpress_stub("Demo", seo.article_md)
    yt = publish_youtube_stub()
    publication.platform_post_id = wp.platform_post_id
    publication.published_at = wp.published_at
    publication.payload_json = {"wp_url": wp.url, "yt_url": yt.url}

    metrics = collect_metrics()
    attribution = calc_romi(leads=10, revenue=200.0, margin=120.0, cost=40.0)

    db.commit()
    return {
        "status": "done",
        "wp_url": wp.url,
        "yt_url": yt.url,
        "views": str(metrics.views),
        "romi": str(attribution.romi),
    }
