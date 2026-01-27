#!/bin/bash
# Kolibri Swarm Stress Test: 1000 Nodes
# Caution: Requires substantial RAM (~80GB if all nodes active, but Linux lazy allocation might help)

COUNT=1000
BASE_PORT=10000
DURATION=30
LOG_DIR="logs/swarm_1000"
GENOME_DIR=".kolibri/swarm_1000"

mkdir -p "$LOG_DIR"
mkdir -p "$GENOME_DIR"

echo "[Swarm] Starting $COUNT nodes..."
echo "[Swarm] Baseline RAM: $(free -h | grep Mem | awk '{print $4}') available"

for i in $(seq 1 $COUNT); do
    PORT=$((BASE_PORT + i))
    # We use a ring topology for peers
    PEER_PORT=$((BASE_PORT + (i % COUNT) + 1))
    
    # Run in background with mass-learn and auto-evolve
    ./build/kolibri_node --listen $PORT \
        --node-id "$i" \
        --peer 127.0.0.1:$PEER_PORT \
        --genome "$GENOME_DIR/node$i.dat" \
        --mass-learn \
        --auto-evolve-ms 100 > "$LOG_DIR/node$i.log" 2>&1 &
    
    # Small delay every 100 nodes to avoid CPU spike during process creation
    if [ $((i % 100)) -eq 0 ]; then
        echo "[Swarm] Launched $i nodes... Current RAM: $(free -h | grep Mem | awk '{print $4}') available"
        sleep 1
    fi
done

echo "[Swarm] All nodes launched. Waiting $DURATION seconds for evolution..."
sleep "$DURATION"

echo "[Swarm] RAM during peak: $(free -h | grep Mem | awk '{print $4}') available"

echo "[Swarm] Collecting final stats from selected nodes..."
for i in 1 100 250 500 750 1000; do
    if [ -f "$LOG_DIR/node$i.log" ]; then
        echo "Node $i Summary:"
        tail -n 20 "$LOG_DIR/node$i.log" | grep -E "фитнес=|ассоциаций=|Активных связей"
    fi
done

echo "[Swarm] Analyzing overall progress (Average Fitness)..."
grep -o "фитнес=[0-9.]*" "$LOG_DIR"/node*.log | cut -d'=' -f2 | awk '{sum+=$1; ++n} END {if (n > 0) print "Average Fitness: " sum/n; print "Data points reported: " n}'

echo "[Swarm] Terminating all nodes..."
pkill kolibri_node

echo "[Swarm] Cleanup..."
# Optional: remove genomes if they are too large
# rm -rf "$GENOME_DIR"
