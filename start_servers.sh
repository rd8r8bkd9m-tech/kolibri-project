#!/bin/bash
# start_servers.sh — Start all Kolibri servers
# Usage: ./start_servers.sh

echo "🐦 Starting Kolibri AI servers..."

# Kill old servers
pkill -f kolibri_http 2>/dev/null
pkill -f kolibri_proxy 2>/dev/null
sleep 1

# Start C server
echo "📦 Starting C server on port 8001..."
./kolibri_http 8001 &>/tmp/kolibri_c.log &
C_PID=$!
echo "   C server PID: $C_PID"
sleep 1

# Start proxy
echo "🔄 Starting proxy on port 8003..."
node kolibri_proxy.js 8003 &>/tmp/kolibri_proxy.log &
PX_PID=$!
echo "   Proxy PID: $PX_PID"
sleep 1

# Test
echo ""
echo "=== Testing ==="
curl -s -X POST http://localhost:8003/api/v1/ai/chat -d '{"message":"Столица Франции","conversation_id":"test"}' -H 'Content-Type: application/json' | python3 -c "
import sys,json
try:
    d=json.load(sys.stdin)
    print(f'✅ [{d.get(\"method\",\"?\")}] {d.get(\"response\",\"ERR\")[:70]}')
except:
    print('❌ Error')
"

echo ""
echo "🚀 All servers running!"
echo "   C server:  http://localhost:8001"
echo "   Proxy:     http://localhost:8003"
echo ""
echo "To stop: pkill -f kolibri_http; pkill -f kolibri_proxy"
