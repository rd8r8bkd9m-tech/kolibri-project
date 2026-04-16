#!/usr/bin/env python3
"""
build_wiki_corpus_stream.py

Потоково собирает большой текстовый корпус из Wikimedia .bz2 dump
и сохраняет в data/corpus/wiki_stream_2gb чанками <= 1MB.

Цель: получить заданный объём корпуса (по умолчанию 2 GiB) без распаковки
всего дампа на диск.
"""

from __future__ import annotations

import argparse
import html
import json
import re
import subprocess
import sys
import time
from pathlib import Path

DEFAULT_URL = "https://dumps.wikimedia.org/ruwiki/latest/ruwiki-latest-pages-articles-multistream.xml.bz2"

TEXT_RE = re.compile(r"<text[^>]*>(.*?)</text>", re.DOTALL | re.IGNORECASE)


def clean_wikitext(raw: str) -> str:
    text = html.unescape(raw)
    text = re.sub(r"<!--.*?-->", " ", text, flags=re.DOTALL)
    text = re.sub(r"<ref[^>/]*/>", " ", text, flags=re.IGNORECASE)
    text = re.sub(r"<ref[^>]*>.*?</ref>", " ", text, flags=re.DOTALL | re.IGNORECASE)
    text = re.sub(r"<[^>]+>", " ", text)
    text = re.sub(r"\{\|.*?\|\}", " ", text, flags=re.DOTALL)

    # Удаляем шаблоны в несколько проходов.
    for _ in range(5):
        next_text = re.sub(r"\{\{[^{}]{1,3000}\}\}", " ", text)
        if next_text == text:
            break
        text = next_text

    text = re.sub(r"\[\[(?:Файл|File|Категория|Category):[^\]]+\]\]", " ", text, flags=re.IGNORECASE)
    text = re.sub(r"\[\[(?:[^|\]]+\|)?([^\]]+)\]\]", r"\1", text)
    text = re.sub(r"\[(?:https?://[^\s\]]+)\s+([^\]]+)\]", r"\1", text)
    text = re.sub(r"\[(?:https?://[^\s\]]+)\]", " ", text)
    text = re.sub(r"https?://\S+", " ", text)
    text = re.sub(r"={2,}\s*([^=]+?)\s*={2,}", r"\1", text)

    text = re.sub(r"[ \t]+", " ", text)
    text = re.sub(r"\n{2,}", "\n", text)
    text = text.strip()
    return text


def ensure_output_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def write_chunk(out_dir: Path, index: int, docs: list[str]) -> tuple[Path, int]:
    body = "\n\n".join(docs).strip() + "\n"
    out_path = out_dir / f"wiki_chunk_{index:06d}.txt"
    out_path.write_text(body, encoding="utf-8")
    return out_path, len(body.encode("utf-8"))


def stream_collect(
    url: str,
    out_dir: Path,
    target_bytes: int,
    chunk_bytes: int,
    min_doc_chars: int,
) -> dict[str, int]:
    ensure_output_dir(out_dir)

    # Очистка старых чанков.
    for old in out_dir.glob("wiki_chunk_*.txt"):
        old.unlink()

    started = time.time()
    total_bytes = 0
    total_docs = 0
    chunk_index = 1
    docs: list[str] = []
    docs_size = 0
    buf = ""

    print(f"[build] source: {url}")
    print(f"[build] target bytes: {target_bytes:,}")
    print(f"[build] output: {out_dir}")
    sys.stdout.flush()
    curl_cmd = [
        "curl",
        "-L",
        "--fail",
        "--retry",
        "6",
        "--retry-delay",
        "2",
        "--connect-timeout",
        "20",
        "--user-agent",
        "KolibriCorpusBuilder/1.0 (+https://kolibriai.ru)",
        url,
    ]
    bz_cmd = ["bzip2", "-dc"]

    curl_proc = subprocess.Popen(curl_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert curl_proc.stdout is not None
    bz_proc = subprocess.Popen(
        bz_cmd,
        stdin=curl_proc.stdout,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    curl_proc.stdout.close()
    assert bz_proc.stdout is not None

    try:
        while total_bytes < target_bytes:
            raw_chunk = bz_proc.stdout.read(1024 * 1024)
            if not raw_chunk:
                break

            buf += raw_chunk.decode("utf-8", errors="ignore")

            search_pos = 0
            while True:
                m = TEXT_RE.search(buf, search_pos)
                if not m:
                    break
                search_pos = m.end()
                cleaned = clean_wikitext(m.group(1))
                if len(cleaned) < min_doc_chars:
                    continue

                encoded_len = len(cleaned.encode("utf-8")) + 2
                if docs_size + encoded_len > chunk_bytes and docs:
                    out_path, written = write_chunk(out_dir, chunk_index, docs)
                    total_bytes += written
                    chunk_index += 1
                    docs = []
                    docs_size = 0
                    if chunk_index % 128 == 0 or total_bytes >= target_bytes:
                        elapsed = max(1.0, time.time() - started)
                        mb = total_bytes / (1024 * 1024)
                        print(
                            f"[build] chunks={chunk_index-1} docs={total_docs:,} "
                            f"written={mb:.1f}MB speed={mb/elapsed:.2f}MB/s last={out_path.name}"
                        )
                        sys.stdout.flush()
                    if total_bytes >= target_bytes:
                        break

                docs.append(cleaned)
                docs_size += encoded_len
                total_docs += 1

            if search_pos:
                buf = buf[search_pos:]
            if len(buf) > 4_000_000:
                buf = buf[-2_000_000:]
    finally:
        try:
            bz_proc.terminate()
        except Exception:
            pass
        try:
            curl_proc.terminate()
        except Exception:
            pass
        bz_proc.wait(timeout=10)
        curl_proc.wait(timeout=10)

    if total_bytes <= 0:
        curl_err = b""
        bz_err = b""
        try:
            if curl_proc.stderr:
                curl_err = curl_proc.stderr.read()
        except Exception:
            pass
        try:
            if bz_proc.stderr:
                bz_err = bz_proc.stderr.read()
        except Exception:
            pass
        raise RuntimeError(
            "No corpus bytes written. "
            f"curl_rc={curl_proc.returncode}, bzip2_rc={bz_proc.returncode}, "
            f"curl_err={curl_err.decode('utf-8', errors='ignore')[:300]}, "
            f"bzip2_err={bz_err.decode('utf-8', errors='ignore')[:300]}"
        )

    if docs and total_bytes < target_bytes:
        out_path, written = write_chunk(out_dir, chunk_index, docs)
        total_bytes += written
        chunk_index += 1
        print(f"[build] final chunk: {out_path.name}")

    elapsed = max(1.0, time.time() - started)
    summary = {
        "bytes_written": total_bytes,
        "documents_written": total_docs,
        "chunks_written": max(0, chunk_index - 1),
        "elapsed_sec": int(elapsed),
    }
    (out_dir / "manifest.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    return summary


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build large wiki corpus stream")
    parser.add_argument("--url", default=DEFAULT_URL, help="Wikimedia bz2 URL")
    parser.add_argument(
        "--output-dir",
        default="data/corpus/wiki_stream_2gb",
        help="Output dir for corpus chunks",
    )
    parser.add_argument(
        "--target-bytes",
        type=int,
        default=2 * 1024 * 1024 * 1024,
        help="Target corpus size in bytes",
    )
    parser.add_argument(
        "--chunk-bytes",
        type=int,
        default=950_000,
        help="Single output txt size limit (bytes)",
    )
    parser.add_argument(
        "--min-doc-chars",
        type=int,
        default=180,
        help="Minimum cleaned doc length",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    out_dir = Path(args.output_dir).resolve()
    summary = stream_collect(
        url=args.url,
        out_dir=out_dir,
        target_bytes=max(1, int(args.target_bytes)),
        chunk_bytes=max(100_000, int(args.chunk_bytes)),
        min_doc_chars=max(50, int(args.min_doc_chars)),
    )
    print(
        "[build] done: "
        f"{summary['bytes_written']:,} bytes, "
        f"{summary['documents_written']:,} docs, "
        f"{summary['chunks_written']:,} chunks"
    )
    return 0 if summary["bytes_written"] >= int(args.target_bytes) else 2


if __name__ == "__main__":
    raise SystemExit(main())
