#!/bin/bash
set -e

echo "═══════════════════════════════════════════"
echo "  KOLIBRI SWARM DEPLOY SCRIPT"
echo "═══════════════════════════════════════════"

NODE1="217.60.249.157"
NODE2="178.207.11.90"
NODE2_PORT="2222"
NODE2_USER="ladik"
KEY="~/.ssh/id_ed25519"

REMOTE_DIR="/opt/kolibri"

# Step 1: Build for Linux x86_64
echo ""
echo "=== Step 1: Cross-compiling for Linux x86_64 ==="

# Check if we have a Linux cross-compiler or need to build on server
if command -v x86_64-linux-gnu-gcc &> /dev/null; then
    echo "Using cross-compiler..."
    CC="x86_64-linux-gnu-gcc"
elif command -v o64-clang &> /dev/null; then
    CC="o64-clang"
else
    echo "No cross-compiler found. Building on remote server..."
    echo "=== Step 1a: Copy source to Node 1 for build ==="
    ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "mkdir -p $REMOTE_DIR/build"
    scp -i $KEY -r backend/include backend/src CMakeLists.txt root@$NODE1:$REMOTE_DIR/build/
    ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "cd $REMOTE_DIR/build && cmake . -DCMAKE_BUILD_TYPE=Release && make -j4 kolibri_http"
    echo "✅ Built on Node 1"
    echo ""
    echo "=== Step 1b: Copy binary back ==="
    scp -i $KEY root@$NODE1:$REMOTE_DIR/build/kolibri_http ./kolibri_http_linux
    echo "✅ Binary retrieved"
    CC="cc"
fi

if [ "$CC" = "cc" ]; then
    echo "Trying local build (may not work for Linux)..."
    cc -O2 -I backend/include -I backend/include/kolibri -o kolibri_http_linux backend/src/kolibri_http_server.c build/libkolibri_core.a -lm -lpthread 2>/dev/null || echo "⚠️ Local build failed, will build on server"
fi

echo ""
echo "=== Step 2: Deploy to Node 1 ($NODE1) ==="
ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "mkdir -p $REMOTE_DIR"
ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "mkdir -p $REMOTE_DIR/knowledge/swarm"

# Copy binary
if [ -f kolibri_http_linux ]; then
    scp -i $KEY kolibri_http_linux root@$NODE1:$REMOTE_DIR/kolibri_http
    echo "✅ Binary copied"
else
    # Build on server
    echo "Building on Node 1..."
    ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "mkdir -p $REMOTE_DIR/kolibri-build/include/kolibri"
    scp -i $KEY backend/include/kolibri/*.h root@$NODE1:$REMOTE_DIR/kolibri-build/include/kolibri/
    scp -i $KEY backend/src/kolibri_http_server.c root@$NODE1:$REMOTE_DIR/kolibri-build/
    scp -i $KEY build/libkolibri_core.a root@$NODE1:$REMOTE_DIR/kolibri-build/
    ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "cd $REMOTE_DIR/kolibri-build && cc -O2 -I include -o $REMOTE_DIR/kolibri_http kolibri_http_server.c libkolibri_core.a -lm -lpthread"
    echo "✅ Built on Node 1"
fi

# Copy knowledge shard
scp -i $KEY knowledge/swarm/node1_knowledge.md root@$NODE1:$REMOTE_DIR/knowledge/knowledge_base.md
echo "✅ Knowledge shard copied to Node 1"

# Create startup script
cat > /tmp/kolibri_node1.sh << 'REMOTE_SCRIPT'
#!/bin/bash
pkill -9 kolibri_http 2>/dev/null || true
sleep 1
cd /opt/kolibri
./kolibri_http 8001 > /var/log/kolibri_node1.log 2>&1 &
echo "Node 1 started, PID=$!"
REMOTE_SCRIPT

scp -i $KEY /tmp/kolibri_node1.sh root@$NODE1:$REMOTE_DIR/start.sh
ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "chmod +x $REMOTE_DIR/start.sh && $REMOTE_DIR/start.sh"
sleep 2

# Health check
if ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "curl -s -m 3 http://localhost:8001/api/v1/health"; then
    echo ""
    echo "✅ Node 1 HEALTHY"
else
    echo ""
    echo "❌ Node 1 FAILED - check logs"
    ssh -o StrictHostKeyChecking=no -i $KEY root@$NODE1 "tail -20 /var/log/kolibri_node1.log" || true
fi

echo ""
echo "=== Step 3: Deploy to Node 2 ($NODE2:$NODE2_PORT) ==="

# Install gcc on Node 2 if needed
ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no -i $KEY -p $NODE2_PORT $NODE2_USER@$NODE2 "which gcc >/dev/null 2>&1 || (echo 'Installing gcc...' && sudo apt-get update -qq && sudo apt-get install -y -qq gcc libc6-dev 2>&1 | tail -3)"
echo ""

ssh -o StrictHostKeyChecking=no -i $KEY -p $NODE2_PORT $NODE2_USER@$NODE2 "mkdir -p $REMOTE_DIR"
ssh -o StrictHostKeyChecking=no -i $KEY -p $NODE2_PORT $NODE2_USER@$NODE2 "mkdir -p $REMOTE_DIR/knowledge/swarm"

# Copy binary
if [ -f kolibri_http_linux ]; then
    scp -i $KEY -P $NODE2_PORT kolibri_http_linux $NODE2_USER@$NODE2:$REMOTE_DIR/kolibri_http
    echo "✅ Binary copied"
else
    echo "Building on Node 2..."
    ssh -o StrictHostKeyChecking=no -i $KEY -p $NODE2_PORT $NODE2_USER@$NODE2 "mkdir -p $REMOTE_DIR/kolibri-build/include/kolibri"
    scp -i $KEY -P $NODE2_PORT backend/include/kolibri/*.h $NODE2_USER@$NODE2:$REMOTE_DIR/kolibri-build/include/kolibri/
    scp -i $KEY -P $NODE2_PORT backend/src/kolibri_http_server.c $NODE2_USER@$NODE2:$REMOTE_DIR/kolibri-build/
    scp -i $KEY -P $NODE2_PORT build/libkolibri_core.a $NODE2_USER@$NODE2:$REMOTE_DIR/kolibri-build/
    ssh -o StrictHostKeyChecking=no -i $KEY -p $NODE2_PORT $NODE2_USER@$NODE2 "cd $REMOTE_DIR/kolibri-build && cc -O2 -I include -o $REMOTE_DIR/kolibri_http kolibri_http_server.c libkolibri_core.a -lm -lpthread"
    echo "✅ Built on Node 2"
fi

# Copy knowledge shard
scp -i $KEY -P $NODE2_PORT knowledge/swarm/node2_knowledge.md $NODE2_USER@$NODE2:$REMOTE_DIR/knowledge/knowledge_base.md
echo "✅ Knowledge shard copied to Node 2"

# Create startup script
cat > /tmp/kolibri_node2.sh << 'REMOTE_SCRIPT'
#!/bin/bash
pkill -9 kolibri_http 2>/dev/null || true
sleep 1
cd /opt/kolibri
./kolibri_http 8001 > /var/log/kolibri_node2.log 2>&1 &
echo "Node 2 started, PID=$!"
REMOTE_SCRIPT

scp -i $KEY -P $NODE2_PORT /tmp/kolibri_node2.sh $NODE2_USER@$NODE2:$REMOTE_DIR/start.sh
ssh -o StrictHostKeyChecking=no -i $KEY -p $NODE2_PORT $NODE2_USER@$NODE2 "chmod +x $REMOTE_DIR/start.sh && sudo $REMOTE_DIR/start.sh"
sleep 2

# Health check
if ssh -o StrictHostKeyChecking=no -i $KEY -p $NODE2_PORT $NODE2_USER@$NODE2 "curl -s -m 3 http://localhost:8001/api/v1/health"; then
    echo ""
    echo "✅ Node 2 HEALTHY"
else
    echo ""
    echo "❌ Node 2 FAILED - check logs"
    ssh -o StrictHostKeyChecking=no -i $KEY -p $NODE2_PORT $NODE2_USER@$NODE2 "tail -20 /var/log/kolibri_node2.log" || true
fi

echo ""
echo "═══════════════════════════════════════════"
echo "  DEPLOY COMPLETE"
echo "═══════════════════════════════════════════"
echo "Node 1: http://$NODE1:8001 (Math/Logic/Science - 10K facts)"
echo "Node 2: http://$NODE2:$NODE2_PORT (Geography/History/Tech - 10K facts)"
echo ""
echo "Next: Configure swarm peering between nodes"
