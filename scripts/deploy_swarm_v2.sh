#!/bin/bash
set -e

echo "═══════════════════════════════════════════"
echo "  KOLIBRI SWARM DEPLOY — FULL BUILD"
echo "═══════════════════════════════════════════"

NODE1="217.60.249.157"
NODE2="178.207.11.90"
NODE2_PORT="2222"
NODE2_USER="ladik"
KEY="~/.ssh/id_ed25519"

REMOTE_DIR="/opt/kolibri"

# ─── Install deps on both servers ───
echo "=== Installing dependencies ==="
for node in "$NODE1" "$NODE2"; do
    port=22; user="root"
    [ "$node" = "$NODE2" ] && { port=$NODE2_PORT; user=$NODE2_USER; }
    
    echo "→ $node:$port"
    ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no -i $KEY -p $port $user@$node "
        sudo apt-get update -qq && sudo apt-get install -y -qq gcc libc6-dev make 2>&1 | tail -2
        echo '✅ gcc ready'
    "
done

# ─── Prepare build dir on Node 1 ───
echo ""
echo "=== Preparing source on Node 1 ==="
ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "rm -rf $REMOTE_DIR && mkdir -p $REMOTE_DIR"

# Upload full source tree needed for build
echo "→ Uploading headers..."
ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "mkdir -p $REMOTE_DIR/backend/include/kolibri"
scp -r -q backend/include/kolibri/*.h root@$NODE1:$REMOTE_DIR/backend/include/kolibri/

echo "→ Upsembling server source..."
scp -q backend/src/kolibri_http_server.c root@$NODE1:$REMOTE_DIR/

echo "→ Uploading ALL C sources for library..."
for f in backend/src/*.c; do
    scp -q "$f" root@$NODE1:$REMOTE_DIR/ 2>/dev/null || true
done

echo "→ Uploading Makefile..."
scp -q Makefile root@$NODE1:$REMOTE_DIR/ 2>/dev/null || true

# Create a simple build script
cat > /tmp/build_kolibri.sh << 'BUILDEOF'
#!/bin/bash
cd /opt/kolibri

# Compile library
echo "Compiling kolibri_core.a..."
OBJECTS=""
for src in auto_learn.c attention.c corpus_trainer.c fractal_memory.c formula.c \
           genome.c knowledge_index.c logical_memory.c math_solver.c \
           numeric_tokenizer.c pattern_discovery.c reasoning_engine.c \
           domain_knowledge_loader.c self_verification.c explanation_generator.c \
           symbol_table.c world_model.c swarm_learner.c swarm_network.c \
           compress.c logical_solver.c fact_extractor.c kat_train_backprop.c \
           evolutionary_trainer.c royal.c sim.c knowledge.c knowledge_queue.c \
           net.c context.c corpus.c generation.c semantic.c simd_ops.c \
           threaded_inference.c trace.c vision.c web_crawler.c audio.c \
           async_executor.c huffman_ans.c predictive_compress.c digit_text.c \
           digits.c decimal.c random.c phoneme.c script.c math_utils.c; do
    if [ -f "$src" ]; then
        echo "  → $src"
        gcc -O2 -c -I backend/include -I include "$src" -o "${src%.c}.o" 2>/dev/null
        OBJECTS="$OBJECTS ${src%.c}.o"
    fi
done

if [ -n "$OBJECTS" ]; then
    ar rcs libkolibri_core.a $OBJECTS
    echo "✅ Library built: $(wc -c < libkolibri_core.a) bytes"
else
    echo "❌ No objects compiled"
    exit 1
fi

# Compile server
echo "Compiling kolibri_http..."
gcc -O2 -I backend/include -I include \
    -o kolibri_http kolibri_http_server.c libkolibri_core.a -lm -lpthread

echo "✅ Server built: $(wc -c < kolibri_http) bytes"

# Setup knowledge
mkdir -p knowledge
BUILDEOF

scp -q /tmp/build_kolibri.sh root@$NODE1:$REMOTE_DIR/build.sh
ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "chmod +x $REMOTE_DIR/build.sh"

echo ""
echo "=== Building on Node 1 ==="
ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "$REMOTE_DIR/build.sh" 2>&1 | tail -20

echo ""
echo "=== Starting Node 1 ==="
ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "
    pkill -9 kolibri_http 2>/dev/null || true
    cd $REMOTE_DIR
    ./kolibri_http 8001 > /var/log/kolibri.log 2>&1 &
    echo PID=\$!
"
sleep 3
ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "curl -s -m 3 http://localhost:8001/api/v1/health" && echo "" && echo "✅ Node 1 RUNNING" || echo "❌ Node 1 failed"

echo ""
echo "═══════════════════════════════════════════"
echo "  NODE 1 DEPLOYED"
echo "  http://$NODE1:8001"
echo "═══════════════════════════════════════════"
