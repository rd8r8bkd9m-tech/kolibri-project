from .workspaces import router as workspaces_router
from .brands import router as brands_router
from .channels import router as channels_router
from .competitors import router as competitors_router
from .source_videos import router as source_videos_router
from .ideas import router as ideas_router
from .content_items import router as content_items_router
from .publications import router as publications_router
from .dashboard import router as dashboard_router
from .demo import router as demo_router

__all__ = [
    "workspaces_router",
    "brands_router",
    "channels_router",
    "competitors_router",
    "source_videos_router",
    "ideas_router",
    "content_items_router",
    "publications_router",
    "dashboard_router",
    "demo_router",
]
