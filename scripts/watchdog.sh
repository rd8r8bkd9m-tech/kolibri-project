#!/bin/bash
# Kolibri Watchdog - автоперезапуск при падении
while true; do
    # Check backend
    if ! curl -s --max-time 2 http://127.0.0.1:8001/api/v1/health > /dev/null 2>&1; then
        pkill -9 -f kolibri_http 2>/dev/null || true
        sleep 1
        cd /Users/kolibri/Desktop/kolibri-project
        ./kolibri_http 8001 > /tmp/backend.log 2>&1 &
        echo "[watchdog] Backend restarted at $(date)"
    fi
    
    # Check frontend
    if ! curl -s --max-time 2 http://127.0.0.1:3000/ > /dev/null 2>&1; then
        pkill -9 -f "node server.cjs" 2>/dev/null || true
        sleep 1
        cd /Users/kolibri/Desktop/kolibri-project/frontend
        node server.cjs > /tmp/frontend.log 2>&1 &
        echo "[watchdog] Frontend restarted at $(date)"
    fi
    
    sleep 3
done
