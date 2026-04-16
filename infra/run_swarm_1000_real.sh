#!/bin/bash
# Kolibri Swarm 1000 - High Performance Launch Script
# Optimized for 64GB+ RAM and 16+ Cores

# Configuration
NODES=1000
DURATION=30
LOG_DIR="logs/swarm_1000_real"
GENOME_DIR=".kolibri/swarm_1000_real"
BASE_PORT=10000

# Cleanup
echo "[Swarm] Cleaning up previous run..."
pkill -9 kolibri_node || true
rm -rf "$LOG_DIR"
mkdir -p "$LOG_DIR"
mkdir -p "$GENOME_DIR"

# Настройка системных ресурсов (Swap)
if [ -f "./scripts/enable_swap.sh" ]; then
    echo "[Swarm] Checking system resources (Swap)..."
    ./scripts/enable_swap.sh
fi

# Resource check
echo "[Swarm] Detected system resources:"
nproc | xargs echo "Cores: "
free -h | grep Mem | awk '{print "RAM: " $2}'

echo "[Swarm] Launching $NODES independent nodes..."

# Launch loop - optimized for speed
# Launching in batches of 50 to avoid process storm
BATCH_SIZE=20
for ((i=1; i<=NODES; i++)); do
    PORT=$((BASE_PORT + i))
    PEER_PORT=$((BASE_PORT + (i % NODES) + 1))
    
    # Run node in background
    # --mass-learn: Fast initial learning
    # --auto-evolve-ms 100: Fast evolution ticks
    # stdbuf -oL: Line buffered output for real-time logging
    # tail -f /dev/null | ... keeps stdin open so node doesn't exit immediately
    tail -f /dev/null | stdbuf -oL ./build/kolibri_node --listen $PORT \
        --node-id "$i" \
        --peer 127.0.0.1:$PEER_PORT \
        --genome "$GENOME_DIR/node$i.dat" \
        --mass-learn \
        --auto-evolve-ms 200 > "$LOG_DIR/node$i.log" 2>&1 &
        
    if ((i % BATCH_SIZE == 0)); then
        echo "[Swarm] Launched $i / $NODES nodes..."
        # Brief pause to let system stabilize
        sleep 1
    fi
done

echo "[Swarm] All $NODES nodes active!"
echo "[Swarm] Starting 30-second evolution phase..."

# Countdown and monitoring
for ((t=1; t<=DURATION; t++)); do
    sleep 1
    ACTIVE=$(pgrep -c kolibri_node)
    echo -ne "\r[Progress] Time: ${t}s / ${DURATION}s | Active Nodes: $ACTIVE "
    
    if [ "$ACTIVE" -lt "$((NODES / 2))" ]; then
        echo ""
        echo "[Critical] More than 50% of nodes died!"
        break
    fi
done

echo ""
echo "[Swarm] Stopping swarm..."
pkill -9 kolibri_node || true

# Analysis
echo "[Analysis] Generating report..."

# Calculate statistics
ACTIVE_NODES=$(ls "$LOG_DIR"/*.log 2>/dev/null | wc -l)
TOTAL_PATTERNS=0
TRAINED_NODES=0
TOTAL_FITNESS=0
BEST_FITNESS=0

# Process logs efficiently
# extraction regex for fitness: "фитнес=([0-9.]+)"
# extraction regex for associations: "ассоциаций=([0-9]+)"

# Use grep to bulk extract last status line from all logs
grep "фитнес=" "$LOG_DIR"/*.log > "$LOG_DIR/all_stats.txt"

while read -r line; do
    # Extract values
    FITNESS=$(echo "$line" | sed -n 's/.*фитнес=\([0-9.]*\).*/\1/p')
    PATTERNS=$(echo "$line" | sed -n 's/.*ассоциаций=\([0-9]*\).*/\1/p')
    
    if [ ! -z "$FITNESS" ]; then
        TOTAL_FITNESS=$(echo "$TOTAL_FITNESS + $FITNESS" | bc -l)
        TRAINED_NODES=$((TRAINED_NODES + 1))
        
        IS_BETTER=$(echo "$FITNESS > $BEST_FITNESS" | bc -l)
        if [ "$IS_BETTER" -eq 1 ]; then
            BEST_FITNESS=$FITNESS
        fi
    fi
    
    if [ ! -z "$PATTERNS" ]; then
        TOTAL_PATTERNS=$((TOTAL_PATTERNS + PATTERNS))
    fi
done < "$LOG_DIR/all_stats.txt"

AVG_FITNESS=0
if [ "$TRAINED_NODES" -gt 0 ]; then
    AVG_FITNESS=$(echo "scale=4; $TOTAL_FITNESS / $TRAINED_NODES" | bc -l)
fi

echo "=================================================="
echo "   KOLIBRI SWARM REPORT (1000 Nodes)"
echo "=================================================="
echo "Nodes Attempted:     $NODES"
echo "Nodes Active/Logs:   $ACTIVE_NODES"
echo "Nodes Trained:       $TRAINED_NODES"
echo "--------------------------------------------------"
echo "Total Knowledge:     $TOTAL_PATTERNS patterns"
echo "Average Fitness:     $AVG_FITNESS"
echo "Best Fitness:        $BEST_FITNESS"
echo "=================================================="
