#!/bin/bash
set -e

echo "🚀 Запуск Kolibri AI..."

# Kill old processes
pkill -f "kolibri_http" 2>/dev/null || true
pkill -f "python3.*3000" 2>/dev/null || true
sleep 1

# Build frontend if needed
if [ ! -f "frontend/dist/index.html" ]; then
    echo "📦 Building frontend..."
    cd frontend && npm run build && cd ..
fi

# Start backend on :8001
echo "🧠 Starting backend on :8001..."
cd /Users/kolibri/Desktop/kolibri-project
./kolibri_http 8001 > /tmp/kolibri_backend.log 2>&1 &
BACKEND_PID=$!
sleep 2

# Verify backend
if curl -s http://127.0.0.1:8001/api/v1/health > /dev/null 2>&1; then
    echo "✅ Backend running (PID: $BACKEND_PID)"
else
    echo "❌ Backend failed to start"
    exit 1
fi

# Start frontend on :3000
echo "🎨 Starting frontend on :3000..."
cd /Users/kolibri/Desktop/kolibri-project/frontend/dist
python3 -m http.server 3000 --bind 127.0.0.1 > /tmp/kolibri_frontend.log 2>&1 &
FRONTEND_PID=$!
sleep 2

# Verify frontend
if curl -s http://127.0.0.1:3000/ | grep -q "<title>"; then
    echo "✅ Frontend running (PID: $FRONTEND_PID)"
else
    echo "❌ Frontend failed to start"
    exit 1
fi

echo ""
echo "============================================================"
echo "  🐦 Kolibri AI — Ready!"
echo "============================================================"
echo "  Frontend: http://127.0.0.1:3000"
echo "  Backend:  http://127.0.0.1:8001"
echo "  Backend PID: $BACKEND_PID"
echo "  Frontend PID: $FRONTEND_PID"
echo "============================================================"
echo ""
echo "Press Ctrl+C to stop all servers"

# Wait for interrupt
wait
