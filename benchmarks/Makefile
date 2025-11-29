# Kolibri Benchmark Suite Makefile
#
# Targets:
#   make benchmark       - Run all benchmarks (default mode)
#   make benchmark-quick - Quick benchmark (1KB, 1MB only)
#   make benchmark-full  - Full benchmark (up to 100MB)
#   make test            - Run correctness tests
#   make stress          - Run stress tests
#   make report          - Generate full report
#   make clean           - Remove build artifacts

# Compiler settings
CC ?= gcc
CFLAGS = -O3 -Wall -Wextra -std=c11
LDFLAGS = -lm -lpthread

# Detect architecture for optimization flags
ARCH := $(shell uname -m)
ifeq ($(ARCH),x86_64)
    CFLAGS += -march=native
else ifeq ($(ARCH),arm64)
    CFLAGS += -mcpu=native
else ifeq ($(ARCH),aarch64)
    CFLAGS += -mcpu=native
endif

# Detect OS
UNAME_S := $(shell uname -s)

# Include paths
INCLUDE_DIR = ../backend/include
CFLAGS += -I$(INCLUDE_DIR)

# Source files
BENCH_SUITE_SRC = kolibri_benchmark_suite.c
COMPARE_SRC = compare_with_competitors.c
GPU_BENCH_SRC = kolibri_gpu_benchmark.m
TEST_CORRECT_SRC = ../tests/test_correctness.c
TEST_STRESS_SRC = ../tests/test_stress.c

# Output binaries
BENCH_SUITE = kolibri_benchmark_suite
COMPARE = compare_with_competitors
GPU_BENCH = kolibri_gpu_benchmark
TEST_CORRECT = test_correctness
TEST_STRESS = test_stress

# Results directory
RESULTS_DIR = results

# Default target
.PHONY: all
all: build benchmark

# Build all binaries
.PHONY: build
build: $(BENCH_SUITE) $(COMPARE) $(TEST_CORRECT) $(TEST_STRESS)
ifeq ($(UNAME_S),Darwin)
	@if [ -f $(GPU_BENCH_SRC) ]; then \
		echo "Building GPU benchmark..."; \
		clang -O3 -Wall -Wextra -framework Metal -framework Foundation \
			$(GPU_BENCH_SRC) -o $(GPU_BENCH) 2>/dev/null || \
			echo "Warning: GPU benchmark requires Metal framework"; \
	fi
endif

# Build benchmark suite
$(BENCH_SUITE): $(BENCH_SUITE_SRC)
	@echo "Building $(BENCH_SUITE)..."
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Build competitor comparison
$(COMPARE): $(COMPARE_SRC)
	@echo "Building $(COMPARE)..."
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Build correctness tests
$(TEST_CORRECT): $(TEST_CORRECT_SRC)
	@echo "Building $(TEST_CORRECT)..."
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Build stress tests
$(TEST_STRESS): $(TEST_STRESS_SRC)
	@echo "Building $(TEST_STRESS)..."
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Create results directory
$(RESULTS_DIR):
	@mkdir -p $(RESULTS_DIR)

# Run default benchmark
.PHONY: benchmark
benchmark: $(BENCH_SUITE) $(COMPARE) $(RESULTS_DIR)
	@echo "Running benchmark..."
	./$(BENCH_SUITE) --json=$(RESULTS_DIR)/cpu_results.json --md=$(RESULTS_DIR)/cpu_results.md
	@echo ""
	./$(COMPARE) --json=$(RESULTS_DIR)/comparison.json

# Run quick benchmark
.PHONY: benchmark-quick
benchmark-quick: $(BENCH_SUITE) $(COMPARE) $(RESULTS_DIR)
	@echo "Running quick benchmark..."
	./$(BENCH_SUITE) --quick --json=$(RESULTS_DIR)/cpu_results.json --md=$(RESULTS_DIR)/cpu_results.md
	@echo ""
	./$(COMPARE) --json=$(RESULTS_DIR)/comparison.json

# Run full benchmark (up to 100MB)
.PHONY: benchmark-full
benchmark-full: $(BENCH_SUITE) $(COMPARE) $(RESULTS_DIR)
	@echo "Running full benchmark..."
	./$(BENCH_SUITE) --full --json=$(RESULTS_DIR)/cpu_results.json --md=$(RESULTS_DIR)/cpu_results.md
	@echo ""
	./$(COMPARE) --json=$(RESULTS_DIR)/comparison.json

# Run GPU benchmark (macOS only)
.PHONY: benchmark-gpu
benchmark-gpu: $(GPU_BENCH) $(RESULTS_DIR)
ifeq ($(UNAME_S),Darwin)
	@echo "Running GPU benchmark..."
	./$(GPU_BENCH) --json=$(RESULTS_DIR)/gpu_results.json
else
	@echo "GPU benchmark is only available on macOS"
endif

# Run correctness tests
.PHONY: test
test: $(TEST_CORRECT)
	@echo "Running correctness tests..."
	./$(TEST_CORRECT)

# Run stress tests (quick)
.PHONY: stress
stress: $(TEST_STRESS)
	@echo "Running stress tests (quick mode)..."
	./$(TEST_STRESS) --quick

# Run stress tests (full)
.PHONY: stress-full
stress-full: $(TEST_STRESS)
	@echo "Running stress tests (full, 60 seconds)..."
	./$(TEST_STRESS) --duration=60

# Generate full report
.PHONY: report
report: build $(RESULTS_DIR)
	@echo "Generating benchmark report..."
	./run_all_benchmarks.sh
	@echo "Report generated in $(RESULTS_DIR)/"

# Run all tests and benchmarks
.PHONY: all-tests
all-tests: test benchmark stress
	@echo "All tests and benchmarks complete"

# Clean build artifacts
.PHONY: clean
clean:
	rm -f $(BENCH_SUITE) $(COMPARE) $(GPU_BENCH)
	rm -f $(TEST_CORRECT) $(TEST_STRESS)
	rm -f $(RESULTS_DIR)/*.json $(RESULTS_DIR)/*.md
	rm -f *.o

# Deep clean (removes results too)
.PHONY: distclean
distclean: clean
	rm -rf $(RESULTS_DIR)

# Show help
.PHONY: help
help:
	@echo "Kolibri Benchmark Suite"
	@echo ""
	@echo "Targets:"
	@echo "  make build          - Build all binaries"
	@echo "  make benchmark      - Run standard benchmark"
	@echo "  make benchmark-quick- Run quick benchmark (1KB, 1MB)"
	@echo "  make benchmark-full - Run full benchmark (up to 100MB)"
	@echo "  make benchmark-gpu  - Run GPU benchmark (macOS only)"
	@echo "  make test           - Run correctness tests"
	@echo "  make stress         - Run stress tests (quick, 10 seconds)"
	@echo "  make stress-full    - Run stress tests (full, 60 seconds)"
	@echo "  make report         - Generate full report"
	@echo "  make all-tests      - Run all tests and benchmarks"
	@echo "  make clean          - Remove build artifacts"
	@echo "  make distclean      - Remove all generated files"
	@echo "  make help           - Show this help"
	@echo ""
	@echo "Compiler: $(CC)"
	@echo "Flags: $(CFLAGS)"
