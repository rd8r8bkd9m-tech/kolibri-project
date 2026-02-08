#!/bin/bash
# Kolibri Swarm Ingest 100k
# Orchestrates 100 concurrent workers to process 100k sites using Hyper-Regression.

SITES_LIST="seeds/internet_map_100k.txt"
NODES=100
MAX_GENS=25000 # Balanced mode
LOG_DIR="logs/swarm_100k"
SHARD_DIR="data/swarm_shards"

mkdir -p "$LOG_DIR" "$SHARD_DIR"

if [ ! -f "$SITES_LIST" ]; then
    echo "Ошибка: Файл семян $SITES_LIST не найден."
    exit 1
fi

echo "--- Kolibri AI: Massive Ingestion Protocol ---"
echo "Target: 100,000 URLS"
echo "Mode: Swarm (Nodes: $NODES, Parallelism: 40)"
echo "Compression: Neural Hyper-Regression ($MAX_GENS gens/site)"

# Split tasks (Smaller chunks for visual progress: 50 sites per worker)
split -l 50 "$SITES_LIST" "$LOG_DIR/task_"

process_worker() {
    task_file="$1"
    worker_id=$(basename "$task_file")
    export KOLIBRI_GENOME_PATH="$SHARD_DIR/genome_${worker_id}.dat"
    export KOLIBRI_GENS=$MAX_GENS
    
    echo "[Worker $worker_id] Starting batch..."
    cat "$task_file" | python3 backend/python/universal_parser.py | ./build/kolibri_ingest >> "$LOG_DIR/${worker_id}.log" 2>&1
    echo "[Worker $worker_id] Finished."
}

export -f process_worker
export SHARD_DIR LOG_DIR MAX_GENS

# Run Swarm
# -P 40 limits to 40 cores (safe for many environments)
find "$LOG_DIR" -name "task_*" -print0 | xargs -0 -n 1 -P 40 -I {} bash -c 'process_worker "{}"'

echo "--- Swarm Ingest Complete ---"
echo "Merging shards into kolibri_knowledge_base.dat..."
cat "$SHARD_DIR"/genome_*.dat > kolibri_knowledge_base.dat

echo "Compressing into Super Archive..."
./build/kolibri_archiver --create --input kolibri_knowledge_base.dat --output kolibri_compressed_100k.klb

echo "Process Finished! Genome size: $(du -h kolibri_knowledge_base.dat | cut -f1)"
echo "Compressed size: $(du -h kolibri_compressed_100k.klb | cut -f1)"
