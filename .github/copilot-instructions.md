# Kolibri OS — AI Agent Instructions

## Architecture

Hybrid **C23 / Python / WASM** platform: evolutionary compression ("Number-Thinking") and AGI.

| Layer | Path | Tech | Notes |
|-------|------|------|-------|
| Core library | `backend/src/` (~35 modules) | C23 | Genome, formulas, knowledge graph, compression |
| Public headers | `backend/include/kolibri/*.h` | C23 | **Only** import from here for apps/bindings |
| Executables | `apps/` | C23 | `kolibri_node.c`, `kolibri_archiver.c`, etc. → link `kolibri_core` |
| FFI shared libs | `build/libkolibri_evolve.so`, `libkolibri_kpc.so` | C23 | Loaded by Python via `ctypes` |
| Backend API | `backend/service/` | Python 3.10+ / FastAPI | Modular routers, Pydantic models |
| Frontend | `frontend/` | React 18 / TypeScript / Vite | WASM bridge with LLM/fallback strategy |
| Archiver research | `kolibri-archiver/` | C11, own `Makefile` | Zero-dependency, single-file compilation |
| Content factory | `content_factory_mvp/` | Python / Docker Compose | PostgreSQL + Redis + Celery workers |

### Data flow

Text → DJB2 tokenisation → 64-digit numeric patterns → weighted knowledge graph → 4000-digit genome (500-layer formula ResNet) → evolutionary training (genetic algorithm: mutation, crossover, selection).

## Build & Run (always from repo root)

```bash
cmake -S . -B build -G Ninja && cmake --build build   # Native C
./scripts/build_wasm.sh                                 # WASM → build/wasm/kolibri.wasm
make frontend                                           # WASM + npm install + Vite build
cd kolibri-archiver && make                             # Standalone archiver (C11)
```

## Test & Quality

```bash
make test              # CTest + pytest + ruff + pyright + Vitest (all-in-one)
ctest --test-dir build --output-on-failure   # C tests only
ruff check . && pyright                       # Python lint
make benchmark                                # Compression benchmarks
```

- C tests use a **custom minimal framework** — `assert()` + `printf`, no external deps. Runner: `tests/test_main.c` calls `void test_<module>(void)` functions.
- Python tests use **pytest**. Classes `TestXxx`, methods `test_<behaviour>(self) -> None`, use `setup_method` for init.
- WASM size budget: **< 60 MB** (61440 KB). Verified in CI via `scripts/policy_validate.py`.

## C Conventions (Strict C23)

### Naming
- Functions: `snake_case` with **module prefix** — `kg_` (genome), `kf_` (formula), `kolibri_knowledge_` (knowledge), `mf_` (meta-formula).
- Structs/typedefs: `KolibriXxx` (PascalCase) — `KolibriGenome`, `KolibriFormulaPool`, `KolibriAssociation`.
- Constants/enums: `KOLIBRI_XXX` — `KOLIBRI_HASH_SIZE`, `KOLIBRI_DOMAIN_GENERAL`.
- Files: `backend/src/<module>.c` ↔ `backend/include/kolibri/<module>.h`.

### Patterns
- **Russian comments MANDATORY** for section headers and high-level logic: `/* --- Утилиты --- */`, `/* Домены знаний */`. Never translate existing Russian.
- **Parameter order**: context/object first, inputs next, output buffers last. Example: `kg_open(KolibriGenome *ctx, const char *path, const unsigned char *key, size_t key_len)`.
- **Error handling**: `return -1` on error, `return 0` on success, `NULL` for pointer failures. Guard every function entry: `if (!ctx || !path) return -1;`.
- **Memory**: `malloc`/`free`/`realloc` only. `memset` for init. No allocators or pools.
- **Section separators** in source files:
  ```c
  /* ============================================================================
   * WAL (Write-Ahead Logging) реализация
   * ============================================================================ */
  ```
- **Copyright**: `/* Copyright (c) 2025 Кочуров Владислав Евгеньевич */` at file top.
- **Dependencies**: OpenSSL (HMAC/SHA), SQLite3, pthreads, `libm`. Prefer zero-dependency for core logic.

### FFI shared libraries (`evolve_ffi.c`, `predictive_compress.c`)
- Mark exports: `__attribute__((visibility("default")))`.
- Constants **must mirror** Python counterparts exactly (e.g., `FFI_GENE_SIZE 4000` ↔ `GENE_SIZE = 4000` in `number_mind.py`).
- Include standalone compilation command in file header comment.

## Python Conventions (3.10+)

- **Always** start files with `from __future__ import annotations`.
- Type hints on **everything** — params, returns, class attributes. Tests use `-> None`.
- FastAPI services: modular `APIRouter(prefix="/api/v1/<domain>", tags=[...])` included in `main.py`. Each endpoint has a `Request`/`Response` Pydantic `BaseModel` pair with `Field(...)` validators.
- C FFI pattern (`backend/service/c_evolve.py`): auto-compile `.so` if missing → `ctypes.CDLL` → set `argtypes`/`restype` → marshal numpy arrays to flat C arrays → graceful fallback to pure Python if C unavailable.
- Linting: must pass `ruff check .` and `pyright` (strict).

## Frontend Conventions (TypeScript)

- WASM bridge (`frontend/src/core/kolibri-bridge.ts`) uses **Strategy pattern**: `KolibriScriptBridge` (WASM) → `KolibriLLMBridge` (API proxy) → `KolibriFallbackBridge` (error message). Factory `createBridge()` picks based on `VITE_KOLIBRI_RESPONSE_MODE`.
- KolibriScript programs built in TypeScript use Russian keywords: `начало:`, `переменная`, `обучить связь`, `конец.`.
- WASM exports: `_malloc`, `_free`, `_kolibri_bridge_init`, `_kolibri_bridge_execute`, `_kolibri_bridge_reset`.
- Includes a full **WasiAdapter** (minimal WASI shim) for browser runtime.

## Key Paths

| Purpose | Path |
|---------|------|
| Core engine | `backend/src/{genome.c, formula.c, knowledge.c, compress.c, inference.c}` |
| Public API headers | `backend/include/kolibri/*.h` |
| FFI bridge | `backend/src/evolve_ffi.c` (C) ↔ `backend/service/c_evolve.py` (Python) |
| AI Node entry | `apps/kolibri_node.c` |
| FastAPI main | `backend/service/main.py` (routers: `ai_chat`, `agent`, `swarm_sync`, etc.) |
| WASM bridge | `frontend/src/core/kolibri-bridge.ts` |
| Test runner (C) | `tests/test_main.c` + `tests/test_<module>.c` |
| CI policy | `scripts/policy_validate.py` (WASM size, coverage thresholds) |
| Archiver research | `kolibri-archiver/` (separate C11 build, own Makefile) |
| Content factory | `content_factory_mvp/` (Docker Compose microservices) |
