#!/bin/bash
# Deploy kolibri swarm via git clone
set -e

NODE1="217.60.249.157"
NODE2="178.207.11.90"
NODE2_PORT="2222"
NODE2_USER="ladik"
KEY="~/.ssh/id_ed25519"
REMOTE_DIR="/opt/kolibri"

echo "═══════════════════════════════════════════"
echo "  KOLIBRI SWARM — DEPLOY VIA GIT"
echo "═══════════════════════════════════════════"

# ─── Build script that runs on remote server ───
BUILD_SCRIPT='#!/bin/bash
set -e
cd /opt
rm -rf kolibri-project 2>/dev/null || true
git clone git@github.com:rd8r8bkd9m-tech/kolibri-project.git
cd kolibri-project

echo "✅ Cloned"
echo "Building kolibri_http..."
cc -O2 -I backend/include -I backend/include/kolibri \
   -o kolibri_http backend/src/kolibri_http_server.c \
   build/libkolibri_core.a -lm -lpthread 2>/dev/null || {
    echo "No prebuilt library, building from source..."
    # Compile all needed .o files
    OBJS=""
    for src in backend/src/reasoning_engine.c backend/src/math_solver.c \
               backend/src/domain_knowledge_loader.c backend/src/self_verification.c \
               backend/src/explanation_generator.c backend/src/numeric_tokenizer.c \
               backend/src/knowledge_index.c backend/src/fractal_memory.c \
               backend/src/logical_memory.c backend/src/formula.c \
               backend/src/symbol_table.c backend/src/world_model.c \
               backend/src/auto_learn.c backend/src/swarm_learner.c \
               backend/src/swarm_network.c backend/src/pattern_discovery.c \
               backend/src/logical_solver.c backend/src/fact_extractor.c \
               backend/src/kat_train_backprop.c backend/src/evolutionary_trainer.c \
               backend/src/roy.c backend/src/sim.c backend/src/knowledge.c \
               backend/src/knowledge_queue.c backend/src/net.c \
               backend/src/context_window.c backend/src/corpus_trainer.c \
               backend/src/compress.c backend/src/genome.c \
               backend/src/attention.c backend/src/corpus_learning.c \
               backend/src/digits.c backend/src/decimal.c backend/src/random.c \
               backend/src/digit_text.c backend/src/phoneme.c \
               backend/src/script.c backend/src/trace.c \
               backend/src/web_crawler.c backend/src/audio.c \
               backend/src/vision.c backend/src/generation.c \
               backend/src/semantic.c backend/src/simd_ops.c \
               backend/src/threaded_inference.c backend/src/formula_logic.c \
               backend/src/logical_memory.c backend/src/math_utils.c \
               backend/src/huffman_ans.c backend/src/predictive_compress.c \
               backend/src/async_executor.c backend/src/autonomous_learning.c; do
        if [ -f "$src" ]; then
            gcc -O2 -c -I backend/include -I backend/include/kolibri "$src" -o "${src%.c}.o" 2>/dev/null && \
            OBJS="$OBJS ${src%.c}.o"
        fi
    done
    if [ -n "$OBJS" ]; then
        ar rcs build/libkolibri_core.a $OBJS
        echo "✅ Library built"
    fi
    cc -O2 -I backend/include -I backend/include/kolibri \
       -o kolibri_http backend/src/kolibri_http_server.c \
       build/libkolibri_core.a -lm -lpthread
}
echo "✅ kolibri_http built: $(wc -c < kolibri_http) bytes"

# Setup knowledge
mkdir -p knowledge/swarm
'

echo ""
echo "=== Deploy Node 1 ($NODE1) ==="
ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "
    which gcc >/dev/null 2>&1 || { echo 'Installing gcc...'; apt-get update -qq && apt-get install -y -qq gcc libc6-dev git make 2>&1 | tail -2; }
    mkdir -p /opt/build
"

# Upload build script
echo "$BUILD_SCRIPT" > /tmp/build_node.sh
scp -q -i $KEY /tmp/build_node.sh root@$NODE1:/opt/build_node.sh
ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "chmod +x /opt/build_node.sh && /opt/build_node.sh" 2>&1 | tail -10

echo ""
echo "=== Starting Node 1 ==="
ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "
    pkill -9 kolibri_http 2>/dev/null || true
    sleep 1
    cd /opt/kolibri-project
    cp knowledge/swarm/node1_knowledge.md knowledge/knowledge_base.md 2>/dev/null || true
    ./kolibri_http 8001 > /var/log/kolibri.log 2>&1 &
    sleep 2
    curl -s -m 3 http://localhost:8001/api/v1/health
"
echo "✅ Node 1: http://$NODE1:8001"

echo ""
echo "=== Deploy Node 2 ($NODE2:$NODE2_PORT) ==="
ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=no -i $KEY -p $NODE2_PORT $NODE2_USER@$NODE2 "
    which gcc >/dev/null 2>&1 || { echo 'Installing gcc...'; sudo apt-get update -qq && sudo apt-get install -y -qq gcc libc6-dev git make 2>&1 | tail -2; }
    mkdir -p /opt/build
"

scp -q -i $KEY -P $NODE2_PORT /tmp/build_node.sh $NODE2_USER@$NODE2:/opt/build_node.sh
ssh -o StrictHostKeyChecking=no -i $KEY -p $NODE2_PORT $NODE2_USER@$NODE2 "chmod +x /opt/build_node.sh && /opt/build_node.sh" 2>&1 | tail -10

echo ""
echo "=== Starting Node 2 ==="
ssh -o StrictHostKeyChecking=no -i $KEY -p $NODE2_PORT $NODE2_USER@$NODE2 "
    pkill -9 kolibri_http 2>/dev/null || true
    sleep 1
    cd /opt/kolibri-project
    cp knowledge/swarm/node2_knowledge.md knowledge/knowledge_base.md 2>/dev/null || true
    sudo ./kolibri_http 8001 > /var/log/kolibri.log 2>&1 &
    sleep 2
    curl -s -m 3 http://localhost:8001/api/v1/health
"
echo "✅ Node 2: http://$NODE2:$NODE2_PORT"

echo ""
echo "═══════════════════════════════════════════"
echo "  SWARM DEPLOYED!"
echo "  Node 1: http://$NODE1:8001 (10K facts)"
echo "  Node 2: http://$NODE2:$NODE2_PORT (10K facts)"
echo "═══════════════════════════════════════════"
