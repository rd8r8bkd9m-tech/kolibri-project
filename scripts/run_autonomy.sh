#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: run_autonomy.sh [options]

This script launchs a continuous loop:
  1) Run the Kolibri crawler to fetch external docs.
  2) Execute knowledge pipeline.
  3) Run auto_train.sh (which updates Kolibri gene pool and runs soak analysis).

Options:
  --seeds-file FILE        File with seed URLs (passed to crawler)
  --allow-domain DOMAIN    Allowed domain for crawler (repeatable)
  --crawler-pages N        Pages per crawler run (default: 25)
  --crawler-depth N        Depth for crawler (default: 2)
  --crawler-timeout SEC    HTTP timeout per request (default: 15)
  --interval SEC           Sleep between cycles (default: 600)
  --cycles N               Number of cycles to run (default: unlimited)
  --ticks N                Kolibri evolution ticks per auto_train (default: 256)
  --skip-build             Pass --skip-build to auto_train (use if build done once)
  -h, --help               Show this help message
USAGE
}

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"
build_dir="$project_root/build"
docs_ingested="$project_root/docs/ingested"
crawler_db="$build_dir/crawler/crawler.db"

seeds_file=""
declare -a allow_domains=()
crawler_pages=25
crawler_depth=2
crawler_timeout=15
interval_seconds=600
cycles=-1
auto_ticks=256
skip_build_flag=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --seeds-file)
            seeds_file="$2"
            shift 2
            ;;
        --allow-domain)
            allow_domains+=("$2")
            shift 2
            ;;
        --crawler-pages)
            crawler_pages="$2"
            shift 2
            ;;
        --crawler-depth)
            crawler_depth="$2"
            shift 2
            ;;
        --crawler-timeout)
            crawler_timeout="$2"
            shift 2
            ;;
        --interval)
            interval_seconds="$2"
            shift 2
            ;;
        --cycles)
            cycles="$2"
            shift 2
            ;;
        --ticks)
            auto_ticks="$2"
            shift 2
            ;;
        --skip-build)
            skip_build_flag=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

mkdir -p "$docs_ingested" "$build_dir/crawler"

cycle_counter=0
while [[ "$cycles" -lt 0 || "$cycle_counter" -lt "$cycles" ]]; do
    cycle_counter=$((cycle_counter + 1))
    echo "[autonomy] === Cycle $cycle_counter ==="

    crawler_cmd=(python3 "$project_root/scripts/kolibri_crawler.py"
        --output "$docs_ingested"
        --db "$crawler_db"
        --max-pages "$crawler_pages"
        --max-depth "$crawler_depth"
        --timeout "$crawler_timeout")

    if [[ -n "$seeds_file" ]]; then
        crawler_cmd+=(--seed-file "$seeds_file")
    fi
    for domain in "${allow_domains[@]}"; do
        crawler_cmd+=(--allow-domain "$domain")
    done

    echo "[autonomy] Running crawler: ${crawler_cmd[*]}"
    "${crawler_cmd[@]}"

    echo "[autonomy] Running knowledge pipeline"
    "$project_root/scripts/knowledge_pipeline.sh" docs data

    auto_cmd=("$project_root/scripts/auto_train.sh" --ticks "$auto_ticks")
    if [[ "$skip_build_flag" -eq 1 ]]; then
        auto_cmd+=(--skip-build)
    fi
    echo "[autonomy] Running auto_train: ${auto_cmd[*]}"
    "${auto_cmd[@]}"

    echo "[autonomy] Cleaning ingested docs (keeping only genome)"
    rm -rf "${docs_ingested:?}/"*

    if [[ "$cycles" -ge 0 && "$cycle_counter" -ge "$cycles" ]]; then
        echo "[autonomy] Completed $cycle_counter cycles, exiting."
        break
    fi
    echo "[autonomy] Sleeping ${interval_seconds}s before next cycle..."
    sleep "$interval_seconds"
done
