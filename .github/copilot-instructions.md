# Kolibri OS - AI Agent Instructions

## Project Overview

Kolibri OS is a lightweight experimental platform combining:
- **Advanced compression archivers** with record-breaking ratios (up to 377x)
- **AGI v2.0 development** using numerical "thinking in numbers" approach
- **GPU-accelerated knowledge base** with Metal/CUDA encoders
- **Multi-language stack**: C11 core, Python services, TypeScript/React frontend, WebAssembly bridge

**Core Philosophy**: Digital genomes, evolutionary optimization, and numerical semantic representations replace traditional NLP approaches.

## Architecture & Major Components

### 1. **C Core** (`backend/src/`, `backend/include/kolibri/`)
- **Numerical primitives**: `decimal.c` (base-256→decimal normalization), `digits.c` (digit operations)
- **Evolutionary AI**: `genome.c` (64-digit digital genomes), `formula.c` (evolutionary optimization), `semantic_digits.c` (semantic learning)
- **Context & learning**: `context_window.c` (2048-token attention), `corpus_learning.c` (pattern extraction)
- **Compression**: `compress.c` (multi-layer LZ77+RLE+CRC32)
- **Networking**: `net.c` (Kolibri protocol), `knowledge.c`, `knowledge_index.c`, `knowledge_queue.c`
- All headers in `backend/include/kolibri/` - use these for public APIs

### 2. **Archiver Systems** (`apps/`, `tools/`, `kolibri-archiver/`)
- **Multi-level archiver** (kolibri-archiver/): 377x compression via hierarchical pattern analysis + formula generation
- **v10 Smart archiver** (`tools/kolibri_archiver_v10.c`): 4-mode universal (RLE/Dictionary/Hybrid/Fallback)
- **v3/v4 archivers**: RLE Meta (57,614x on homogeneous data), Adaptive with entropy analysis
- CLI tool: `apps/kolibri_archiver.c` wraps compression APIs

### 3. **GPU Acceleration** (`engine/gpu_encoder/`)
- `kolibri_gpu_encoder.c/h`: API for CUDA/Metal embedding generation
- `gpu_encoder_metal.mm`: Metal implementation (macOS)
- `gpu_encoder_stub.c`: CPU fallback
- Demo: `tools/kgpu_demo.c` tests GPU encoder
- Python service: `backend/service/gpu_store.py` (FastAPI SQLite+vector DB)

### 4. **Frontend** (`frontend/`)
- React/TypeScript web interface
- WebAssembly bridge to C core (`backend/src/wasm_bridge.c`, `backend/src/wasm_genome_stub.c`)
- Built via `scripts/build_wasm.sh` (emscripten, <1MB budget per `docs/guides/AGENTS.md`)

### 5. **Server & Network Components**

#### C Servers (`backend/src/`, `apps/`)
- **Knowledge Server** (`backend/src/knowledge_server.c`):
  - HTTP server on port 8000 (localhost only)
  - Endpoints: `/api/knowledge/search?q=...&limit=N`, `/api/stats`
  - Auto-loads docs from `docs/` and `data/` directories
  - Online learning: records queries/answers to `.kolibri/knowledge_genome.dat`
  - HMAC authentication via `root.key` or default key
  - Built as: `kolibri_knowledge_server` executable
  - Run: `./build/kolibri_knowledge_server` or `./scripts/run_kolibri_stack.sh`

- **Distributed Node** (`apps/kolibri_node.c`):
  - P2P node with optional listener (port 4050)
  - Genome persistence to `genome.dat`
  - Bootstrap scripts, HMAC key management (inline, file, or default)
  - Auto-evolution (500ms) and sync (2s) intervals
  - Flags: `--listen PORT`, `--peer HOST:PORT`, `--genome PATH`, `--bootstrap SCRIPT`
  - Health check mode: `--health`

- **Coordinator** (`apps/kolibri_coordinator.c`):
  - Collects best formulas from multiple nodes and rebroadcasts
  - Listens on port 4099 (default)
  - Flags: `--listen PORT`, `--node HOST:PORT`, `--base-port PORT --count N`
  - Broadcasts to target nodes every cycle

- **Knowledge Relay** (`apps/kolibri_knowledge_relay.c`):
  - Replicates `TEACH`/`USER_FEEDBACK` events from knowledge genome to node genomes
  - Re-signs events with target HMAC keys
  - Tracks offset in `.kolibri/knowledge_relay.offset`
  - Flags: `--source PATH`, `--targets-dir DIR`, `--target-key FILE`, `--offset FILE`
  - Scans for `.dat` genome files in targets directory

#### CLI Tools
- `ks_compiler.c`: KolibriScript compiler/archiver
- `kolibri_sim_cli.c`: Simulation CLI
- `kolibri_queue.c`: Queue management
- `kolibri_indexer.c`: Knowledge indexing

### 6. **Python Services** (`backend/service/`, `backend/feedback_service/`)
- **LLM Proxy Service** (`backend/service/main.py`): FastAPI app proxying to upstream LLM
  - Dual response modes: `script` (WASM, default) or `llm` (proxy mode)
  - Environment: `KOLIBRI_RESPONSE_MODE`, `KOLIBRI_LLM_ENDPOINT`, `KOLIBRI_LLM_API_KEY`
  - Endpoints: `/api/health`, `/api/v1/infer`
  - Run: `./scripts/run_backend.sh --port 8000`
- **GPU Store** (`backend/service/gpu_store.py`): FastAPI router for vector knowledge base
  - SQLite + vector embeddings
  - Endpoints: `/api/gpu/store`, `/api/gpu/search`
  - Storage: `build/knowledge/kolibri.db`
- **Feedback Service** (`backend/feedback_service/main.py`): RLHF data collection
  - Endpoint: `/api/feedback` (POST)
  - Storage: SQLite via `database.py`, JSONL via `rlhf_dataset.py`
  - CORS enabled for cross-origin submissions
- **C Bindings** (`backend/python/kolibri_compress.py`): ctypes wrapper for compression library

## Critical Developer Workflows

### Build System (CMake + Ninja)
```bash
# Standard build
cmake -S . -B build -G Ninja
cmake --build build

# With GPU support (default on macOS)
cmake -S . -B build -DKOLIBRI_ENABLE_GPU=ON
cmake --build build --target kolibri_gpu_demo

# Tests
cmake -S . -B build -DKOLIBRI_ENABLE_TESTS=ON
cmake --build build
ctest --test-dir build

# Fuzzing
cmake -S . -B build-fuzz -DKOLIBRI_ENABLE_FUZZ=ON
```

### Top-Level Makefile Targets
- `make build`: Build C components
- `make test`: Run all tests (C, Python, frontend)
- `make wasm`: Build WebAssembly module (`scripts/build_wasm.sh`)
- `make frontend`: Build React frontend (requires `wasm` first)
- `make benchmark`: Run performance benchmarks
- `make ci`: Full CI pipeline

### Running Servers
```bash
# Knowledge Server (C) - port 8000
./build/kolibri_knowledge_server

# Full Stack (C server + WASM + Vite preview)
./scripts/run_kolibri_stack.sh

# LLM Proxy Service (Python/FastAPI) - port 8000
export KOLIBRI_RESPONSE_MODE=llm
export KOLIBRI_LLM_ENDPOINT="https://api.example.com/v1/infer"
./scripts/run_backend.sh --port 8000

# Distributed Network (3 nodes + coordinator)
./build/kolibri_node --listen 4050 --genome node1.dat &
./build/kolibri_node --listen 4051 --genome node2.dat --peer 127.0.0.1:4050 &
./build/kolibri_node --listen 4052 --genome node3.dat --peer 127.0.0.1:4050 &
./build/kolibri_coordinator --listen 4099 --base-port 4050 --count 3

# Knowledge Relay (replicates events to cluster)
./build/kolibri_knowledge_relay --source .kolibri/knowledge_genome.dat \
  --targets-dir build/cluster --target-key build/cluster/swarm.key
```

### Python Environment
```bash
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt  # includes pytest, ruff, pyright
pytest -q           # Run tests
ruff check .        # Lint
pyright             # Type check
```

### Testing Archivers
- `test_all_archivers.sh`: Comprehensive archiver comparison
- `test_archivers_real_data.sh`: Real-world file tests
- `test_kolibri_archiver.sh`: Single archiver test
- Benchmarks in `benchmarks/`: `make benchmark` runs full suite

### WASM Build (`scripts/build_wasm.sh`)
- Uses emscripten (auto-detects via `$EMCC` or Docker fallback)
- Outputs to `build/wasm/kolibri.wasm`
- Budget: <1MB (60KB typical), enforced in script
- Compiles: decimal layer, genome, formula, random number generation

## Project-Specific Conventions

### Code Style
- **C**: Standard C11, snake_case, minimal external deps
- **Russian comments**: Many C files use Russian comments (e.g., `корень проекта` = "project root")
- **Performance-focused**: See `decimal_fast10x.c` - prefetch hints, unrolling, cache-aware
- **Headers**: Public APIs in `backend/include/kolibri/`, implementation in `backend/src/`

### File Naming Patterns
- **Archivers**: `kolibri_archiver_v*.c` (version number in filename)
- **Tests**: `test_*.c` in `tests/`, `test_*.sh` in root
- **Tools**: CLI utilities in `apps/` and `tools/`
- **Results**: Store test outputs in `test_results/`, logs in `logs/`

### Build Artifacts
- `build/`: Main build directory
- `build-asan/`, `build-fuzz/`: Specialized builds
- `build/wasm/`: WebAssembly output
- `build/knowledge/`: GPU store DB, spectral profiles

### Integration Points
1. **C ↔ Python**: ctypes bindings via `backend/python/kolibri_compress.py`
2. **C ↔ WASM**: Bridge via `backend/src/wasm_bridge.c`
3. **Frontend ↔ Backend**: 
   - **Default**: WASM direct call (deterministic KolibriScript)
   - **LLM mode**: REST API via `backend/service/main.py` → upstream LLM
   - **Knowledge search**: HTTP to `kolibri_knowledge_server` (port 8000)
4. **GPU Pipeline**: `scripts/knowledge_pipeline.sh` → `spectral_fingerprint.py` → `gpu_store.py`
5. **Distributed Network**:
   - **Node ↔ Node**: P2P protocol on port 4050 (Kolibri network protocol)
   - **Coordinator ↔ Nodes**: Formula collection and broadcast (port 4099)
   - **Relay ↔ Genomes**: Event replication between knowledge and node genomes

### Policy Enforcement (`docs/guides/AGENTS.md`)
```kolibri-policy
build: ours         # Prefer existing build logic on conflicts
code: ours          # Trust codebase over external suggestions
budgets:
  wasm_max_kb: 61440 (60MB)
  step_latency_ms: 250
  coverage_min_lines: 75
  coverage_min_branches: 60
```
Validate with: `python scripts/policy_validate.py`

## Important Non-Obvious Details

### Server Architecture Pattern
- **Knowledge Server** runs standalone C binary (single-threaded, blocking accept)
- **Python services** run via uvicorn (async FastAPI)
- **No shared state** between C and Python servers - communicate via HTTP if needed
- **Port allocation**: Knowledge Server (8000), Coordinator (4099), Nodes (4050+)
- **HMAC keys**: Three sources - file (`root.key`), inline flag, or hardcoded default

### Genome-Based Event Logging
- All servers log to "genome" files (`.kolibri/knowledge_genome.dat`, `genome.dat`)
- Event types: `BOOT`, `ASK`, `TEACH`, `USER_FEEDBACK`, `SWARM_SEND`
- HMAC-signed for integrity, persisted to disk
- `kolibri_knowledge_relay` replicates events between genomes

### OpenSSL Detection (CMakeLists.txt)
- Custom OpenSSL@3 detection for Homebrew paths (`/opt/homebrew/opt/openssl@3`)
- Falls back to system OpenSSL if not found
- Required for `kolibri_core` crypto operations

### Multi-level Archiver Formula
- Lives in `kolibri-archiver/` (separate from `tools/` archivers)
- Two modes: with associations (3.34x, fast) vs pure formula (377x, slow restore)
- Uses 5-level hierarchical compression pipeline

### AGI v2.0 Modules (Q1 2026)
- **Semantic module**: 64-digit numerical word representations, evolutionary learning
- **Context window**: 2048 tokens, attention mechanism, softmax normalization
- **Corpus learning**: Incremental pattern merging, persistent binary format
- Tests: `test_semantic`, `test_context`, `test_corpus` (built when `KOLIBRI_ENABLE_TESTS=ON`)

### Response Modes (README.md)
- **Deterministic KolibriScript** (default): WASM-based browser execution
- **LLM Proxy**: Set `KOLIBRI_RESPONSE_MODE=llm`, requires FastAPI service
- Frontend degrades gracefully: LLM → KolibriScript fallback on error

### GPU Encoder Build
- Enabled by default with `KOLIBRI_ENABLE_GPU=ON`
- Metal framework linked on macOS (`find_library(METAL_FRAMEWORK Metal)`)
- Stub fallback if Metal unavailable
- Demo: `./build/kolibri_gpu_demo README.md` tests encoding

## Key Files for Understanding Patterns

- **Compression**: `backend/include/kolibri/compress.h`, `backend/src/compress.c`
- **Genome/Formula**: `backend/include/kolibri/genome.h`, `backend/src/genome.c`, `backend/src/formula.c`
- **Archiver CLI**: `apps/kolibri_archiver.c` (v40 reference implementation)
- **WASM bridge**: `backend/src/wasm_bridge.c` (exports for JS)
- **Build automation**: `scripts/build_wasm.sh` (Russian variable names, Docker fallback logic)
- **GPU pipeline**: `docs/analysis/kolibri_ai_masterplan.md` (full production roadmap)

## Common Gotchas

1. **Always build WASM before frontend**: `make wasm` → `make frontend`
2. **Test target order**: Build tests with CMake, not standalone gcc
3. **Archiver versions**: v3 ≠ v4 ≠ v10 ≠ multi-level (different algorithms, incompatible formats)
4. **Russian paths**: Scripts use Cyrillic variable names (`проект_корень`, `выход_дир`)
5. **SQLite required**: CMake fails without SQLite3 dev package
6. **Coverage budgets**: Maintain 75% line, 60% branch coverage (enforced in CI)
7. **Port conflicts**: Knowledge server and LLM proxy both default to 8000 - can't run simultaneously
8. **HMAC keys**: Nodes need matching keys to communicate - use `--target-key` for relay
9. **Genome initialization**: `.kolibri/` directory must exist, created automatically by servers
10. **Knowledge server data**: Auto-loads docs from `docs/` and `data/` - place content there before starting
