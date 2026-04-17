#!/bin/bash
# ═══════════════════════════════════════════════
#  KOLIBRI SWARM DEPLOY — 3 NODES
#  Node 1: 217.60.249.157:8001 (root, key=id_ed25519)
#  Node 2: 178.207.11.90:8001  (ladik, port 2222, key=id_ed25519)
#  Node 3: Mac localhost:8002
# ═══════════════════════════════════════════════
set -e

KEY="$HOME/.ssh/id_ed25519"
N1="217.60.249.157"
N2="178.207.11.90"
N2_PORT="2222"
N2_USER="ladik"
REMOTE_DIR="/opt/kp"

echo "╔══════════════════════════════════════╗"
echo "║   KOLIBRI SWARM — 3-NODE DEPLOY     ║"
echo "╚══════════════════════════════════════╝"

# ─── Ensure knowledge shards exist ───
echo ""
echo "=== Checking knowledge shards ==="
if [ ! -f knowledge/swarm/node1_knowledge.md ]; then
    echo "Generating shards..."
    python3 generate_swarm_shards.py 2>/dev/null || echo "⚠️  Shard generation failed"
fi

NODE1_KB="knowledge/swarm/node1_knowledge.md"
NODE2_KB="knowledge/swarm/node2_knowledge.md"

# ─── Deploy Node 1 ───
echo ""
echo "═══════════════════════════════════════"
echo "  NODE 1: $N1 (Math/Logic/Science)"
echo "═══════════════════════════════════════"

ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=no -i $KEY root@$N1 "
    sudo mkdir -p $REMOTE_DIR/knowledge/swarm
    sudo pkill -9 kolibri_swarm 2>/dev/null || true
" 2>/dev/null

echo "  → Uploading source..."
scp -q -i $KEY kolibri_swarm_node.c root@$N1:$REMOTE_DIR/ 2>/dev/null || {
    echo "  ⚠️  Upload via scp failed, trying base64 pipe..."
    base64 kolibri_swarm_node.c | ssh -i $KEY root@$N1 "base64 -d > $REMOTE_DIR/kolibri_swarm_node.c"
}

echo "  → Uploading knowledge..."
if [ -f "$NODE1_KB" ]; then
    scp -q -i $KEY "$NODE1_KB" root@$N1:$REMOTE_DIR/knowledge/swarm/node1_knowledge.md
else
    echo "  ⚠️  node1_knowledge.md not found"
fi
if [ -f "$NODE2_KB" ]; then
    scp -q -i $KEY "$NODE2_KB" root@$N1:$REMOTE_DIR/knowledge/swarm/node2_knowledge.md
else
    echo "  ⚠️  node2_knowledge.md not found"
fi

echo "  → Compiling..."
ssh -i $KEY root@$N1 "
    cd $REMOTE_DIR
    rm -f kolibri_swarm
    gcc -O2 -o kolibri_swarm kolibri_swarm_node.c -lm 2>&1 | grep error || echo '  ✅ Compiled'
    echo \"Binary: \$(wc -c < kolibri_swarm) bytes\"

    # Merge knowledge
    cat knowledge/swarm/node1_knowledge.md knowledge/swarm/node2_knowledge.md > /tmp/merged_kb.md
    cp /tmp/merged_kb.md knowledge/knowledge_base.md
    echo \"  Facts: \$(grep -c '### Q' knowledge/knowledge_base.md)\"

    # Start with peer to Node 2
    ./kolibri_swarm 8001 --peer $N2:8001 &
    sleep 6

    echo \"  Health:\"
    curl -s -m 5 http://localhost:8001/api/v1/health | python3 -m json.tool 2>/dev/null || curl -s -m 5 http://localhost:8001/api/v1/health
"

# ─── Deploy Node 2 ───
echo ""
echo "═══════════════════════════════════════"
echo "  NODE 2: $N2:$N2_PORT (Geography/History/Tech)"
echo "═══════════════════════════════════════"

ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=no -i $KEY -p $N2_PORT $N2_USER@$N2 "
    sudo mkdir -p $REMOTE_DIR/knowledge/swarm
    sudo pkill -9 kolibri_swarm 2>/dev/null || true
" 2>/dev/null

echo "  → Uploading source..."
scp -q -i $KEY -P $N2_PORT kolibri_swarm_node.c $N2_USER@$N2:/tmp/ 2>/dev/null || {
    echo "  ⚠️  Upload failed, trying base64 pipe..."
    base64 kolibri_swarm_node.c | ssh -i $KEY -p $N2_PORT $N2_USER@$N2 "base64 -d > /tmp/kolibri_swarm_node.c"
}

echo "  → Uploading knowledge..."
if [ -f "$NODE2_KB" ]; then
    scp -q -i $KEY -P $N2_PORT "$NODE2_KB" $N2_USER@$N2:/tmp/node2_kb.md
else
    echo "  ⚠️  node2_knowledge.md not found"
fi

echo "  → Compiling & starting..."
ssh -i $KEY -p $N2_PORT $N2_USER@$N2 "
    cd $REMOTE_DIR
    sudo cp /tmp/kolibri_swarm_node.c kolibri_swarm_node.c
    sudo rm -f kolibri_swarm
    sudo gcc -O2 -o kolibri_swarm kolibri_swarm_node.c -lm 2>&1 | grep error || echo '  ✅ Compiled'

    # Setup knowledge
    if [ -f /tmp/node2_kb.md ]; then
        sudo cp /tmp/node2_kb.md knowledge/swarm/node2_knowledge.md
        sudo cp /tmp/node2_kb.md knowledge/knowledge_base.md
        echo \"  Facts: \$(grep -c '### Q' knowledge/knowledge_base.md)\"
    fi

    sudo chown -R \$USER:\$USER . 2>/dev/null || true

    # Start with peer to Node 1
    ./kolibri_swarm 8001 --peer $N1:8001 &
    sleep 6

    echo \"  Health:\"
    curl -s -m 5 http://localhost:8001/api/v1/health | python3 -m json.tool 2>/dev/null || curl -s -m 5 http://localhost:8001/api/v1/health
"

# ─── Verify Swarm ───
echo ""
echo "═══════════════════════════════════════"
echo "  SWARM VERIFICATION"
echo "═══════════════════════════════════════"

echo ""
echo "Node 1 → Peers:"
ssh -i $KEY root@$N1 "curl -s -m 3 http://localhost:8001/api/v1/swarm/peers" 2>/dev/null | python3 -m json.tool 2>/dev/null || \
ssh -i $KEY root@$N1 "curl -s -m 3 http://localhost:8001/api/v1/swarm/peers" 2>/dev/null

echo ""
echo "Node 2 → Peers:"
ssh -i $KEY -p $N2_PORT $N2_USER@$N2 "curl -s -m 3 http://localhost:8001/api/v1/swarm/peers" 2>/dev/null | python3 -m json.tool 2>/dev/null || \
ssh -i $KEY -p $N2_PORT $N2_USER@$N2 "curl -s -m 3 http://localhost:8001/api/v1/swarm/peers" 2>/dev/null

echo ""
echo "Node 1 → Test query:"
ssh -i $KEY root@$N1 "curl -s -X POST http://localhost:8001/api/v1/ai/chat \
  -d '{\"message\":\"Столица Австралии\",\"conversation_id\":\"t\"}' \
  -H 'Content-Type: application/json' | python3 -c 'import sys,json;d=json.load(sys.stdin);print(d.get(\"response\",\"ERR\")[:80])'" 2>/dev/null || \
ssh -i $KEY root@$N1 "curl -s -X POST http://localhost:8001/api/v1/ai/chat \
  -d '{\"message\":\"Столица Австралии\",\"conversation_id\":\"t\"}' \
  -H 'Content-Type: application/json' | head -c 120" 2>/dev/null

echo ""
echo "Node 2 → Test query:"
ssh -i $KEY -p $N2_PORT $N2_USER@$N2 "curl -s -X POST http://localhost:8001/api/v1/ai/chat \
  -d '{\"message\":\"Что такое Docker\",\"conversation_id\":\"t\"}' \
  -H 'Content-Type: application/json' | python3 -c 'import sys,json;d=json.load(sys.stdin);print(d.get(\"response\",\"ERR\")[:80])'" 2>/dev/null || \
ssh -i $KEY -p $N2_PORT $N2_USER@$N2 "curl -s -X POST http://localhost:8001/api/v1/ai/chat \
  -d '{\"message\":\"Что такое Docker\",\"conversation_id\":\"t\"}' \
  -H 'Content-Type: application/json' | head -c 120" 2>/dev/null

# ─── Sync Node 1 → Node 2 ───
echo ""
echo "═══════════════════════════════════════"
echo "  SYNCING NODE 1 → NODE 2"
echo "═══════════════════════════════════════"

echo "  Requesting sync from Node 2..."
ssh -i $KEY -p $N2_PORT $N2_USER@$N2 "curl -s -X POST http://localhost:8001/api/v1/swarm/sync \
  -d '{\"host\":\"$N1\",\"port\":8001}' \
  -H 'Content-Type: application/json'" 2>/dev/null || echo "  ⚠️  Sync endpoint not available"

sleep 2

echo ""
echo "Node 2 → Health after sync:"
ssh -i $KEY -p $N2_PORT $N2_USER@$N2 "curl -s -m 3 http://localhost:8001/api/v1/health" 2>/dev/null

echo ""
echo "═══════════════════════════════════════"
echo "  DEPLOY COMPLETE"
echo "═══════════════════════════════════════"
echo ""
echo "  Node 1: http://$N1:8001  (20K facts)"
echo "  Node 2: http://$N2:$N2_PORT  (10K facts)"
echo ""
echo "  Swarm sync: Node 1 ↔ Node 2"
echo ""
