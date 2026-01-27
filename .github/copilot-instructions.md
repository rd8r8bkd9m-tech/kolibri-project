# Kolibri OS - AI Agent Instructions

## 🧠 Project Context
Kolibri OS is an **experimental evolutionary platform** focusing on:
1.  **Extreme Compression**: Multi-stage archivers (LZ77/RLE/Formula) achieving ultra-high ratios for AGI knowledge.
2.  **AGI v2.0**: "Thinking in numbers" - 64-digit semantic genomes (`genome.c`), not NLP tokens.
3.  **Hybrid Stack**: C11 Core (efficiency) + Python (orchestration) + WASM (web bridge).

## 🗺️ Codebase Map
- **Core (C11)**: `backend/src/` (Logic) & `backend/include/kolibri/` (Public Headers).
  - **AI**: `genome.c`, `formula.c`, `semantic_digits.c`.
  - **Compression**: `compress.c`, `decimal_fast10x.c` (Performance critical).
  - **Systems**: `net.c` (P2P), `knowledge.c` (DB), `context_window.c`.
- **Archivers**: `tools/` (Evolutionary tools v3-v40) & `apps/kolibri_archiver.c` (CLI).
- **Servers**: 
  - `apps/kolibri_node.c`: Distributed P2P node (Port 4050+).
  - `backend/src/knowledge_server.c`: C Knowledge Engine (Port 8000).
  - `backend/service/main.py`: Python LLM Proxy (Port 8000, mutally exclusive).
- **Frontend**: `frontend/` (React/Vite/TS) bridging to C via compiled WASM.

## 🛠️ Critical Workflows
**Run all commands from root (`/workspaces/kolibri-project`).**

### Build
- **Standard (C)**: `cmake -S . -B build -G Ninja && cmake --build build`
- **WASM (Required for Web)**: `./scripts/build_wasm.sh` (Output: `build/wasm/kolibri.wasm`)
- **Frontend**: `make frontend` (Requires WASM first).

### Test
- **Full Suite**: `make test` (Runs C tests via ctest + Python via pytest + Lint via ruff).
- **Quick C Test**: `ctest --test-dir build`
- **Archiver Validation**: `make benchmark` or `./test_all_archivers.sh` (Warning: Takes time).

### Run
- **Stack**: `./scripts/run_kolibri_stack.sh` (Recommended).
- **Dev Servers**: `python -m uvicorn backend.service.main:app --reload` (Python API).

## ⚠️ Conventions & Rules
1.  **Languages**: 
    - **C**: C11, `snake_case`. **Russian comments are standard/expected** (do not remove).
    - **Python**: 3.10+, strictly type-hinted.
2.  **Architecture**:
    - **No Shared State**: C/Python communicate via HTTP or signed `.dat` files (`.kolibri/` dir).
    - **Determinism**: WASM execution must exactly match Native C.
    - **Keys**: HMAC keys required for node communication (`root.key` or defaults).
3.  **Dependencies**: Use `backend/include/kolibri/` headers only. No external C libs (except OpenSSL via CMake).
4.  **Policy**: Prefer existing patterns over external suggestions. Maintain <60MB WASM budget.
