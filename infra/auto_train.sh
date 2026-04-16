#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: auto_train.sh [options] [ROOT...]

Options:
  --ticks N        Number of evolution ticks to run (default: 128)
  --seed N         Base seed for Kolibri node (default: 20250923)
  --genome PATH    Path to genome file (default: build/training/auto_genome.dat)
  --bootstrap PATH Path to generate KolibriScript bootstrap (default: build/training/bootstrap.ks)
  --skip-build     Do not rebuild the project before training
  -h, --help       Show this message

Arguments:
  ROOT             Additional directories to feed into knowledge pipeline
                   (defaults: docs data)
USAGE
}

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"
build_dir="$project_root/build"
training_dir="$build_dir/training"
mkdir -p "$training_dir"

ticks=128
seed=20250923
genome_path="$training_dir/auto_genome.dat"
bootstrap_script="$training_dir/bootstrap.ks"
skip_build=0

roots=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --ticks)
            ticks="$2"
            shift 2
            ;;
        --seed)
            seed="$2"
            shift 2
            ;;
        --genome)
            genome_path="$2"
            shift 2
            ;;
        --bootstrap)
            bootstrap_script="$2"
            shift 2
            ;;
        --skip-build)
            skip_build=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            roots+=("$1")
            shift
            ;;
    esac
done

if [[ ${#roots[@]} -eq 0 ]]; then
    roots=("docs" "data")
fi

if [[ "$skip_build" -ne 1 ]]; then
    cmake -S "$project_root" -B "$build_dir" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build "$build_dir"
fi

echo "[auto-train] Building knowledge index from roots: ${roots[*]}"
"$project_root/scripts/knowledge_pipeline.sh" "${roots[@]}"

index_json="$build_dir/knowledge/index.json"
if [[ ! -f "$index_json" ]]; then
    echo "[auto-train] knowledge index not found: $index_json" >&2
    exit 1
fi

echo "[auto-train] Generating KolibriScript bootstrap: $bootstrap_script"
python3 - "$index_json" "$bootstrap_script" <<'PY'
import json
import sys

index_path, output_path = sys.argv[1], sys.argv[2]
docs = []

with open(index_path, "r", encoding="utf-8", errors="replace") as f:
    payload = json.load(f)
    docs = payload.get("documents", [])

# По умолчанию обрабатываем всё, что нашли
docs = docs[:1000]

def escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\n")

lines = ["начало:", '    показать "Kolibri выполняет автоматическое обучение"']
for idx, doc in enumerate(docs, start=1):
    question = escape(doc.get("title") or doc.get("id") or f"doc_{idx}")
    preview = doc.get("content") or ""
    preview = preview.strip()
    if len(preview) > 360:
        preview = preview[:357] + "..."
    answer = escape(preview)
    source = escape(doc.get("source") or doc.get("id") or f"doc_{idx}")
    lines.append(f'    переменная источник_{idx} = "{source}"')
    lines.append(f'    обучить связь "{question}" -> "{answer}"')

lines.append('    создать формулу ответ из "ассоциация"')
lines.append("    вызвать эволюцию")
lines.append('    показать "Автоматическое обучение завершено"')
lines.append("конец.")

with open(output_path, "w", encoding="utf-8") as out:
    out.write("\n".join(lines))
PY

custom_model="$project_root/training/custom_model.ks"
if [[ -f "$custom_model" ]]; then
    echo "[auto-train] Appending custom model: $custom_model"
    if [[ ! -r "$bootstrap_script" ]]; then
        echo "[auto-train] Bootstrap script is not readable: $bootstrap_script" >&2
        exit 1
    fi
    if [[ ! -r "$custom_model" ]]; then
        echo "[auto-train] Custom model is not readable: $custom_model" >&2
        exit 1
    fi
    tmp_bootstrap="$(mktemp -p "$(dirname "$bootstrap_script")" "bootstrap.tmp.XXXXXX")"
    if [[ -z "$tmp_bootstrap" || ! -f "$tmp_bootstrap" ]]; then
        echo "[auto-train] Failed to create temporary bootstrap file" >&2
        exit 1
    fi
    if ! {
        cat "$bootstrap_script"
        echo ""
        cat "$custom_model"
    } > "$tmp_bootstrap"; then
        echo "[auto-train] Failed to build custom bootstrap" >&2
        rm -f "$tmp_bootstrap"
        exit 1
    fi
    backup_bootstrap="${bootstrap_script}.bak.$(date +%Y%m%d%H%M%S)"
    if ! cp -f "$bootstrap_script" "$backup_bootstrap"; then
        echo "[auto-train] Failed to backup bootstrap script" >&2
        rm -f "$tmp_bootstrap"
        exit 1
    fi
    if ! mv "$tmp_bootstrap" "$bootstrap_script"; then
        echo "[auto-train] Failed to update bootstrap script" >&2
        rm -f "$tmp_bootstrap"
        exit 1
    fi
fi

echo "[auto-train] Running Kolibri node bootstrap and evolution"
{
    echo ":tick $ticks"
    echo ":quit"
} | "$build_dir/kolibri_node" \
        --node-id 1 \
        --seed "$seed" \
        --bootstrap "$bootstrap_script" \
        --genome "$genome_path"

echo "[auto-train] Genome health status:"
"$build_dir/kolibri_node" --health --genome "$genome_path"

echo "[auto-train] Done"
