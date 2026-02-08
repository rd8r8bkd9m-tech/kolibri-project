#!/bin/bash
export PATH=/usr/local/share/nvm/versions/node/v22.21.1/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH
echo "[Real OS Bridge] Starting Kolibri System..."

# Enable error handling
set -e

# Kill any existing instances if needed (optional)
# pkill -f "uvicorn" || true

# Start Backend Bridge in background
echo "Starting Backend Bridge (Port 3000)..."
/workspaces/kolibri-project/.venv/bin/python backend/service/os_bridge.py &
BACKEND_PID=$!

# Wait for backend to be ready
sleep 2

# Start Frontend
echo "Starting Visual OS (Port 5173)..."
cd frontend && npm run dev -- --host &
FRONTEND_PID=$!

# Handle shutdown
trap "kill $BACKEND_PID $FRONTEND_PID; exit" SIGINT SIGTERM

# Wait indefinitely
wait
