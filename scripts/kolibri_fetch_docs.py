#!/usr/bin/env python3
"""Utility to fetch external web pages and store them as training docs."""
from __future__ import annotations

import argparse
import datetime as dt
import html
import re
import sys
from html.parser import HTMLParser
from pathlib import Path
from typing import Iterable

import httpx


def slugify(url: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9]+", "-", url)
    slug = slug.strip("-")
    if not slug:
        slug = "doc"
    return slug[:60]


class TextExtractor(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.chunks: list[str] = []

    def handle_data(self, data: str) -> None:
        text = data.strip()
        if text:
            self.chunks.append(text)

    def get_text(self) -> str:
        return " ".join(self.chunks)


def extract_text(html_body: str) -> str:
    parser = TextExtractor()
    parser.feed(html_body)
    return parser.get_text()


def fetch_url(url: str, timeout: float) -> str:
    with httpx.Client(timeout=timeout, follow_redirects=True) as client:
        response = client.get(url)
        response.raise_for_status()
        content_type = response.headers.get("Content-Type", "")
        body = response.text
        if "text/html" in content_type.lower():
            return extract_text(body)
        return html.unescape(body)


def write_document(output_dir: Path, url: str, content: str) -> Path:
    slug = slugify(url)
    timestamp = dt.datetime.utcnow().isoformat() + "Z"
    output_path = output_dir / f"{slug}.md"
    header = [
        f"# Source: {url}",
        f"_Fetched: {timestamp}_",
        "",
    ]
    with output_path.open("w", encoding="utf-8") as handle:
        handle.write("\n".join(header))
        handle.write(content.strip())
        handle.write("\n")
    return output_path


def iter_urls(args: argparse.Namespace) -> Iterable[str]:
    urls = list(args.urls)
    if args.url_file:
        file_path = Path(args.url_file)
        if not file_path.exists():
            raise FileNotFoundError(f"URL list file not found: {file_path}")
        with file_path.open("r", encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if line and not line.startswith("#"):
                    urls.append(line)
    return urls


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fetch external webpages and store them as docs for Kolibri knowledge pipeline."
    )
    parser.add_argument("--output", type=Path, default=Path("docs/ingested"), help="Directory to store fetched docs")
    parser.add_argument("--timeout", type=float, default=15.0, help="HTTP timeout in seconds")
    parser.add_argument("--url-file", type=Path, help="File with URLs (one per line)")
    parser.add_argument("urls", nargs="*", help="URLs to fetch")
    args = parser.parse_args()

    urls = list(iter_urls(args))
    if not urls:
        print("No URLs provided.", file=sys.stderr)
        return 1

    args.output.mkdir(parents=True, exist_ok=True)
    successes = 0
    failures = 0
    for url in urls:
        try:
            print(f"[fetch] downloading {url}")
            content = fetch_url(url, args.timeout)
            doc_path = write_document(args.output, url, content)
            print(f"[fetch] saved {doc_path}")
            successes += 1
        except Exception as exc:  # pragma: no cover - network dependent
            print(f"[fetch] failed to fetch {url}: {exc}", file=sys.stderr)
            failures += 1

    print(f"[fetch] completed: {successes} success, {failures} failed")
    return 0 if successes > 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
