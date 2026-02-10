#!/bin/bash
# ═══════════════════════════════════════════════════════
#  Kolibri OS Archiver Benchmark
#  Compares Kolibri (Huffman+ANS+LZ77+Math) vs Gzip, Bzip2, XZ
# ═══════════════════════════════════════════════════════

ARCHIVER="./build/kolibri_archiver"
OUT_DIR="./test_results/archiver"
mkdir -p "$OUT_DIR"

GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[0;33m'
dNC='\033[0m'

if [ ! -f "$ARCHIVER" ]; then
    echo "Error: $ARCHIVER not found. Please build first."
    exit 1
fi

echo -e "${CYAN}Starting Archiver Benchmark...${NC}"
echo "--------------------------------------------------------"
printf "%-20s | %-15s | %-10s | %-10s\n" "Method" "Size" "Ratio" "Time"
echo "--------------------------------------------------------"

function bench_file() {
    local INPUT="$1"
    local NAME="$2"
    
    if [ ! -f "$INPUT" ]; then
        echo "Skipping $NAME: File not found"
        return
    fi
    
    local ORIG_SIZE=$(stat -c%s "$INPUT")
    echo -e "\n${YELLOW}Testing set: $NAME ($((ORIG_SIZE/1024)) KB)${NC}"

    # 1. Kolibri
    local T_START=$(date +%s%N)
    "$ARCHIVER" compress "$INPUT" "$OUT_DIR/$NAME.klb" > /dev/null
    local T_END=$(date +%s%N)
    local K_SIZE=$(stat -c%s "$OUT_DIR/$NAME.klb")
    local K_TIME=$(( (T_END - T_START) / 1000000 ))
    local K_RATIO=$(echo "scale=2; $ORIG_SIZE / $K_SIZE" | bc)
    printf "%-20s | %-15s | %-10s | %-10s ms\n" "Kolibri OS" "$K_SIZE bytes" "${K_RATIO}x" "$K_TIME"

    # 2. Gzip (Fast)
    T_START=$(date +%s%N)
    gzip -c "$INPUT" > "$OUT_DIR/$NAME.gz"
    T_END=$(date +%s%N)
    local G_SIZE=$(stat -c%s "$OUT_DIR/$NAME.gz")
    local G_TIME=$(( (T_END - T_START) / 1000000 ))
    local G_RATIO=$(echo "scale=2; $ORIG_SIZE / $G_SIZE" | bc)
    printf "%-20s | %-15s | %-10s | %-10s ms\n" "Gzip (Deflate)" "$G_SIZE bytes" "${G_RATIO}x" "$G_TIME"

    # 3. Bzip2 (Strong)
    T_START=$(date +%s%N)
    bzip2 -k -c "$INPUT" > "$OUT_DIR/$NAME.bz2"
    T_END=$(date +%s%N)
    local B_SIZE=$(stat -c%s "$OUT_DIR/$NAME.bz2")
    local B_TIME=$(( (T_END - T_START) / 1000000 ))
    local B_RATIO=$(echo "scale=2; $ORIG_SIZE / $B_SIZE" | bc)
    printf "%-20s | %-15s | %-10s | %-10s ms\n" "Bzip2" "$B_SIZE bytes" "${B_RATIO}x" "$B_TIME"

    # 4. XZ (Extreme)
    T_START=$(date +%s%N)
    xz -k -c "$INPUT" > "$OUT_DIR/$NAME.xz"
    T_END=$(date +%s%N)
    local X_SIZE=$(stat -c%s "$OUT_DIR/$NAME.xz")
    local X_TIME=$(( (T_END - T_START) / 1000000 ))
    local X_RATIO=$(echo "scale=2; $ORIG_SIZE / $X_SIZE" | bc)
    printf "%-20s | %-15s | %-10s | %-10s ms\n" "XZ (LZMA2)" "$X_SIZE bytes" "${X_RATIO}x" "$X_TIME"
}

# 1. Text Data (Source Code)
echo -e "${CYAN}Preparing Source Code Tarball...${NC}"
tar -cf "$OUT_DIR/src.tar" backend/src apps
bench_file "$OUT_DIR/src.tar" "src.tar"

# 2. Large Data (KLM Model)
bench_file "data/models/kolibri_web.klm" "model.klm"

# 3. Small Text (Log)
if [ -f "logs/backend.log" ]; then
    bench_file "logs/backend.log" "backend.log"
fi

echo "--------------------------------------------------------"
echo -e "${GREEN}Benchmark Complete.${NC}"
