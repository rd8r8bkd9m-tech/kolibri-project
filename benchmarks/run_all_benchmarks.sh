#!/bin/bash
#
# Kolibri Benchmark Suite Runner
# Run all benchmarks and generate reports
#
# Usage:
#   ./run_all_benchmarks.sh [options]
#
# Options:
#   --quick     Run quick benchmarks only (1KB, 1MB)
#   --full      Run full benchmarks (up to 100MB/1GB)
#   --gpu       Include GPU benchmark (macOS only)
#   --clean     Clean build artifacts before running
#   --help      Show this help

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"

# Default options
QUICK_MODE=0
FULL_MODE=0
GPU_MODE=0
CLEAN_MODE=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --quick)
            QUICK_MODE=1
            shift
            ;;
        --full)
            FULL_MODE=1
            shift
            ;;
        --gpu)
            GPU_MODE=1
            shift
            ;;
        --clean)
            CLEAN_MODE=1
            shift
            ;;
        --help)
            echo "Kolibri Benchmark Suite Runner"
            echo ""
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --quick     Run quick benchmarks only (1KB, 1MB)"
            echo "  --full      Run full benchmarks (up to 100MB/1GB)"
            echo "  --gpu       Include GPU benchmark (macOS only)"
            echo "  --clean     Clean build artifacts before running"
            echo "  --help      Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Print banner
echo ""
echo "╔═══════════════════════════════════════════════════════════════════════════╗"
echo "║     KOLIBRI BENCHMARK SUITE RUNNER                                        ║"
echo "╚═══════════════════════════════════════════════════════════════════════════╝"
echo ""

# Create results directory
mkdir -p "$RESULTS_DIR"

# Detect OS and compiler flags
OS="$(uname -s)"
ARCH="$(uname -m)"
echo -e "${BLUE}System:${NC} $OS $ARCH"

# Set compiler flags
CC="${CC:-gcc}"
CFLAGS="-O3 -Wall -Wextra"

# Add architecture-specific flags
if [[ "$ARCH" == "x86_64" ]]; then
    CFLAGS="$CFLAGS -march=native"
elif [[ "$ARCH" == "arm64" ]] || [[ "$ARCH" == "aarch64" ]]; then
    CFLAGS="$CFLAGS -mcpu=native"
fi

# Add math library
LDFLAGS="-lm"

echo -e "${BLUE}Compiler:${NC} $CC"
echo -e "${BLUE}Flags:${NC} $CFLAGS"
echo ""

# Clean if requested
if [[ $CLEAN_MODE -eq 1 ]]; then
    echo -e "${YELLOW}Cleaning build artifacts...${NC}"
    rm -f "$SCRIPT_DIR/kolibri_benchmark_suite"
    rm -f "$SCRIPT_DIR/compare_with_competitors"
    rm -f "$SCRIPT_DIR/kolibri_gpu_benchmark"
    rm -f "$RESULTS_DIR"/*.json
    echo "Done."
    echo ""
fi

# Function to compile benchmark
compile_benchmark() {
    local name=$1
    local source=$2
    local output=$3
    local extra_flags=$4
    
    echo -e "${BLUE}Compiling${NC} $name..."
    
    if [[ -f "$output" ]] && [[ "$source" -ot "$output" ]]; then
        echo "  (using cached binary)"
        return 0
    fi
    
    if $CC $CFLAGS -I"$PROJECT_ROOT/backend/include" "$source" -o "$output" $LDFLAGS $extra_flags; then
        echo -e "  ${GREEN}✓${NC} Compiled successfully"
        return 0
    else
        echo -e "  ${RED}✗${NC} Compilation failed"
        return 1
    fi
}

# Compile benchmarks
echo "═══════════════════════════════════════════════════════════════════════════════"
echo "  COMPILING BENCHMARKS"
echo "═══════════════════════════════════════════════════════════════════════════════"
echo ""

# Main benchmark suite
compile_benchmark "Benchmark Suite" \
    "$SCRIPT_DIR/kolibri_benchmark_suite.c" \
    "$SCRIPT_DIR/kolibri_benchmark_suite"

# Competitor comparison
compile_benchmark "Competitor Comparison" \
    "$SCRIPT_DIR/compare_with_competitors.c" \
    "$SCRIPT_DIR/compare_with_competitors"

# GPU benchmark (macOS only)
if [[ "$OS" == "Darwin" ]] && [[ $GPU_MODE -eq 1 ]]; then
    echo -e "${BLUE}Compiling${NC} GPU Benchmark..."
    if clang -O3 -Wall -Wextra \
        -framework Metal -framework Foundation \
        "$SCRIPT_DIR/kolibri_gpu_benchmark.m" \
        -o "$SCRIPT_DIR/kolibri_gpu_benchmark"; then
        echo -e "  ${GREEN}✓${NC} Compiled successfully"
    else
        echo -e "  ${YELLOW}⚠${NC} GPU benchmark compilation failed (Metal not available?)"
        GPU_MODE=0
    fi
fi

echo ""

# Run benchmarks
echo "═══════════════════════════════════════════════════════════════════════════════"
echo "  RUNNING BENCHMARKS"
echo "═══════════════════════════════════════════════════════════════════════════════"
echo ""

# Determine benchmark mode flags
BENCH_FLAGS=""
if [[ $QUICK_MODE -eq 1 ]]; then
    BENCH_FLAGS="--quick"
elif [[ $FULL_MODE -eq 1 ]]; then
    BENCH_FLAGS="--full"
fi

# Run main benchmark suite
echo -e "${BLUE}Running${NC} Benchmark Suite..."
"$SCRIPT_DIR/kolibri_benchmark_suite" $BENCH_FLAGS \
    --json="$RESULTS_DIR/cpu_results.json" \
    --md="$RESULTS_DIR/cpu_results.md"

echo ""

# Run competitor comparison
echo -e "${BLUE}Running${NC} Competitor Comparison..."
"$SCRIPT_DIR/compare_with_competitors" \
    --json="$RESULTS_DIR/comparison.json"

echo ""

# Run GPU benchmark if available
if [[ $GPU_MODE -eq 1 ]] && [[ -f "$SCRIPT_DIR/kolibri_gpu_benchmark" ]]; then
    echo -e "${BLUE}Running${NC} GPU Benchmark..."
    "$SCRIPT_DIR/kolibri_gpu_benchmark" $BENCH_FLAGS \
        --json="$RESULTS_DIR/gpu_results.json"
    echo ""
fi

# Generate summary report
echo "═══════════════════════════════════════════════════════════════════════════════"
echo "  GENERATING REPORT"
echo "═══════════════════════════════════════════════════════════════════════════════"
echo ""

TIMESTAMP=$(date +"%Y-%m-%d %H:%M:%S")
REPORT_FILE="$RESULTS_DIR/BENCHMARK_REPORT.md"

cat > "$REPORT_FILE" << EOF
# Kolibri Benchmark Report

**Generated:** $TIMESTAMP  
**System:** $OS $ARCH  
**Compiler:** $CC  
**Flags:** $CFLAGS  

## Summary

This report contains the results of the Kolibri encoding benchmark suite.

## Test Configuration

- **Warmup iterations:** 3
- **Benchmark iterations:** 5-20 (adaptive)
- **Target duration:** 1000ms per test
- **Data pattern:** Pseudo-random (i * 73 + 17) % 256

## CPU Benchmark Results

EOF

if [[ -f "$RESULTS_DIR/cpu_results.md" ]]; then
    cat "$RESULTS_DIR/cpu_results.md" >> "$REPORT_FILE"
fi

cat >> "$REPORT_FILE" << EOF

## Competitor Comparison

| Encoding | Encode (GB/s) | Decode (GB/s) | Expansion Ratio |
|----------|---------------|---------------|-----------------|
EOF

# Parse comparison.json if available
if [[ -f "$RESULTS_DIR/comparison.json" ]]; then
    # Extract data from JSON using grep/sed (portable approach)
    python3 -c "
import json
with open('$RESULTS_DIR/comparison.json') as f:
    data = json.load(f)
for r in data.get('results', []):
    print(f\"| {r['name']} | {r['encode_gbps']:.2f} | {r['decode_gbps']:.2f} | {r['expansion_ratio']:.2f}x |\")
" 2>/dev/null >> "$REPORT_FILE" || echo "| (unable to parse results) |" >> "$REPORT_FILE"
fi

if [[ $GPU_MODE -eq 1 ]] && [[ -f "$RESULTS_DIR/gpu_results.json" ]]; then
    cat >> "$REPORT_FILE" << EOF

## GPU Benchmark Results

| Test | CPU (GB/s) | GPU (GB/s) | Speedup |
|------|------------|------------|---------|
EOF

    python3 -c "
import json
with open('$RESULTS_DIR/gpu_results.json') as f:
    data = json.load(f)
for r in data.get('results', []):
    print(f\"| {r['test']} | {r['cpu_gbps']:.2f} | {r['gpu_gbps']:.2f} | {r['speedup']:.1f}x |\")
" 2>/dev/null >> "$REPORT_FILE" || echo "| (unable to parse results) |" >> "$REPORT_FILE"
fi

cat >> "$REPORT_FILE" << EOF

## Methodology

### Timing
- High-resolution timing using \`clock_gettime(CLOCK_MONOTONIC)\` on Linux/macOS
- Warmup phase to stabilize CPU frequency and cache state
- Multiple iterations for statistical significance

### Data Generation
- Pseudo-random data pattern ensures consistent byte distribution
- All 256 byte values are represented in large test sizes

### Metrics
- **Throughput:** GB/s calculated as data_size / time
- **Latency:** Time in milliseconds per operation
- **Percentiles:** p50, p95, p99 for latency distribution

## Files

- \`cpu_results.json\` - Detailed CPU benchmark results
- \`comparison.json\` - Competitor comparison results
- \`gpu_results.json\` - GPU benchmark results (if available)
- \`BENCHMARK_REPORT.md\` - This report

---

*Report generated by Kolibri Benchmark Suite*
EOF

echo -e "${GREEN}Report generated:${NC} $REPORT_FILE"
echo ""

# Print summary
echo "═══════════════════════════════════════════════════════════════════════════════"
echo "  BENCHMARK COMPLETE"
echo "═══════════════════════════════════════════════════════════════════════════════"
echo ""
echo "Results saved to: $RESULTS_DIR/"
echo ""
echo "Files:"
ls -la "$RESULTS_DIR/"
echo ""
