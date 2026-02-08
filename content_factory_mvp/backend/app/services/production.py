from __future__ import annotations

import base64
from dataclasses import dataclass
from pathlib import Path


_MIN_PNG = (
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR4nGNg"
    "YAAAAAMAASsJTYQAAAAASUVORK5CYII="
)


@dataclass
class ProductionArtifacts:
    subtitles_path: str
    thumbnail_path: str
    metadata_path: str
    render_stub_path: str


def produce_assets(base_dir: Path, content_id: str, script: str) -> ProductionArtifacts:
    asset_dir = base_dir / content_id
    asset_dir.mkdir(parents=True, exist_ok=True)

    subtitles = asset_dir / "subtitles.srt"
    subtitles.write_text(
        "1\n00:00:00,000 --> 00:00:03,000\n" + script[:70] + "\n",
        encoding="utf-8",
    )

    thumbnail = asset_dir / "thumbnail.png"
    thumbnail.write_bytes(base64.b64decode(_MIN_PNG))

    metadata = asset_dir / "metadata.json"
    metadata.write_text(
        "{\n  \"title\": \"Demo Short\",\n  \"tags\": [\"demo\", \"kolibri\"],\n  \"description\": \"Demo content\"\n}\n",
        encoding="utf-8",
    )

    render_stub = asset_dir / "render_stub.mp4"
    render_stub.write_bytes(b"\x00\x00\x00\x18ftypmp42\x00\x00\x00\x00mp42isom")

    return ProductionArtifacts(
        subtitles_path=str(subtitles),
        thumbnail_path=str(thumbnail),
        metadata_path=str(metadata),
        render_stub_path=str(render_stub),
    )
