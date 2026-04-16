#!/bin/bash
# Kolibri AI - Stable launcher with auto-restart
set -e

ROOT="/Users/kolibri/Desktop/kolibri-project"
FE="$ROOT/frontend"

# Kill old
pkill -f "kolibri_http" 2>/dev/null || true
pkill -f "node server.cjs" 2>/dev/null || true
sleep 2

echo "🚀 Запуск Kolibri AI..."

# Start backend
cd "$ROOT"
./kolibri_http 8001 > /tmp/kolibri_backend.log 2>&1 &
BACKEND_PID=$!
sleep 3

# Verify backend
if ! curl -s --max-time 3 http://127.0.0.1:8001/api/v1/health > /dev/null 2>&1; then
    echo "❌ Backend не запустился"
    exit 1
fi
echo "✅ Backend запущен (PID: $BACKEND_PID)"

# Start frontend server with API proxy
cd "$FE"
node server.cjs > /tmp/kolibri_frontend.log 2>&1 &
FRONTEND_PID=$!
sleep 2

# Verify frontend
if ! curl -s --max-time 3 http://127.0.0.1:3000/ > /dev/null 2>&1; then
    echo "❌ Frontend не запустился"
    exit 1
fi
echo "✅ Frontend запущен (PID: $FRONTEND_PID)"

# Verify API proxy
if curl -s --max-time 3 http://127.0.0.1:3000/api/v1/health > /dev/null 2>&1; then
    echo "✅ API Proxy работает"
else
    echo "❌ API Proxy не работает"
    exit 1
fi

echo ""
echo "============================================================"
echo "  🐦 Kolibri AI — Ready!"
echo "============================================================"
echo "  Frontend: http://127.0.0.1:3000"
echo "  Backend:  http://127.0.0.1:8001"
echo "  Backend PID:  $BACKEND_PID"
echo "  Frontend PID: $FRONTEND_PID"
echo "============================================================"
echo ""
echo "Нажмите Ctrl+C для остановки"
echo ""

# Auto-restart loop
while true; do
    sleep 5
    
    # Check backend
    if ! kill -0 $BACKEND_PID 2>/dev/null; then
        echo "⚠️  Backend упал, перезапуск..."
        cd "$ROOT" && ./kolibri_http 8001 >> /tmp/kolibri_backend.log 2>&1 &
        BACKEND_PID=$!
        sleep 2
        echo "✅ Backend перезапущен (PID: $BACKEND_PID)"
    fi
    
    # Check frontend
    if ! kill -0 $FRONTEND_PID 2>/dev/null; then
        echo "⚠️  Frontend упал, перезапуск..."
        cd "$FE" && node server.cjs >> /tmp/kolibri_frontend.log 2>&1 &
        FRONTEND_PID=$!
        sleep 2
        echo "✅ Frontend перезапущен (PID: $FRONTEND_PID)"
    fi
done
