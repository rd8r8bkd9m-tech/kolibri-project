#!/bin/bash
# Kolibri Utility: Learn from External URL
# Usage: ./scripts/learn_url.sh <URL>

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <URL>"
    echo "Example: $0 https://en.wikipedia.org/wiki/Artificial_intelligence"
    exit 1
fi

URL="$1"
OUTPUT_DIR="docs/ingested"
LOG_DIR="logs"

# Ensure directories exist
mkdir -p "$OUTPUT_DIR"
mkdir -p "$LOG_DIR"

# 1. Fetch the content
echo "[Kolibri] Fetching content from $URL..."
if python3 scripts/kolibri_fetch_docs.py "$URL" --output "$OUTPUT_DIR"; then
    echo "[Kolibri] Content successfully saved to $OUTPUT_DIR"
else
    echo "[Kolibri] Failed to fetch content via python script."
    exit 1
fi

# 2. Restart Knowledge Server to re-index
echo "[Kolibri] Restarting Knowledge Server to index new content..."

if pgrep -f "kolibri_knowledge_server" > /dev/null; then
    echo "[Kolibri] Stopping running server..."
    pkill -f "kolibri_knowledge_server"
    sleep 3
    # Force kill if still running
    if pgrep -f "kolibri_knowledge_server" > /dev/null; then
        echo "[Kolibri] Force killing server..."
        pkill -9 -f "kolibri_knowledge_server"
        sleep 1
    fi
fi

# Check if binary exists
if [ ! -f "./build/kolibri_knowledge_server" ]; then
    echo "[Kolibri] Error: ./build/kolibri_knowledge_server not found. Please build the project first."
    exit 1
fi

# Start in background
nohup ./build/kolibri_knowledge_server > "$LOG_DIR/knowledge_server.log" 2>&1 &
PID=$!

sleep 1
if ps -p $PID > /dev/null; then
    echo "[Kolibri] Knowledge Server restarted (PID: $PID). New knowledge is now available."
    echo "[Kolibri] You can monitor logs at $LOG_DIR/knowledge_server.log"
else
    echo "[Kolibri] Error: Knowledge Server failed to start. Check $LOG_DIR/knowledge_server.log"
    exit 1
fi
