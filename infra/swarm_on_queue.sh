#!/bin/bash
# Kolibri Massive Swarm Orchestrator - Queue Mode
# Uses C-Parser for maximum throughput.

QUEUE_DIR="data/queue"
NODE_COUNT=1000
PARALLEL_WORKERS=8
BASE_DIR="data/swarm_proof"
GENS_PER_SITE=100 # Fast mode for the proof

echo "--- Kolibri AI: Swarm Ingest from Queue ($QUEUE_DIR) ---"
rm -rf "$BASE_DIR"
mkdir -p "$BASE_DIR/shards"
mkdir -p "$BASE_DIR/nodes"
mkdir -p "$BASE_DIR/logs"

# 1. Initialize empty node genomes
echo "[1/3] Initializing $NODE_COUNT node identifiers..."
for i in $(seq 1 $NODE_COUNT); do
    touch "$BASE_DIR/nodes/node_$i.dat"
done

# 2. Parallel Ingestion using C-Parser
# We map each job file in queue to a worker.
echo "[2/3] Processing queue via C-Parser + Ingest Engine (Workers: $PARALLEL_WORKERS)..."

process_job() {
    job_file="$1"
    job_id=$(basename "$job_file")
    shard_file="$BASE_DIR/shards/shard_$job_id.dat"
    
    # Run the pipeline: C-Parser -> Ingest
    cat "$job_file" | ./build/kolibri_fast_parser | \
    KOLIBRI_GENOME_PATH="$shard_file" \
    KOLIBRI_GENS=$GENS_PER_SITE \
    ./build/kolibri_ingest > "$BASE_DIR/logs/job_$job_id.log" 2>&1
}

export -f process_job
export BASE_DIR GENS_PER_SITE

# Use xargs to process all jobs in the queue with a progress counter
total_jobs=$(find "$QUEUE_DIR" -type f -name "job_*" | wc -l)
current_job=0

echo "Processing $total_jobs jobs..."
find "$QUEUE_DIR" -type f -name "job_*" | while read job; do
    process_job "$job" &
    current_job=$((current_job + 1))
    if (( current_job % 8 == 0 )); then
        wait
        echo -ne "   Progress: $current_job / $total_jobs jobs processed\r"
    fi
done
wait
echo -e "\nAll jobs processed."

# 3. Sharding Proof
echo "[3/3] Distributing Knowledge across Nodes (Shard Mode)..."

# Collect all sharded events and distribute them
# Since we have many small shard files, we'll concatenate them or process one by one
# For the proof, we'll just take the first few completed shards and relay them.

# Find all non-empty shards
SHARDS=$(find "$BASE_DIR/shards" -name "*.dat" -size +1k)
SHARDS_COUNT=$(echo "$SHARDS" | wc -l)

echo "Ingested $SHARDS_COUNT shards. Sharding to 1000 nodes..."

# Relay each shard to the 1000 nodes in shard mode
for s in $SHARDS; do
    ./build/kolibri_knowledge_relay --mode shard \
        --source "$s" \
        --targets-dir "$BASE_DIR/nodes" \
        --allow-events DEEP_L \
        --target-key-inline kolibri-secret-key > /dev/null 2>&1
done

echo "--- Swarm Proof Ready ---"
echo "Check random nodes for indexed data:"
./build/kolibri_inspect "$BASE_DIR/nodes/node_123.dat" | head -n 10
./build/kolibri_inspect "$BASE_DIR/nodes/node_777.dat" | head -n 10

echo "Total nodes with data:"
find "$BASE_DIR/nodes" -name "*.dat" -size +0c | wc -l
