#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: fork_local_models.sh [options]

Options:
  --catalog PATH     Catalog JSON (default: models/local_model_catalog.json)
  --sources-dir DIR  Local sources root (default: .kolibri/local_sources)
  --output-dir DIR   Output directory (default: .kolibri/forked)
  --dry-run          Print planned actions without copying
  -h, --help         Show this message

Notes:
  - Скрипт работает только с локальными файлами и ничего не скачивает.
  - Для отсутствующих источников выводит предупреждение и продолжает.
USAGE
}

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"

catalog="$project_root/models/local_model_catalog.json"
sources_dir="$project_root/.kolibri/local_sources"
output_dir="$project_root/.kolibri/forked"
dry_run=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --catalog)
            catalog="$2"
            shift 2
            ;;
        --sources-dir)
            sources_dir="$2"
            shift 2
            ;;
        --output-dir)
            output_dir="$2"
            shift 2
            ;;
        --dry-run)
            dry_run=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ ! -f "$catalog" ]]; then
    echo "Catalog not found: $catalog" >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 is required to parse the catalog." >&2
    exit 1
fi

mkdir -p "$output_dir"

python3 - "$catalog" <<'PY' | while IFS=$'\t' read -r model_id source_path question description; do
import json
import sys

catalog_path = sys.argv[1]

with open(catalog_path, "r", encoding="utf-8") as handle:
    entries = json.load(handle)

for entry in entries:
    model_id = entry.get("id", "")
    source = entry.get("source", "")
    question = entry.get("question", "")
    description = entry.get("description", "")
    if not model_id or not source:
        continue
    print(f"{model_id}\t{source}\t{question}\t{description}")
PY
    resolved_source="$source_path"
    if [[ "$source_path" != /* ]]; then
        if [[ -f "$project_root/$source_path" ]]; then
            resolved_source="$project_root/$source_path"
        elif [[ -f "$sources_dir/$source_path" ]]; then
            resolved_source="$sources_dir/$source_path"
        fi
    fi

    target_dir="$output_dir/$model_id"
    target_genome="$target_dir/genome.dat"
    target_meta="$target_dir/manifest.txt"

    if [[ ! -f "$resolved_source" ]]; then
        echo "[fork] missing source: $source_path (expected: $resolved_source)" >&2
        continue
    fi

    if [[ "$dry_run" -eq 1 ]]; then
        echo "[fork] would copy $resolved_source -> $target_genome"
        continue
    fi

    mkdir -p "$target_dir"
    cp -f "$resolved_source" "$target_genome"
    {
        echo "id: $model_id"
        echo "source: $source_path"
        echo "question: $question"
        echo "description: $description"
        echo "forked_at: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } > "$target_meta"

    echo "[fork] copied $resolved_source -> $target_genome"
done
