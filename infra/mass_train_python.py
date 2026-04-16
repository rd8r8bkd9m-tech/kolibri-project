#!/usr/bin/env python3
"""
Kolibri — Массовое обучение Python-движка (Числовое Мышление).

Подаёт текстовые файлы из корпуса в /api/v1/ai/train endpoint.
Обучает: KnowledgeGraph + FormulaPool + EmbeddingTable + SentenceStore.

Использование:
    python3 scripts/mass_train_python.py                    # Все директории
    python3 scripts/mass_train_python.py --dir data/corpus  # Конкретная папка
    python3 scripts/mass_train_python.py --wiki-mass        # Только wiki_mass
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

import urllib.request
import urllib.error

API_BASE = "http://localhost:8001/api/v1/ai"
MAX_TEXT_SIZE = 95_000  # чуть меньше лимита API (100KB)
MIN_TEXT_SIZE = 50      # минимум 50 символов


def send_train(text: str, timeout: int = 120) -> dict:
    """Отправить текст на обучение через API."""
    payload = json.dumps({"text": text, "verify": False}).encode("utf-8")
    req = urllib.request.Request(
        f"{API_BASE}/train",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        body = e.read().decode() if e.fp else ""
        return {"error": f"HTTP {e.code}", "detail": body}
    except Exception as e:
        return {"error": str(e)}


def get_stats(timeout: int = 30) -> dict:
    """Получить статистику движка."""
    try:
        req = urllib.request.Request(f"{API_BASE}/stats")
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode())
    except Exception as e:
        return {"error": str(e)}


def collect_files(dirs: list[str]) -> list[Path]:
    """Собрать все текстовые файлы для обучения."""
    files: list[Path] = []
    for d in dirs:
        p = Path(d)
        if not p.exists():
            continue
        for ext in ("*.txt", "*.md"):
            files.extend(sorted(p.rglob(ext)))
    # Убрать дубликаты, сохранив порядок
    seen: set[str] = set()
    unique: list[Path] = []
    for f in files:
        key = str(f.resolve())
        if key not in seen:
            seen.add(key)
            unique.append(f)
    return unique


def chunk_text(text: str, max_size: int = MAX_TEXT_SIZE) -> list[str]:
    """Разбить длинный текст на куски по max_size символов."""
    if len(text) <= max_size:
        return [text]
    chunks: list[str] = []
    lines = text.split("\n")
    current: list[str] = []
    current_len = 0
    for line in lines:
        if current_len + len(line) + 1 > max_size and current:
            chunks.append("\n".join(current))
            current = []
            current_len = 0
        current.append(line)
        current_len += len(line) + 1
    if current:
        chunks.append("\n".join(current))
    return chunks


def main() -> None:
    parser = argparse.ArgumentParser(description="Массовое обучение Python-движка")
    parser.add_argument("--dir", nargs="+", help="Директории с текстами")
    parser.add_argument("--wiki-mass", action="store_true",
                        help="Обучить только из data/corpus/wiki_mass/")
    parser.add_argument("--delay", type=float, default=0.5,
                        help="Задержка между запросами (сек)")
    parser.add_argument("--timeout", type=int, default=120,
                        help="Таймаут запроса (сек)")
    parser.add_argument("--batch-size", type=int, default=50,
                        help="Перезагрузка эмбеддингов каждые N документов")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent

    if args.dir:
        dirs = args.dir
    elif args.wiki_mass:
        dirs = [str(root / "data" / "corpus" / "wiki_mass")]
    else:
        dirs = [
            str(root / "data" / "corpus"),
            str(root / "docs" / "wikipedia"),
            str(root / "docs" / "ingested"),
        ]

    # Проверяем бэкенд
    stats = get_stats(timeout=10)
    if "error" in stats:
        print(f"❌ Бэкенд недоступен: {stats['error']}")
        print("   Запустите: bash start.sh")
        sys.exit(1)

    initial_patterns = stats.get("graph_patterns", 0)
    initial_edges = stats.get("graph_edges", 0)
    print(f"🧠 Начальное состояние: {initial_patterns:,} паттернов, {initial_edges:,} рёбер")
    print()

    files = collect_files(dirs)
    if not files:
        print("❌ Нет файлов для обучения")
        print(f"   Проверьте директории: {dirs}")
        sys.exit(1)

    print(f"📚 Найдено {len(files)} файлов для обучения")
    print(f"   Из: {', '.join(dirs)}")
    print()

    ok_count = 0
    fail_count = 0
    skip_count = 0
    total_chunks = 0
    t0 = time.time()

    for i, fpath in enumerate(files, 1):
        try:
            text = fpath.read_text(encoding="utf-8", errors="ignore").strip()
        except Exception as e:
            print(f"  ⚠ [{i}/{len(files)}] Не могу прочитать {fpath.name}: {e}")
            fail_count += 1
            continue

        if len(text) < MIN_TEXT_SIZE:
            if args.verbose:
                print(f"  ⏭ [{i}/{len(files)}] {fpath.name}: слишком короткий ({len(text)} симв)")
            skip_count += 1
            continue

        chunks = chunk_text(text)
        file_ok = True

        for ci, chunk in enumerate(chunks):
            result = send_train(chunk, timeout=args.timeout)

            if "error" in result:
                print(f"  ❌ [{i}/{len(files)}] {fpath.name} (часть {ci+1}/{len(chunks)}): {result['error']}")
                if args.verbose and "detail" in result:
                    print(f"     {result['detail'][:200]}")
                file_ok = False
                break

            total_chunks += 1
            time.sleep(args.delay)

        if file_ok:
            ok_count += 1
            elapsed = time.time() - t0
            speed = ok_count / elapsed if elapsed > 0 else 0
            new_patterns = result.get("new_patterns", "?")
            new_edges = result.get("new_edges", "?")

            status = f"  ✅ [{i}/{len(files)}] {fpath.name}"
            if len(chunks) > 1:
                status += f" ({len(chunks)} частей)"
            status += f"  [{speed:.1f} docs/sec]"
            print(status)
        else:
            fail_count += 1

        # Каждые batch-size документов — пауза для усвоения
        if ok_count > 0 and ok_count % args.batch_size == 0:
            print(f"\n  ⏸ Пауза 5с для усвоения (обработано {ok_count} файлов)...\n")
            time.sleep(5)

    elapsed = time.time() - t0

    # Финальная статистика
    print()
    print("═" * 60)
    stats_final = get_stats(timeout=30)
    final_patterns = stats_final.get("graph_patterns", 0)
    final_edges = stats_final.get("graph_edges", 0)
    final_sentences = stats_final.get("sentence_count", 0)
    formula_gen = stats_final.get("formula_generation", 0)
    emb_vocab = stats_final.get("embedding_vocab_size", 0)

    print(f"🏁 Массовое обучение Python-движка завершено!")
    print(f"   Время: {elapsed:.0f}с ({elapsed/60:.1f} мин)")
    print(f"   Файлов: ✅ {ok_count}  ❌ {fail_count}  ⏭ {skip_count}")
    print(f"   Чанков обработано: {total_chunks}")
    print()
    print(f"📊 Рост базы знаний:")
    print(f"   Паттерны: {initial_patterns:,} → {final_patterns:,} (+{final_patterns-initial_patterns:,})")
    print(f"   Рёбра:    {initial_edges:,} → {final_edges:,} (+{final_edges-initial_edges:,})")
    print(f"   Предложения: {final_sentences:,}")
    print(f"   Формулы: поколение {formula_gen}, эмбеддинги: {emb_vocab:,} слов")
    print("═" * 60)


if __name__ == "__main__":
    main()
