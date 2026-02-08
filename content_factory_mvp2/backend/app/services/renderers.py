from __future__ import annotations

import base64
from dataclasses import dataclass
from pathlib import Path


_MIN_PNG = (
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR4nGNg"
    "YAAAAAMAASsJTYQAAAAASUVORK5CYII="
)


@dataclass
class RenderArtifacts:
    mp4_path: str
    srt_path: str
    png_path: str
    md_path: str
    meta_path: str


def render_stub(base_dir: Path, package_id: str, profile: str) -> RenderArtifacts:
    target_dir = base_dir / package_id / profile
    target_dir.mkdir(parents=True, exist_ok=True)

    mp4_path = target_dir / "render.mp4"
    mp4_path.write_bytes(b"\x00\x00\x00\x18ftypmp42\x00\x00\x00\x00mp42isom")

    srt_path = target_dir / "subtitles.srt"
    srt_path.write_text("1\n00:00:00,000 --> 00:00:03,000\nDemo\n", encoding="utf-8")

    png_path = target_dir / "thumbnail.png"
    png_path.write_bytes(base64.b64decode(_MIN_PNG))

    md_path = target_dir / "post.md"
    md_path.write_text("# Demo\n\nShort text for social media.", encoding="utf-8")

    meta_path = target_dir / "meta.json"
    meta_path.write_text("{\"title\":\"Demo\",\"cta\":\"Подпишись\"}", encoding="utf-8")

    return RenderArtifacts(
        mp4_path=str(mp4_path),
        srt_path=str(srt_path),
        png_path=str(png_path),
        md_path=str(md_path),
        meta_path=str(meta_path),
    )
