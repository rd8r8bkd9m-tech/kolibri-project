#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: run_local_model_fork.sh [options]

Options:
  --base-genome PATH   Base genome to fork (or set KOLIBRI_LOCAL_MODEL_PATH)
  --fork-genome PATH   Output genome path (default: build/training/local_fork_genome.dat)
  --bootstrap PATH     Bootstrap script path (default: build/training/local_fork_bootstrap.ks)
  --question TEXT      Question to ask after bootstrap (default: "Kolibri принципы")
  --seed N             Seed for kolibri_node (default: 20250923)
  -h, --help           Show this message
USAGE
}

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"
build_dir="$project_root/build"
node_bin="$build_dir/kolibri_node"

base_genome="${KOLIBRI_LOCAL_MODEL_PATH:-}"
fork_genome="$build_dir/training/local_fork_genome.dat"
bootstrap_script="$build_dir/training/local_fork_bootstrap.ks"
question="Kolibri принципы"
seed=20250923

while [[ $# -gt 0 ]]; do
    case "$1" in
        --base-genome)
            base_genome="$2"
            shift 2
            ;;
        --fork-genome)
            fork_genome="$2"
            shift 2
            ;;
        --bootstrap)
            bootstrap_script="$2"
            shift 2
            ;;
        --question)
            question="$2"
            shift 2
            ;;
        --seed)
            seed="$2"
            shift 2
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

if [[ ! -x "$node_bin" ]]; then
    echo "kolibri_node not found at $node_bin. Build the project first." >&2
    exit 1
fi

select_best_genome() {
    local best_path=""
    local best_size=-1
    local candidates=(
        "$build_dir/training/auto_genome.dat"
        "$build_dir/knowledge/knowledge_genome.dat"
        "$project_root/.kolibri/knowledge_genome.dat"
    )
    for candidate in "${candidates[@]}"; do
        if [[ -f "$candidate" ]]; then
            local size
            size=$(stat -c%s "$candidate" 2>/dev/null || echo 0)
            if (( size > best_size )); then
                best_size=$size
                best_path="$candidate"
            fi
        fi
    done
    echo "$best_path"
}

if [[ -z "$base_genome" ]]; then
    base_genome="$(select_best_genome)"
fi

mkdir -p "$(dirname "$fork_genome")"
mkdir -p "$(dirname "$bootstrap_script")"

if [[ -n "$base_genome" ]]; then
    if [[ ! -f "$base_genome" ]]; then
        echo "Base genome not found: $base_genome" >&2
        exit 1
    fi
    cp -f "$base_genome" "$fork_genome"
    echo "[local-model] Forked genome: $base_genome -> $fork_genome"
else
    rm -f "$fork_genome"
    echo "[local-model] No existing genome found; starting a new fork at $fork_genome"
fi

cat > "$bootstrap_script" <<'KOLIBRI'
начало:
    показать "Запуск форка локальной модели Kolibri"
    обучить связь "Kolibri принципы" -> "Колибри мыслит числами, сжимает знания и эволюционирует локально."
    обучить связь "Kolibri компрессия" -> "Экстремальное сжатие сохраняет смысл и ускоряет поиск."
    обучить связь "Kolibri детерминизм" -> "Ответы воспроизводимы и проверяемы."
    создать формулу ответ из "ассоциация"
    вызвать эволюцию
    показать "Колибри принципы применены"
конец.
KOLIBRI

{
    echo ":ask $question"
    echo ":quit"
} | "$node_bin" \
        --node-id 1 \
        --seed "$seed" \
        --genome "$fork_genome" \
        --bootstrap "$bootstrap_script"
