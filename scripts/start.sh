#!/bin/bash
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
FE="$ROOT/frontend"

# Fix PATH — add Homebrew and Node.js paths
export PATH="/opt/homebrew/bin:/opt/homebrew/opt/sqlite/bin:/Users/kolibri/.nvm/versions/node/v22.21.1/bin:$PATH"

# Kill old - use -9 for force kill and wait for port release
pkill -9 -f kolibri_http 2>/dev/null || true
pkill -9 -f kolibri_swarm 2>/dev/null || true
pkill -9 -f kolibri_mac_proxy 2>/dev/null || true
pkill -9 -f vite 2>/dev/null || true
pkill -9 -f "node server.cjs" 2>/dev/null || true
sleep 2

# Wait for port 8001 to be free (max 10 seconds)
echo "Waiting for port 8001 to be free..."
for i in {1..10}; do
    if ! lsof -i :8001 > /dev/null 2>&1; then
        echo "Port 8001 is free"
        break
    fi
    echo "  Waiting... ($i)"
    sleep 1
done

# Also ensure port 3000 is free
pkill -9 -f "python3.*3000" 2>/dev/null || true
for i in {1..5}; do
    if ! lsof -i :3000 > /dev/null 2>&1; then
        break
    fi
    sleep 1
done

# Compile C HTTP if missing or source is newer
HTTP_NEED_BUILD=0
if [ ! -f "$ROOT/kolibri_http" ]; then
    HTTP_NEED_BUILD=1
elif [ "$ROOT/backend/src/kolibri_http_server.c" -nt "$ROOT/kolibri_http" ]; then
    HTTP_NEED_BUILD=1
fi

if [ $HTTP_NEED_BUILD -eq 1 ]; then
    echo "Compiling C HTTP..."
    # Build kolibri_core if library is missing
    if [ ! -f "$ROOT/build/libkolibri_core.a" ]; then
        echo "  Building kolibri_core library..."
        cd "$ROOT/build" 2>/dev/null || { mkdir -p "$ROOT/build"; cd "$ROOT/build"; cmake .. -DCMAKE_BUILD_TYPE=Debug 2>&1 || true; }
        cmake --build "$ROOT/build" --target kolibri_core -j4 2>&1 || echo "  Warning: cmake build failed, trying direct compile..."
        cd "$ROOT"
    fi
    # Find OpenSSL
    OPENSSL_INC=""
    OPENSSL_LIB=""
    for p in /opt/homebrew/opt/openssl@3 /opt/homebrew/opt/openssl /usr/local/opt/openssl; do
        if [ -d "$p/include" ]; then
            OPENSSL_INC="-I$p/include"
            OPENSSL_LIB="-L$p/lib -lssl -lcrypto"
            break
        fi
    done
    echo "  Linking kolibri_http..."
    cc -std=c23 -O2 -I "$ROOT/backend/include" -I "$ROOT/backend/include/kolibri" $OPENSSL_INC \
        -o "$ROOT/kolibri_http" \
        "$ROOT/backend/src/kolibri_http_server.c" "$ROOT/build/libkolibri_core.a" \
        $OPENSSL_LIB -lm -lpthread -lsqlite3 2>&1
    echo "  ✅ kolibri_http compiled successfully"
fi

# Start backend
cd "$ROOT" && ./kolibri_http 8001 &
BPID=$!
sleep 2
echo "✅ Kolibri HTTP Server: C-Core"
echo "   Modules: reasoning, world_model, corpus, formula, fractal, autolearn"

# Start kolibri_swarm_mac
if [ -f "$ROOT/kolibri_swarm_mac" ]; then
    echo "🐝 Starting kolibri_swarm_mac :8002..."
    mkdir -p "$ROOT/knowledge/swarm"
    # Merge shards if needed
    if [ -f "$ROOT/knowledge/swarm/node1_knowledge.md" ] && [ -f "$ROOT/knowledge/swarm/node2_knowledge.md" ]; then
        cat "$ROOT/knowledge/swarm/node1_knowledge.md" "$ROOT/knowledge/swarm/node2_knowledge.md" > "$ROOT/knowledge/knowledge_base.md"
    fi
    cd "$ROOT" && ./kolibri_swarm_mac 8002 --peer 217.60.249.157:8001 < /dev/null &>/tmp/kolibri_swarm_mac.log &
    SWARM_PID=$!
    sleep 2
    echo "✅ Swarm node: 20K+ facts, peer to Node 1"
fi

# Start hybrid proxy
if [ -f "$ROOT/kolibri_mac_proxy.js" ]; then
    echo "🔗 Starting hybrid proxy :8003..."
    cd "$ROOT" && nohup node kolibri_mac_proxy.js > /tmp/kolibri_mac_proxy.log 2>&1 &
    PROXY_PID=$!
    sleep 2
    # Verify proxy is running
    if kill -0 $PROXY_PID 2>/dev/null; then
        echo "✅ Hybrid proxy: swarm → Node1 → kolibriai.ru"
    else
        echo "⚠️  Hybrid proxy failed to start. Check /tmp/kolibri_mac_proxy.log"
    fi
fi

# Start frontend
if [ -f "$FE/dist/index.html" ]; then
    echo "🎨 Starting frontend server :3000 (with API proxy)..."
    cd "$FE" && node server.cjs > /tmp/kolibri_frontend.log 2>&1 &
    VPID=$!
    sleep 2
    if kill -0 $VPID 2>/dev/null; then
        echo "✅ Frontend: http://localhost:3000 (API proxy enabled)"
    else
        echo "⚠️  Frontend failed to start. Check /tmp/kolibri_frontend.log"
    fi
else
    echo "⚠️  Frontend dist not found. Run: cd frontend && npm run build"
    VPID=""
fi

echo ""
echo "============================================================"
echo "  🐦 Kolibri AI — Ready"
echo "============================================================"
echo "  Backend:  http://localhost:8001"
echo "  Proxy:    http://localhost:8003 (if running)"
echo "  Frontend: http://localhost:3000 (if running)"
echo ""
echo "  Test: curl http://localhost:8001/api/v1/health"
echo "  Chat: curl -X POST http://localhost:8001/api/v1/ai/chat \\"
echo "        -H 'Content-Type: application/json' \\"
echo "        -d '{\"message\":\"привет\"}'"
echo "============================================================"
echo ""
echo "Watchdog: auto-restart on crash every 3s"

# Watchdog: auto-restart on crash
while true; do
    sleep 3
    # Restart backend if dead
    if ! kill -0 $BPID 2>/dev/null; then
        echo "[watchdog] Backend crashed, restarting..."
        cd "$ROOT" && ./kolibri_http 8001 &
        BPID=$!
    fi
    # Restart frontend if dead
    if ! kill -0 $VPID 2>/dev/null; then
        echo "[watchdog] Frontend crashed, restarting..."
        cd "$FE" && node server.cjs > /tmp/kolibri_frontend.log 2>&1 &
        VPID=$!
    fi
done
