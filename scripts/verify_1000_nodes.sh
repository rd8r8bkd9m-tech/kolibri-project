#!/bin/bash
export FAST_BENCHMARK=1
export KOLIBRI_GENS=50
export SITES_LIST=seeds/quick_100.txt
export NODE_COUNT=1000
export BASE_DIR="data/massive_swarm"

rm -rf "$BASE_DIR" && mkdir -p "$BASE_DIR/shards" "$BASE_DIR/genomes" "$BASE_DIR/logs" "$BASE_DIR/tasks"

echo "1. Initializing 1000 genomes..."
for i in $(seq 1 1000); do touch "$BASE_DIR/genomes/node_$i.dat"; done

echo "2. Processing 100 sites (Parallel Ingest)..."
split -l 25 "$SITES_LIST" "$BASE_DIR/tasks/batch_"

process_demo_batch() {
    batch="$1"
    id=$(basename "$batch")
    cat "$batch" | python3 backend/python/universal_parser.py | \
    KOLIBRI_GENOME_PATH="$BASE_DIR/shards/shard_$id.dat" ./build/kolibri_ingest > "$BASE_DIR/logs/$id.log" 2>&1
}
export -f process_demo_batch
find "$BASE_DIR/tasks" -name "batch_*" | xargs -n 1 -P 4 bash -c 'process_demo_batch "$@"' _

echo "3. Sharding Knowledge across Swarm..."
# Shard all worker outputs into the 1000 node genomes
for shard in "$BASE_DIR/shards"/shard_*.dat; do
    ./build/kolibri_knowledge_relay --mode shard --source "$shard" --targets-dir "$BASE_DIR/genomes" --allow-events "DEEP_L" > /dev/null 2>&1
done

echo "4. PROOF OF KNOWLEDGE INDEXING (Node #123):"
./build/kolibri_inspect "$BASE_DIR/genomes/node_123.dat"
