#!/usr/bin/env python3
"""Autonomous crawler that feeds docs/ingested for Kolibri training."""
from __future__ import annotations

import argparse
import datetime as dt
import html
import logging
import sqlite3
import sys
import time
import urllib.parse
import urllib.robotparser
from collections import deque
from html.parser import HTMLParser
from pathlib import Path
from typing import Iterable

import httpx

LOGGER = logging.getLogger("kolibri_crawler")
USER_AGENT = "KolibriCrawler/0.1 (+https://kolibri.example)"  # placeholder UA


def slugify(url: str) -> str:
    pieces = [chunk for chunk in url.replace("://", "-").split("/") if chunk]
    slug = "-".join(pieces)
    slug = "".join(ch if ch.isalnum() or ch in "-_" else "-" for ch in slug)
    slug = slug.strip("-")
    return slug[:80] or "doc"


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


class LinkExtractor(HTMLParser):
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
        href = href.strip()
        if href.startswith("#") or href.startswith("mailto:") or href.startswith("javascript:"):
            return
        absolute = urllib.parse.urljoin(self.base_url, href)
        self.links.append(absolute)


def extract_text_and_links(body: str, base_url: str) -> tuple[str, list[str]]:
    parser = TextExtractor()
    parser.feed(body)
    link_parser = LinkExtractor(base_url)
    link_parser.feed(body)
    return parser.get_text(), link_parser.links


def normalize_url(url: str) -> str | None:
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme not in {"http", "https"}:
        return None
    netloc = parsed.netloc.lower()
    path = parsed.path or "/"
    normalized = urllib.parse.urlunsplit((parsed.scheme, netloc, path, "", ""))
    return normalized


def ensure_db(conn: sqlite3.Connection) -> None:
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS pages (
            url TEXT PRIMARY KEY,
            status TEXT NOT NULL,
            depth INTEGER NOT NULL,
            last_fetch REAL
        )
        """
    )
    conn.commit()


def enqueue_urls(conn: sqlite3.Connection, urls: Iterable[str], depth: int) -> int:
    added = 0
    for url in urls:
        try:
            conn.execute(
                "INSERT OR IGNORE INTO pages (url, status, depth, last_fetch) VALUES (?, 'pending', ?, NULL)",
                (url, depth),
            )
            if conn.total_changes:
                added += 1
        except sqlite3.Error as exc:
            LOGGER.warning("Failed to enqueue %s: %s", url, exc)
    conn.commit()
    return added


def next_pending(conn: sqlite3.Connection, max_depth: int) -> tuple[str, int] | None:
    cursor = conn.execute(
        """
        SELECT url, depth
        FROM pages
        WHERE status='pending' AND depth <= ?
        ORDER BY depth ASC, last_fetch ASC NULLS FIRST
        LIMIT 1
        """,
        (max_depth,),
    )
    row = cursor.fetchone()
    return (row[0], row[1]) if row else None


def update_status(conn: sqlite3.Connection, url: str, status: str) -> None:
    conn.execute(
        "UPDATE pages SET status=?, last_fetch=? WHERE url=?",
        (status, time.time(), url),
    )
    conn.commit()


def robots_allowed(url: str, robot_cache: dict[str, urllib.robotparser.RobotFileParser]) -> bool:
    parsed = urllib.parse.urlsplit(url)
    base = f"{parsed.scheme}://{parsed.netloc}"
    parser = robot_cache.get(base)
    if not parser:
        robots_url = urllib.parse.urljoin(base, "/robots.txt")
        parser = urllib.robotparser.RobotFileParser()
        try:
            parser.set_url(robots_url)
            parser.read()
        except Exception:
            parser = urllib.robotparser.RobotFileParser()
            parser.parse("")  # allow by default if robots not reachable
        robot_cache[base] = parser
    return parser.can_fetch(USER_AGENT, url)


def write_document(output_dir: Path, url: str, text: str) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = dt.datetime.utcnow().isoformat() + "Z"
    slug = slugify(url)
    output_path = output_dir / f"{slug}.md"
    header = [
        f"# Source: {url}",
        f"_Fetched: {timestamp}_",
        "",
    ]
    with output_path.open("w", encoding="utf-8") as handle:
        handle.write("\n".join(header))
        handle.write(text.strip())
        handle.write("\n")
    return output_path


def within_domains(url: str, allow_domains: set[str]) -> bool:
    if not allow_domains:
        return True
    hostname = urllib.parse.urlsplit(url).hostname or ""
    return any(hostname == domain or hostname.endswith(f".{domain}") for domain in allow_domains)


def crawl_once(
    seeds: Iterable[str],
    output_dir: Path,
    db_path: Path,
    allow_domains: set[str],
    max_pages: int,
    max_depth: int,
    timeout: float,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    db_path.parent.mkdir(parents=True, exist_ok=True)

    conn = sqlite3.connect(db_path)
    ensure_db(conn)

    normalized_seeds = [normalize_url(url) for url in seeds]
    normalized_seeds = [url for url in normalized_seeds if url]
    enqueue_urls(conn, normalized_seeds, depth=0)

    robot_cache: dict[str, urllib.robotparser.RobotFileParser] = {}
    processed = 0

    while processed < max_pages:
        pending = next_pending(conn, max_depth)
        if not pending:
            LOGGER.info("No pending URLs left, stopping.")
            break
        url, depth = pending
        if not within_domains(url, allow_domains):
            LOGGER.info("Skipping %s (outside allowed domains)", url)
            update_status(conn, url, "skipped")
            continue
        if not robots_allowed(url, robot_cache):
            LOGGER.info("Skipping %s (robots.txt disallows)", url)
            update_status(conn, url, "skipped")
            continue

        try:
            LOGGER.info("Fetching %s (depth=%s)", url, depth)
            with httpx.Client(timeout=timeout, follow_redirects=True, headers={"User-Agent": USER_AGENT}) as client:
                response = client.get(url)
                response.raise_for_status()
                content_type = response.headers.get("Content-Type", "")
                body = response.text
            links: list[str] = []
            if "text/html" in content_type.lower():
                text, links = extract_text_and_links(body, url)
            else:
                text = html.unescape(body)
            doc_path = write_document(output_dir, url, text)
            LOGGER.info("Saved %s", doc_path)
            update_status(conn, url, "done")
            processed += 1
            if depth + 1 <= max_depth and links:
                normalized_links = []
                for link in links:
                    normalized = normalize_url(link)
                    if normalized:
                        normalized_links.append(normalized)
                if normalized_links:
                    enqueue_urls(conn, normalized_links, depth=depth + 1)
        except Exception as exc:  # pragma: no cover - network operations
            LOGGER.warning("Failed to fetch %s: %s", url, exc)
            update_status(conn, url, "failed")

    conn.close()
    LOGGER.info("Crawl finished: processed=%s", processed)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Kolibri autonomous crawler")
    parser.add_argument("--output", type=Path, default=Path("docs/ingested"), help="Directory to store documents")
    parser.add_argument("--db", type=Path, default=Path("build/crawler/crawler.db"), help="SQLite DB path for queue")
    parser.add_argument("--max-pages", type=int, default=20, help="Maximum pages to fetch per run")
    parser.add_argument("--max-depth", type=int, default=2, help="Maximum crawl depth from seeds")
    parser.add_argument("--timeout", type=float, default=15.0, help="HTTP timeout per request")
    parser.add_argument("--allow-domain", action="append", default=[], help="Allowed domain (repeatable). Empty=any.")
    parser.add_argument("--seeds", nargs="*", default=[], help="Seed URLs to enqueue")
    parser.add_argument("--seed-file", type=Path, help="File with seed URLs (one per line)")
    parser.add_argument("--log-level", default="INFO", help="Logging level (default: INFO)")
    return parser.parse_args()


def load_seed_file(path: Path | None) -> list[str]:
    if not path:
        return []
    if not path.exists():
        raise FileNotFoundError(f"Seed file not found: {path}")
    seeds: list[str] = []
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line and not line.startswith("#"):
                seeds.append(line)
    return seeds


def main() -> int:
    args = parse_args()
    logging.basicConfig(level=args.log_level.upper(), format="[%(levelname)s] %(message)s")
    seeds = list(args.seeds)
    seeds.extend(load_seed_file(args.seed_file))
    if not seeds:
        LOGGER.error("No seeds provided. Use --seeds or --seed-file.")
        return 1
    allow_domains = {domain.lower() for domain in args.allow_domain if domain}
    crawl_once(
        seeds=seeds,
        output_dir=args.output,
        db_path=args.db,
        allow_domains=allow_domains,
        max_pages=args.max_pages,
        max_depth=args.max_depth,
        timeout=args.timeout,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
