from __future__ import annotations

import json
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))


def test_export_kpack_creates_manifest_and_knowledge(tmp_path: Path) -> None:
    source = tmp_path / "formula_domains"
    (source / "law").mkdir(parents=True)
    (source / "law" / "0001.txt").write_text("# Право\n\nПраво — система норм.\n", encoding="utf-8")

    from backend.service.kpack import export_kpack

    result = export_kpack(
        source_root=source,
        output_path=tmp_path / "law.kpack",
        package_id="kolibri.law.demo",
        title="Право demo",
        domains=["law"],
        default_query="что такое право",
    )

    pack_path = Path(result["path"])
    assert pack_path.exists()
    with zipfile.ZipFile(pack_path, "r") as archive:
        manifest = json.loads(archive.read("manifest.json").decode("utf-8"))
        assert manifest["format"] == "kpack"
        assert manifest["domains"] == ["law"]
        assert "knowledge/law/0001.txt" in archive.namelist()


def test_import_kpack_copies_docs_into_live_memory(tmp_path: Path) -> None:
    source = tmp_path / "source"
    (source / "biology").mkdir(parents=True)
    (source / "biology" / "0001.txt").write_text("# Биология\n\nБиология изучает живые организмы.\n", encoding="utf-8")

    from backend.service.kpack import export_kpack, import_kpack

    pack = export_kpack(
        source_root=source,
        output_path=tmp_path / "biology.kpack",
        package_id="kolibri.biology.demo",
        title="Биология demo",
        domains=["biology"],
    )
    live = tmp_path / "live"
    result = import_kpack(pack_path=pack["path"], target_root=live)

    assert result["imported_documents"] == 1
    assert result["live_memory_document_delta"] == 1
    assert (live / "biology" / "0001.txt").exists()
    assert result["domain_delta"] == [{"domain": "biology", "before": 0, "after": 1, "delta": 1}]

