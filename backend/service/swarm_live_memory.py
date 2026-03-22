from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Iterable

from .project_paths import get_project_root

_TEXT_EXTENSIONS = {".txt", ".md"}


def _iter_formula_docs(root: Path) -> Iterable[Path]:
    if not root.exists():
        return ()
    return (
        path
        for path in root.rglob("*")
        if path.is_file() and path.suffix.lower() in _TEXT_EXTENSIONS
    )


def count_formula_docs(root: Path) -> int:
    return sum(1 for _ in _iter_formula_docs(root))


def summarize_formula_docs_by_domain(root: Path) -> list[dict[str, int | str]]:
    summary: dict[str, int] = {}
    if not root.exists():
        return []
    for path in _iter_formula_docs(root):
        try:
            relative = path.relative_to(root)
        except ValueError:
            domain = "root"
        else:
            domain = relative.parts[0] if len(relative.parts) > 1 else "root"
        summary[domain] = summary.get(domain, 0) + 1
    return [
        {"domain": domain, "documents": documents}
        for domain, documents in sorted(summary.items(), key=lambda item: (-item[1], item[0]))
    ]


def summarize_formula_docs_by_domain_map(root: Path) -> dict[str, int]:
    summary: dict[str, int] = {}
    if not root.exists():
        return summary
    for path in _iter_formula_docs(root):
        try:
            relative = path.relative_to(root)
        except ValueError:
            domain = "root"
        else:
            domain = relative.parts[0] if len(relative.parts) > 1 else "root"
        summary[domain] = summary.get(domain, 0) + 1
    return summary


def diff_formula_domain_counts(before: dict[str, int], after: dict[str, int]) -> list[dict[str, int | str]]:
    keys = sorted(set(before) | set(after))
    result: list[dict[str, int | str]] = []
    for key in keys:
        delta = int(after.get(key, 0)) - int(before.get(key, 0))
        if delta == 0:
            continue
        result.append(
            {
                "domain": key,
                "before": int(before.get(key, 0)),
                "after": int(after.get(key, 0)),
                "delta": delta,
            }
        )
    result.sort(key=lambda item: (-int(item["delta"]), str(item["domain"])))
    return result


def get_seed_formula_memory_dir() -> Path:
    return (get_project_root() / "data" / "formula_domains").resolve()


def get_live_formula_memory_dir() -> Path:
    override = os.environ.get("KOLIBRI_LIVE_FORMULA_MEMORY_PATH")
    if override:
        return Path(override).expanduser().resolve()
    return (get_project_root() / "data" / "swarm" / "live_formula_memory").resolve()


def ensure_live_formula_memory_seeded() -> Path:
    live_dir = get_live_formula_memory_dir()
    live_dir.mkdir(parents=True, exist_ok=True)

    seed_dir = get_seed_formula_memory_dir()
    if not seed_dir.exists():
        return live_dir

    for source in _iter_formula_docs(seed_dir):
        relative = source.relative_to(seed_dir)
        target = live_dir / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            shutil.copy2(source, target)
    return live_dir


def _slugify(value: str, fallback: str) -> str:
    cleaned = re.sub(r"[^0-9A-Za-zА-Яа-яЁё._-]+", "-", (value or "").strip(), flags=re.UNICODE)
    cleaned = cleaned.strip("-._")
    return cleaned[:64] or fallback


def _anchor_body_with_title(title: str, body: str) -> str:
    title = (title or "").strip()
    body = (body or "").strip()
    if not title or not body:
        return body

    first_line = next((line.strip() for line in body.splitlines() if line.strip()), "")
    if not first_line:
        return body

    lowered_title = title.lower()
    lowered_first = first_line.lower()
    if lowered_first.startswith(lowered_title):
        return body
    if re.match(rf"^{re.escape(title)}\s*[—:-]", first_line, flags=re.IGNORECASE):
        return body

    return f"{title} — {body}"


def ingest_text_document(
    text: str,
    *,
    title: str = "",
    source: str = "manual",
    category: str = "manual",
) -> Path:
    live_dir = ensure_live_formula_memory_seeded()
    safe_category = _slugify(category, "manual")
    safe_title = _slugify(title or text[:48], "document")
    safe_source = _slugify(source, "manual")
    digest = hashlib.sha1(f"{safe_title}|{safe_source}|{text}".encode("utf-8")).hexdigest()[:12]
    timestamp = time.strftime("%Y%m%d_%H%M%S", time.localtime())
    target_dir = live_dir / safe_category
    target_dir.mkdir(parents=True, exist_ok=True)
    target = target_dir / f"{timestamp}_{safe_title}_{digest}.txt"

    body = text.strip()
    header = title.strip() or safe_title.replace("-", " ")
    body = _anchor_body_with_title(header, body)
    target.write_text(f"# {header}\n\n{body}\n", encoding="utf-8")
    return target


def resolve_formula_trainer_binary() -> Path | None:
    root = get_project_root()
    for candidate in (
        root / "build" / "kolibri_formula_trainer",
        root / "build-logic" / "kolibri_formula_trainer",
    ):
        if candidate.exists() and os.access(candidate, os.X_OK):
            return candidate
    return None


def ingest_urls_via_trainer(
    urls: list[str],
    *,
    crawl: bool = False,
    depth: int = 1,
    max_pages: int = 12,
    delay_sec: float = 0.25,
    timeout_sec: int = 600,
) -> dict[str, object]:
    trainer = resolve_formula_trainer_binary()
    if trainer is None:
        raise FileNotFoundError("kolibri_formula_trainer binary not found")

    live_dir = ensure_live_formula_memory_seeded()
    before = count_formula_docs(live_dir)
    before_domains = summarize_formula_docs_by_domain_map(live_dir)

    urls = [url.strip() for url in urls if url and url.strip()]
    if not urls:
        raise ValueError("No URLs provided")

    command = [str(trainer), "--out-dir", str(live_dir), "--delay", f"{delay_sec:.2f}"]
    temp_path: str | None = None
    try:
        if crawl:
            command.extend(["--depth", str(max(1, depth)), "--max-pages", str(max(1, max_pages))])
            for url in urls:
                command.extend(["--crawl", url])
        else:
            if len(urls) == 1:
                command.extend(["--url", urls[0]])
            else:
                with tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False) as handle:
                    for url in urls:
                        handle.write(url)
                        handle.write("\n")
                    temp_path = handle.name
                command.extend(["--urls", temp_path])

        completed = subprocess.run(
            command,
            cwd=get_project_root(),
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=timeout_sec,
            check=True,
        )
    finally:
        if temp_path:
            try:
                os.unlink(temp_path)
            except OSError:
                pass

    after = count_formula_docs(live_dir)
    return {
        "command": command,
        "stdout": completed.stdout.strip(),
        "stderr": completed.stderr.strip(),
        "saved_documents": max(0, after - before),
        "live_memory_path": str(live_dir),
        "live_memory_document_count": after,
        "domain_delta": diff_formula_domain_counts(before_domains, summarize_formula_docs_by_domain_map(live_dir)),
    }
