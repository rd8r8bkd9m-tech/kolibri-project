from __future__ import annotations

import hashlib
import json
import re
import shutil
import tempfile
import time
import zipfile
from pathlib import Path
from typing import Any, Iterable

from .project_paths import get_project_root
from .swarm_live_memory import (
    _iter_formula_docs,
    count_formula_docs,
    diff_formula_domain_counts,
    ensure_live_formula_memory_seeded,
    summarize_formula_docs_by_domain_map,
)

KPACK_MAGIC = "kpack"
KPACK_VERSION = 1
_TEXT_EXTENSIONS = {".txt", ".md"}
_SAFE_COMPONENT_RE = re.compile(r"[^0-9A-Za-z._-]+")


def _safe_component(value: str, fallback: str) -> str:
    cleaned = _SAFE_COMPONENT_RE.sub("-", (value or "").strip()).strip("-._")
    return (cleaned[:96] or fallback).lower()


def _iter_domain_docs(root: Path, domains: set[str] | None = None) -> Iterable[tuple[str, Path, Path]]:
    if not root.exists():
        return ()
    selected = {item.strip() for item in (domains or set()) if item and item.strip()}
    for path in _iter_formula_docs(root):
        try:
            relative = path.relative_to(root)
        except ValueError:
            relative = Path(path.name)
        domain = relative.parts[0] if len(relative.parts) > 1 else "root"
        if selected and domain not in selected:
            continue
        yield domain, relative, path


def _default_pack_dir() -> Path:
    return (get_project_root() / "data" / "swarm" / "kpacks").resolve()


def inspect_kpack(pack_path: str | Path) -> dict[str, Any]:
    path = Path(pack_path).expanduser().resolve()
    if not path.exists():
        raise FileNotFoundError(f"kpack not found: {path}")
    with zipfile.ZipFile(path, "r") as archive:
        names = archive.namelist()
        if "manifest.json" not in names:
            raise ValueError("manifest.json not found in kpack")
        manifest = json.loads(archive.read("manifest.json").decode("utf-8"))
        knowledge_files = [
            name
            for name in names
            if name.startswith("knowledge/") and Path(name).suffix.lower() in _TEXT_EXTENSIONS
        ]
    return {
        "path": str(path),
        "manifest": manifest,
        "documents": len(knowledge_files),
        "knowledge_files": knowledge_files,
    }


def export_kpack(
    *,
    source_root: str | Path | None = None,
    output_path: str | Path | None = None,
    package_id: str,
    title: str,
    language: str = "ru",
    domains: list[str] | None = None,
    description: str = "",
    default_query: str = "",
    source_kind: str = "live-memory",
) -> dict[str, Any]:
    root = Path(source_root).expanduser().resolve() if source_root else ensure_live_formula_memory_seeded()
    selected_domains = {item.strip() for item in (domains or []) if item and item.strip()}

    docs: list[tuple[str, Path, Path]] = list(_iter_domain_docs(root, selected_domains or None))
    if not docs:
        raise ValueError("No knowledge documents selected for kpack export")

    final_domains = sorted({domain for domain, _, _ in docs})
    package_id = _safe_component(package_id, "kolibri-pack")
    if output_path is None:
        output_dir = _default_pack_dir()
        output_dir.mkdir(parents=True, exist_ok=True)
        output = output_dir / f"{package_id}.kpack"
    else:
        output = Path(output_path).expanduser().resolve()
        output.parent.mkdir(parents=True, exist_ok=True)

    manifest = {
        "format": KPACK_MAGIC,
        "version": KPACK_VERSION,
        "id": package_id,
        "title": title.strip() or package_id,
        "language": language.strip() or "ru",
        "domains": final_domains,
        "description": description.strip(),
        "entrypoints": {
            "default_query": default_query.strip(),
        },
        "artifacts": {
            "knowledge_dir": "knowledge",
            "formula_index": None,
            "provenance": "provenance/genesis.json",
        },
        "exported_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "source_kind": source_kind,
    }
    provenance = {
        "created_at": manifest["exported_at"],
        "created_by": "Kolibri",
        "source_kind": source_kind,
        "genome_ref": None,
    }

    with tempfile.TemporaryDirectory(prefix="kolibri-kpack-export-") as tmp_dir_raw:
        tmp_dir = Path(tmp_dir_raw)
        (tmp_dir / "knowledge").mkdir(parents=True, exist_ok=True)
        (tmp_dir / "provenance").mkdir(parents=True, exist_ok=True)
        for _, relative, path in docs:
            target = tmp_dir / "knowledge" / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
        (tmp_dir / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        (tmp_dir / "provenance" / "genesis.json").write_text(
            json.dumps(provenance, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )

        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for file_path in sorted(tmp_dir.rglob("*")):
                if file_path.is_file():
                    archive.write(file_path, file_path.relative_to(tmp_dir).as_posix())

    return {
        "path": str(output),
        "filename": output.name,
        "package_id": package_id,
        "title": manifest["title"],
        "language": manifest["language"],
        "domains": final_domains,
        "documents": len(docs),
        "manifest": manifest,
    }


def import_kpack(
    *,
    pack_path: str | Path,
    target_root: str | Path | None = None,
) -> dict[str, Any]:
    pack = Path(pack_path).expanduser().resolve()
    if not pack.exists():
        raise FileNotFoundError(f"kpack not found: {pack}")

    live_root = Path(target_root).expanduser().resolve() if target_root else ensure_live_formula_memory_seeded()
    live_root.mkdir(parents=True, exist_ok=True)
    before_count = count_formula_docs(live_root)
    before_domains = summarize_formula_docs_by_domain_map(live_root)

    with tempfile.TemporaryDirectory(prefix="kolibri-kpack-import-") as tmp_dir_raw:
        tmp_dir = Path(tmp_dir_raw)
        with zipfile.ZipFile(pack, "r") as archive:
            archive.extractall(tmp_dir)

        manifest_path = tmp_dir / "manifest.json"
        if not manifest_path.exists():
            raise ValueError("manifest.json not found in kpack")
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("format") != KPACK_MAGIC:
            raise ValueError("Unsupported kpack format")
        if int(manifest.get("version", 0)) != KPACK_VERSION:
            raise ValueError("Unsupported kpack version")

        knowledge_dir_name = str((manifest.get("artifacts") or {}).get("knowledge_dir") or "knowledge")
        knowledge_dir = (tmp_dir / knowledge_dir_name).resolve()
        if not knowledge_dir.exists():
            raise ValueError("knowledge directory not found in kpack")

        imported = 0
        skipped = 0
        copied_paths: list[str] = []
        for file_path in sorted(knowledge_dir.rglob("*")):
            if not file_path.is_file() or file_path.suffix.lower() not in _TEXT_EXTENSIONS:
                continue
            relative = file_path.relative_to(knowledge_dir)
            target = live_root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                source_bytes = file_path.read_bytes()
                target_bytes = target.read_bytes()
                if source_bytes == target_bytes:
                    skipped += 1
                    continue
                digest = hashlib.sha1(source_bytes).hexdigest()[:10]
                target = target.with_name(f"{target.stem}_{digest}{target.suffix}")
            shutil.copy2(file_path, target)
            copied_paths.append(str(target))
            imported += 1

        after_count = count_formula_docs(live_root)
        after_domains = summarize_formula_docs_by_domain_map(live_root)
        import_log_dir = live_root / "_kpack_imports"
        import_log_dir.mkdir(parents=True, exist_ok=True)
        import_log_path = import_log_dir / f"{int(time.time())}_{_safe_component(str(manifest.get('id') or 'pack'), 'pack')}.json"
        import_log_path.write_text(
            json.dumps(
                {
                    "imported_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                    "pack_path": str(pack),
                    "manifest": manifest,
                    "imported_documents": imported,
                    "skipped_documents": skipped,
                    "copied_paths": copied_paths,
                },
                ensure_ascii=False,
                indent=2,
            ) + "\n",
            encoding="utf-8",
        )

    return {
        "pack_path": str(pack),
        "manifest": manifest,
        "imported_documents": imported,
        "skipped_documents": skipped,
        "copied_paths": copied_paths,
        "live_memory_path": str(live_root),
        "live_memory_document_count_before": before_count,
        "live_memory_document_count_after": after_count,
        "live_memory_document_delta": after_count - before_count,
        "domain_delta": diff_formula_domain_counts(before_domains, after_domains),
        "import_log_path": str(import_log_path),
    }

