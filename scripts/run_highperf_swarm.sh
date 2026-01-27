#!/bin/bash
# High-Performance Kolibri Swarm Test for Powerful Machines
# Target: 1000 nodes, 30 seconds duration
# Resources: Uses available 16 cores and 62GB RAM efficiently

COUNT=1000
DURATION=30
LOG_DIR="logs/swarm_highperf"
GENOME_DIR=".kolibri/swarm_highperf"
BASE_PORT=10000

# Cleanup
echo "[Swarm] Cleaning up previous runs..."
pkill -9 kolibri_node || true
rm -rf "$LOG_DIR"
mkdir -p "$LOG_DIR"
mkdir -p "$GENOME_DIR"

echo "=================================================="
echo "   KOLIBRI HIGH-PERFORMANCE SWARM LAUNCHER"
echo "=================================================="
echo "Nodes:     $COUNT"
echo "Duration:  $DURATION seconds"
echo "Resources: $(nproc) cores, $(free -h | grep Mem | awk '{print $2}') RAM"
echo "--------------------------------------------------"

# Parallel Launch Strategy
# We launch in batches of 50 to avoid creating 1000 processes simultaneously and choking the scheduler
BATCH_SIZE=50
TOTAL_BATCHES=$((COUNT / BATCH_SIZE))

echo "[Swarm] Launching $COUNT nodes using parallel batches..."

for batch in $(seq 1 $TOTAL_BATCHES); do
    START_NODE=$(( (batch - 1) * BATCH_SIZE + 1 ))
    END_NODE=$(( batch * BATCH_SIZE ))
    
    # Launch batch in background
    for i in $(seq $START_NODE $END_NODE); do
        PORT=$((BASE_PORT + i))
        PEER_PORT=$((BASE_PORT + (i % COUNT) + 1))
        
        # Reduced logging for performance, only stats go to file
        # Using exec to replace shell with process saves 1000 bash processes
        (exec ./build/kolibri_node --listen $PORT \
            --node-id "$i" \
            --peer 127.0.0.1:$PEER_PORT \
            --genome "$GENOME_DIR/node$i.dat" \
            --mass-learn \
            --auto-evolve-ms 100 > "$LOG_DIR/node$i.log" 2>&1) &
    done
    
    echo -ne "Batch $batch/$TOTAL_BATCHES launched ($END_NODE/$COUNT nodes)...\r"
    # Tiny sleep to let scheduler breathe
    sleep 0.2
done
echo "" # Newline after progress bar

echo "[Swarm] All nodes active. Stabilizing (5s)..."
sleep 5

echo "[Swarm] Starting Active Learning Phase ($DURATION seconds)..."
echo "[Command] Broadcast: 'Что такое философия?'"

# Send command to Node 1 (Master)
echo ":mass-learn" > swarm_cmd.txt
echo ":ask философия" >> swarm_cmd.txt
echo ":quit" >> swarm_cmd.txt
./build/kolibri_node --listen 10000 --node-id 0 --peer 127.0.0.1:10001 --mass-learn < swarm_cmd.txt > /dev/null 2>&1

# Progress Bar for Duration
for i in $(seq 1 $DURATION); do
    echo -ne "[Running] Time: $i / $DURATION sec | Active Nodes: $(pgrep -c kolibri_node) \r"
    sleep 1
done
echo ""

echo "=================================================="
echo "   SWARM PERFORMANCE REPORT"
echo "=================================================="

# Parallel analysis using grep/awk is much faster than loop
echo "Analyzing logs..."

# 1. Active Nodes (files with content)
ACTIVE_NODES=$(find "$LOG_DIR" -name "*.log" -not -empty | wc -l)

# 2. Total Knowledge Patterns (Sum of all associations)
# Extract only lines with 'ассоциаций=' then sum them up
TOTAL_PATTERNS=$(grep -h "ассоциаций=" "$LOG_DIR"/*.log | awk -F'ассоциаций=' '{sum+=$2} END {print sum}')

# 3. Average Fitness (Average of last fitness values)
# Extract fitness, filter valid numbers, calculate average
AVG_FITNESS=$(grep -h "фитнес=" "$LOG_DIR"/*.log | sed 's/.*фитнес=\([0-9.]*\).*/\1/' | awk '{sum+=$1; count++} END {if (count>0) print sum/count; else print 0}')

# 4. Best Fitness
BEST_FITNESS=$(grep -h "фитнес=" "$LOG_DIR"/*.log | sed 's/.*фитнес=\([0-9.]*\).*/\1/' | sort -nr | head -n 1)
if [ -z "$BEST_FITNESS" ]; then BEST_FITNESS="0.0000"; fi

echo "Nodes Launched:      $COUNT"
echo "Nodes Active/Logged: $ACTIVE_NODES"
echo "Total Associations:  ${TOTAL_PATTERNS:-0}"
echo "Average Fitness:     ${AVG_FITNESS:-0.000000}"
echo "Best Solution:       ${BEST_FITNESS}"
echo "=================================================="

echo "[Swarm] Terminating..."
pkill -9 kolibri_node
echo "[Swarm] Done."
