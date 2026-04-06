#!/bin/bash
# ═══════════════════════════════════════════════════════════
#  KOLIBRI UNIFIED SWARM BACKEND
#  Запускает единую сеть для kolibriai.ru и Mac
# ═══════════════════════════════════════════════════════════
set -e
cd "$(dirname "$0")"

echo "╔══════════════════════════════════════════════════════╗"
echo "║   KOLIBRI UNIFIED SWARM BACKEND                     ║"
echo "╚══════════════════════════════════════════════════════╝"

# Stop existing
pkill -f kolibri_http 2>/dev/null || true
pkill -f kolibri_swarm_mac 2>/dev/null || true
pkill -f kolibri_mac_proxy 2>/dev/null || true
sleep 2

# 1. Compile kolibri_http
echo ""
echo "=== 1. kolibri_http :8001 ==="
cc -O2 -I backend/include -I backend/include/kolibri \
   -I/opt/homebrew/opt/openssl@3/include \
   -o kolibri_http backend/src/kolibri_http_server.c \
   build/libkolibri_core.a -L/opt/homebrew/opt/openssl@3/lib \
   -lssl -lcrypto -lm -lpthread 2>&1 | grep -v warning | head -3
echo "  ✅ $(wc -c < kolibri_http) bytes"
nohup ./kolibri_http 8001 > /tmp/kolibri_http.log 2>&1 &
echo "  PID: $!"

# 2. Compile kolibri_swarm_mac
echo ""
echo "=== 2. kolibri_swarm_mac :8002 ==="
cc -O2 -o kolibri_swarm_mac kolibri_swarm_node.c -lm 2>&1 | grep error || echo "  ✅ compiled"

# Merge knowledge
mkdir -p knowledge/swarm
if [ -f knowledge/swarm/node1_knowledge.md ] && [ -f knowledge/swarm/node2_knowledge.md ]; then
    cat knowledge/swarm/node1_knowledge.md knowledge/swarm/node2_knowledge.md > knowledge/knowledge_base.md
    echo "  📚 $(grep -c '### Q' knowledge/knowledge_base.md) facts"
fi

nohup ./kolibri_swarm_mac 8002 \
    --peer 217.60.249.157:8001 \
    < /dev/null > /tmp/kolibri_swarm_mac.log 2>&1 &
echo "  PID: $!"

# 3. Hybrid proxy
echo ""
echo "=== 3. hybrid_proxy :8003 ==="
nohup node kolibri_mac_proxy.js > /tmp/kolibri_mac_proxy.log 2>&1 &
echo "  PID: $!"

sleep 6

# Health
echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║   HEALTH CHECKS                                      ║"
echo "╚══════════════════════════════════════════════════════╝"

for port in 8001 8002 8003; do
    echo -n "  :$port → "
    curl -s -m 3 http://localhost:$port/api/v1/health 2>/dev/null | python3 -c "
import sys,json
try:
 d=json.load(sys.stdin)
 facts=d.get('facts',d.get('total_facts','?'))
 peers=d.get('peers','')
 if peers: print(f'✅ {facts} facts, {peers} peers')
 else: print(f'✅ {facts}')
except: print('❌')
" 2>/dev/null
done

# Test
echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║   TEST                                               ║"
echo "╚══════════════════════════════════════════════════════╝"

for q in "Столица Австралии" "Скорость света" "Что такое Docker"; do
    echo -n "  $q → "
    curl -s -X POST http://localhost:8003/api/v1/ai/chat \
      -d "{\"message\":\"$q\",\"conversation_id\":\"t\"}" \
      -H "Content-Type: application/json" 2>/dev/null | python3 -c "
import sys,json
try:
 d=json.load(sys.stdin)
 print(f\"[{d.get('source','?')}] {d.get('response','?')[:60]}\")
except: print('ERR')
" 2>/dev/null
done

echo ""
echo "═══════════════════════════════════════════════════════"
echo "  KOLIBRI SWARM BACKEND READY!"
echo "  Frontend:  http://localhost:3000"
echo "  API:       http://localhost:8003"
echo "  Swarm:     http://localhost:8002"
echo "  C-Core:    http://localhost:8001"
echo "═══════════════════════════════════════════════════════"
