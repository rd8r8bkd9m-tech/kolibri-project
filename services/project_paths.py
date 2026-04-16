from __future__ import annotations

import os
from functools import lru_cache
from pathlib import Path


@lru_cache(maxsize=1)
def get_project_root() -> Path:
    """
    Resolve Kolibri project root for local, container and CI environments.

    Priority:
    1. KOLIBRI_PROJECT_ROOT env var (explicit override)
    2. Repository root inferred from this file location
    3. Current working directory
    """
    candidates: list[Path] = []

    env_root = os.getenv("KOLIBRI_PROJECT_ROOT")
    if env_root:
        candidates.append(Path(env_root).expanduser())

    # services/project_paths.py -> repo root is parents[1]
    candidates.append(Path(__file__).resolve().parents[1])
    candidates.append(Path.cwd())

    for candidate in candidates:
        root = candidate.resolve()
        if (root / "core").exists() and (root / "services").exists():
            return root

    # Last-resort fallback (keeps behavior deterministic even in odd layouts)
    return candidates[0].resolve()

