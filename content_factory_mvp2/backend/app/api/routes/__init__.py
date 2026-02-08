from .workspaces import router as workspaces_router
from .brand_guidelines import router as brand_guidelines_router
from .sites import router as sites_router
from .channels import router as channels_router
from .competitors import router as competitors_router
from .sources import router as sources_router
from .ideas import router as ideas_router
from .packages import router as packages_router
from .publications import router as publications_router
from .dashboard import router as dashboard_router

__all__ = [
    "workspaces_router",
    "brand_guidelines_router",
    "sites_router",
    "channels_router",
    "competitors_router",
    "sources_router",
    "ideas_router",
    "packages_router",
    "publications_router",
    "dashboard_router",
]
