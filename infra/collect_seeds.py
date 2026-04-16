#!/usr/bin/env python3
"""Collect seed URLs from official catalogs and write them into crawler_seeds.txt."""
from __future__ import annotations

import argparse
import json
import sys
import urllib.parse
from html.parser import HTMLParser
from pathlib import Path
from typing import Iterable

import httpx


class LinkExtractor(HTMLParser):
    """Extract all http(s) links in a HTML document."""

    def __init__(self, base_url: str) -> None:
        super().__init__()
        self.base_url = base_url
        self.links: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag.lower() != "a":
            return
        href = dict(attrs).get("href")
        if not href:
            return
        absolute = urllib.parse.urljoin(self.base_url, href.strip())
        if absolute.startswith("http://") or absolute.startswith("https://"):
            self.links.append(absolute)


def normalize_url(url: str) -> str:
    parsed = urllib.parse.urlsplit(url)
    scheme = parsed.scheme.lower()
    if scheme not in {"http", "https"}:
        return ""
    netloc = parsed.netloc.lower()
    path = parsed.path or "/"
    return urllib.parse.urlunsplit((scheme, netloc, path, "", ""))


def allowed(url: str, domains: Iterable[str]) -> bool:
    hostname = urllib.parse.urlsplit(url).hostname or ""
    for domain in domains:
        domain = domain.lower()
        if hostname == domain or hostname.endswith(f".{domain}"):
            return True
    return False


def fetch_links(index_url: str) -> list[str]:
    try:
        with httpx.Client(timeout=20.0, follow_redirects=True) as client:
            resp = client.get(index_url)
            resp.raise_for_status()
            parser = LinkExtractor(index_url)
            parser.feed(resp.text)
            return parser.links
    except Exception as exc:  # pragma: no cover - network dependent
        print(f"[collect-seeds] failed to fetch {index_url}: {exc}", file=sys.stderr)
        return []


def load_catalog(path: Path) -> list[dict[str, object]]:
    with path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    sources = payload.get("sources", [])
    if not isinstance(sources, list):
        raise ValueError("catalog JSON must contain list under 'sources'")
    return sources


def main() -> int:
    parser = argparse.ArgumentParser(description="Collect seed URLs from curated catalogs.")
    parser.add_argument("--catalog", type=Path, default=Path("seeds/catalog.json"), help="Path to catalog JSON")
    parser.add_argument("--output", type=Path, default=Path("crawler_seeds.txt"), help="Output file for seeds")
    parser.add_argument("--per-source", type=int, default=25, help="Max links per source")
    parser.add_argument("--min-links", type=int, default=5, help="Minimum links to include a source")
    args = parser.parse_args()

    if not args.catalog.exists():
        print(f"[collect-seeds] catalog file not found: {args.catalog}", file=sys.stderr)
        return 1

    sources = load_catalog(args.catalog)
    aggregated: list[str] = []
    seen: set[str] = set()

    for source in sources:
        name = source.get("name", "unknown")
        index_url = source.get("index")
        allow_domains = source.get("allow_domains", [])
        if not isinstance(index_url, str):
            print(f"[collect-seeds] skip {name}: missing index URL", file=sys.stderr)
            continue
        if not isinstance(allow_domains, list) or not allow_domains:
            print(f"[collect-seeds] skip {name}: missing allow_domains", file=sys.stderr)
            continue

        links = fetch_links(index_url)
        curated = []
        for link in links:
            normalized = normalize_url(link)
            if not normalized or normalized in seen:
                continue
            if allowed(normalized, allow_domains):
                curated.append(normalized)
                seen.add(normalized)
            if len(curated) >= args.per_source:
                break

        if len(curated) < args.min_links:
            print(f"[collect-seeds] skip {name}: only {len(curated)} links after filtering", file=sys.stderr)
            continue

        print(f"[collect-seeds] {name}: {len(curated)} links")
        aggregated.extend(curated)

    if not aggregated:
        print("[collect-seeds] no links collected", file=sys.stderr)
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as handle:
        for url in sorted(aggregated):
            handle.write(f"{url}\n")

    print(f"[collect-seeds] wrote {len(aggregated)} links to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
