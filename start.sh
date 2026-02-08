#!/bin/bash
# Запуск Kolibri: backend (8001) + frontend (3000)
cd /workspaces/kolibri-project

pkill -9 -f uvicorn 2>/dev/null
pkill -9 -f "node.*vite" 2>/dev/null
sleep 1

# Backend
KOLIBRI_RESPONSE_MODE=script nohup python3 -m uvicorn backend.service.main:app --host 0.0.0.0 --port 8001 > /tmp/backend.log 2>&1 &
echo "Backend PID=$!"

# Frontend
cd frontend
KOLIBRI_SKIP_WASM_AUTOBUILD=1 nohup npx vite --host 0.0.0.0 --port 3000 > /tmp/frontend.log 2>&1 &
echo "Frontend PID=$!"
cd ..

# Ждём запуска
echo "Жду запуска..."
sleep 5

# Проверка
BACK=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8001/api/health 2>/dev/null)
FRONT=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:3000/ 2>/dev/null)

echo ""
echo "=== РЕЗУЛЬТАТ ==="
if [ "$BACK" = "200" ]; then echo "✅ Backend:  http://127.0.0.1:8001 — OK"; else echo "❌ Backend:  FAIL ($BACK)"; fi
if [ "$FRONT" = "200" ]; then echo "✅ Frontend: http://127.0.0.1:3000 — OK"; else echo "❌ Frontend: FAIL ($FRONT)"; fi
echo "================="
