# Kolibri OS - AI Agent Instructions

## 🧠 Core Architecture
Kolibri OS is a hybrid **C23 / Python / WASM** platform focusing on evolutionary compression ("Number-Thinking") and AGI.

- **Core Library (C23)**: `backend/src/`. Implements the "ReasonBlock" genome logic, math kernels, and memory management.
- **Public API (C23)**: `backend/include/kolibri/`. **ONLY** import headers from here when writing apps or bindings.
- **Executables (C23)**: `apps/`. Standalone applications like `kolibri_node.c` (AI Node) and `kolibri_archiver.c`. Link against the core library.
- **Orchestration (Python)**: `backend/service/` (FastAPI services) & `scripts/`. Type-hinted glue code for training and swarming.
- **Frontend (React)**: `frontend/`. A Vite + TypeScript application that bridges to C via WebAssembly (`kolibri.wasm`).
- **Archiver Research**: `kolibri-archiver/`. Specialized high-performance compression codebase with its own `Makefile` and `src/`.
- **Content Factory**: `content_factory_mvp/`. Microservice architecture for content generation (Python/Docker).

## 🛠️ Critical Workflows
**Always run from root: `/workspaces/kolibri-project`**

### Build
- **Native (C)**: `cmake -S . -B build -G Ninja && cmake --build build`
- **WASM (Web)**: `./scripts/build_wasm.sh` (Target: `build/wasm/kolibri.wasm`).
- **Frontend**: `make frontend` (Builds WASM first, then installs/builds frontend assets).
- **Archiver Standalone**: `cd kolibri-archiver && make`

### Test & Quality
- **Full Suite**: `make test` (Runs CTest, Pytest, Ruff, Pyright, Vitest).
- **C Tests**: `ctest --test-dir build --output-on-failure`.
- **Python Lint**: `ruff check .` and `pyright`.
- **Benchmark**: `make benchmark` (Required for compression algorithms usage verification).
- **Policy**: Prefer project-specific implementations (`backend/**`, `frontend/**`) over generic external patterns.

### Run & Interaction
- **AI Node**: `./build/kolibri_node --genome build/training/auto_genome.dat`
- **AI Training**: `./scripts/auto_train.sh --ticks 500`
- **Archiver via CLI**: `./build/kolibri_archiver compress <input> <output>`

## ⚠️ Conventions & Rules

### 1. C Programming (Strict C23)
- **Russian Comments**: **MANDATORY** for high-level logic and headers (e.g., `/* --- Утилиты --- */`). Do not translate existing Russian comments.
- **Style**: Strict `snake_case`. Standard C23 (use `auto`, `constexpr`, `nullptr` where helpful).
- **Memory**: Pure manual management (`malloc`/`free`). No smart pointers.
- **Dependencies**: Restricted. Uses `openssl` (HMAC/SHA) but prefer zero-dependency for core logic.
- **Context**: Most functions require a `Kolibri*` context struct (e.g., `KolibriFormulaPool *pool`).

### 2. Python (3.10+)
- **Type Hints**: **Strictly Required**. Use `from __future__ import annotations`.
- **Linting**: Must pass `ruff` and `pyright`.
- **Structure**: FastAPI services in `backend/service/` use Pydantic models.

### 3. WebAssembly (WASM)
- **Constraint**: Binary size must remain under **60MB** (61440 KB).
- **Determinism**: WASM output must exactly match Native C builds.

### 4. Domain Formats
- **KolibriScript (.ks)**: Domain-specific language for AI instructions.
- **Genomes**: 64-digit numeric semantic strings representing AI state.

## 📂 Key Paths
- `backend/src/{genome.c, formula.c}`: Core "Number-Thinking" engine logic.
- `backend/include/kolibri/*.h`: Public headers.
- `apps/kolibri_node.c`: Main entry point for the AI Node.
- `kolibri-archiver/`: Separate high-performance compression research codebase.
- `frontend/src/core/kolibri-bridge.ts`: WASM storage/bridge implementation.
- `content_factory_mvp/`: Content generation microservices.
