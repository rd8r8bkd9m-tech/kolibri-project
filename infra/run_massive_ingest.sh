#!/bin/bash
# High-Performance Mass Ingestion (Swarm Mode)
# Launches 500 parallel pipelines using native Unix utilities.

NODES=500
INPUT="seeds/internet_map_100k.txt"
LOG_DIR="logs/mass_ingest"

# 1. Prepare
echo "[Swarm] Preparing  parallel workers..."
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/*
rm -rf data/shards
mkdir -p data/shards

# 2. Split Workload (The efficient way: 'split')
# Split lines by line count or number of files
TOTAL_LINES=$(wc -l < "$INPUT")
LINES_PER_NODE=$((TOTAL_LINES / NODES))
if [ "$LINES_PER_NODE" -eq 0 ]; then LINES_PER_NODE=1; fi

mkdir -p data/queue
rm -f data/queue/*
split -l "$LINES_PER_NODE" "$INPUT" data/queue/job_

# 3. Launch Swarm (Controlled Concurrency with xargs)
echo "[Swarm] Launching Worker Pool (Max Concurrency: 50)..."

process_shard() {
    job_file="$1"
    job_id=$(basename "$job_file")
    log_file="logs/mass_ingest/${job_id}.log"
    
    export KOLIBRI_GENOME_PATH="data/shards/genome_${job_id}.dat"
    
    # Pipe: Seed File -> Python Fetcher -> C Ingest Engine
    cat "$job_file" | python3 backend/python/universal_parser.py | ./build/kolibri_ingest > "$log_file" 2>&1
}

export -f process_shard
export KOLIBRI_GENOME_PATH

# Use find + xargs to manage the process pool
# -P 50: Run up to 50 concurrent processes (Simulating 500 nodes via time-division multiplexing)
echo "[Swarm] Starting xargs pool..."
find data/queue -name "job_*" -print0 | xargs -0 -n 1 -P 50 -I {} bash -c 'process_shard "$@"' _ {} &
SWARM_PID=$!

echo "[Swarm] Pool Logic Active (PID: $SWARM_PID). 50 Concurrent Workers."

# 4. Monitor
t=0
while kill -0 "$SWARM_PID" 2>/dev/null; do
    sleep 2
    t=$((t + 2))
    
    # Count generated shards
    shards_count=$(ls -1 data/shards/*.dat 2>/dev/null | wc -l)
    processed=$(grep -r "Success" "$LOG_DIR" 2>/dev/null | wc -l)
    
    echo -ne "\r[Swarm] Time: ${t}s | Active Workers: 50 (capped) | Shards: $shards_count | Processed Sites: $processed "
done

echo ""
echo "[Swarm] Complete. Merging Genomes..."
cat data/shards/*.dat > genome.dat 2>/dev/null
echo "[Swarm] Final Genome Size: $(du -h genome.dat | cut -f1)"
