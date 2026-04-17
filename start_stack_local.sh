#!/bin/bash
set -euo pipefail

# Останавливаем старые процессы
pkill -f "uvicorn" || true
pkill -f "vite" || true
pkill -f "kolibri_http" || true

echo "[Stack] Запускаю FastAPI Backend (Swarm & Benchmarks)..."
export PYTHONPATH="$(pwd)"
uvicorn services.main:app --host 127.0.0.1 --port 8000 &
BACKEND_PID=$!

echo "[Stack] Запускаю Vite Frontend..."
cd web
npm run dev -- --host 127.0.0.1 --port 3000 &
FRONTEND_PID=$!

echo "Стек запущен."
echo "Frontend: http://127.0.0.1:3000"
echo "Backend API: http://127.0.0.1:8000"

wait $FRONTEND_PID $BACKEND_PID
