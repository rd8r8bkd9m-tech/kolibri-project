#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"

output_dir="$project_root/build/knowledge"

if [[ "${1:-}" == "--output" ]]; then
    output_dir="$2"
    shift 2
fi

mkdir -p "$output_dir"

queue_db="$output_dir/queue.db"
approved_dir="$output_dir/approved"

if [[ -x "$project_root/build/kolibri_queue" ]]; then
    "$project_root/build/kolibri_queue" export --db "$queue_db" --status approved --output "$approved_dir" >/dev/null 2>&1 || true
fi

roots=("$@")
if [[ -d "$approved_dir" ]]; then
    if ls "$approved_dir"/*.md >/dev/null 2>&1; then
        roots+=("$approved_dir")
    fi
fi

"$project_root/build/kolibri_indexer" build --output "$output_dir" "${roots[@]}"

spectral_json="$output_dir/spectral_profiles.json"
python3 "$project_root/scripts/spectral_fingerprint.py" --output "$spectral_json" "${roots[@]}"
