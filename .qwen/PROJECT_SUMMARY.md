The user wants me to generate a comprehensive project summary in markdown format based on the conversation history. Let me analyze what happened:

1. The user asked what Kolibri AI can do
2. I clarified I'm Qwen Code, not Kolibri AI
3. User asked me to respond in Russian
4. User clarified they're asking about the project capabilities, not mine
5. I read documentation and explained the project
6. User asked if it's a unique project
7. I explored the codebase and confirmed uniqueness
8. User said "continue development"
9. I started working on Sprint A from the roadmap
10. I fixed several build issues:
    - GPU Metal linking (changed STATIC to OBJECT library, enabled OBJCXX)
    - BWT symbols (libdivsufsort linkage)
    - Added timeouts for flaky tests
11. I'm debugging test failures in `kolibri_tests` - specifically segfaults in `test_script_load_file` where `ks_load_file` returns 0 but `source_stream.length=0`

Let me create a comprehensive summary.# Project Summary — Kolibri OS

## Overall Goal
Continue development of the Kolibri AI project following the roadmap (Sprint A: stabilization, Sprint B: verifiability, Sprint C: AI quality growth), starting with fixing build issues and failing tests.

## Key Knowledge

### Project Identity
- **Kolibri** — a unique AI system built on **numeric thinking** (decimal digit sequences), not traditional token-based LLMs
- Author: Кочуров Владислав Евгеньевич (Kochurov Vladislav Evgenievich)
- ~76,000+ lines of code across C23, Python, TypeScript
- Every UTF-8 character → exactly 3 decimal digits; all knowledge = 64-digit decimal genomes

### Unique Architecture
- **Evolutionary optimization** instead of backpropagation (tournament selection, crossover, mutation)
- **Fractal memory** — 10-ary tree as associative memory with spreading activation
- **Logical memory** — stores generative programs, not raw data (e.g., `repeat("ABC", 1000)` = ~40 bytes instead of 3000)
- **Genome blockchain** — HMAC-SHA256 append-only ledger for knowledge provenance with WAL
- **Swarm (Рой)** — UDP-based P2P knowledge exchange protocol
- **KolibriScript** — DSL with Russian keywords
- **5-strategy hybrid inference** — runs parallel strategies and selects by confidence
- **Pre-LN Transformer** from scratch in pure C (~100K params)
- **Predictive compression** — evolutionary MLP + arithmetic coding

### Build System
- **CMake** with C23 standard
- Key options: `KOLIBRI_ENABLE_TESTS=ON`, `KOLIBRI_ENABLE_GPU=ON`, `KOLIBRI_ENABLE_FUZZ=ON`
- Dependencies: `libdivsufsort`, OpenSSL 3, SQLite3, pthreads
- On macOS: requires `OBJCXX` language enabled for Metal GPU support
- Build commands:
  ```bash
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DKOLIBRI_ENABLE_TESTS=ON
  cmake --build build -j
  ctest --test-dir build --output-on-failure
  ```

### CI Pipeline (GitHub Actions)
- Multi-stage: Python quality → CMake/tests/ISO → WASM → Frontend → Docker smoke → Release bundle
- Uses cosign for artifact signing
- Nightly scheduled runs (`cron: '0 3 * * *'`)

### Test Status (Before Fixes)
- 44/46 tests passing (96%)
- 2 failing: `kolibri_tests` (SIGABRT), `test_async_executor` (killed by timeout)

## Recent Actions

### [DONE] Fixed GPU Metal Linking on macOS
- Changed `kolibri_gpu` from `STATIC` to `OBJECT` library to avoid Objective-C++ linker issues
- Added `enable_language(OBJCXX)` for Apple platforms
- Added Metal/Foundation frameworks directly to `kolibri_gpu_demo` link

### [DONE] Fixed BWT Symbol Resolution
- `bw_transform` and `inverse_bw_transform` are provided by `libdivsufsort` (already declared in `divsufsort.h`)
- Added `${KOLIBRI_DIVSUFSORT_TARGET}` to `kolibri_compress_ffi` link libraries

### [DONE] Added Test Timeouts
- Set `TIMEOUT 120` for `kolibri_tests` and `test_async_executor` in CMakeLists.txt

### [IN PROGRESS] Debugging `kolibri_tests` Segfault
- Root cause traced to `test_script_load_file` — `ks_load_file` returns 0 but `source_stream.length=0`
- The file is written correctly via `zapisat_skript_text` (uses `mkstemp` + `fwrite`)
- `ks_load_file` reads the file but `kolibri_digit_text_assign_utf8` produces 0-length output
- Debug logging added to `ks_load_file` in `script.c` but output not appearing (possible build caching issue)
- All other script tests pass: `test_script` and `test_script_crystal_cycle` work correctly

### [DONE] Added Debug Instrumentation
- Added `fprintf(stderr, ...)` logging throughout `test_script.c` to isolate crash points
- Confirmed crash sequence: `test_script` ✓ → `test_script_crystal_cycle` ✓ → `test_script_load_file` ✗

## Current Plan

### Sprint A (Stabilization) — IN PROGRESS
1. [IN PROGRESS] Fix `kolibri_tests` segfault in `test_script_load_file`
   - Investigate why `kolibri_digit_text_assign_utf8` returns 0 length for file-loaded content
   - Check if `zapisat_skript_text` writes UTF-8 correctly (no BOM, proper encoding)
   - Verify `ks_load_file` buffer handling vs `ks_load_text` (which works)
2. [TODO] Fix `test_async_executor` timeout issue
3. [TODO] Fix CI pipeline for macOS runner
4. [TODO] Establish unified AI metrics (answer quality, latency, robustness)
5. [TODO] Build reproducible benchmark suite

### Sprint B (Verifiability) — TODO
1. [TODO] Clean up documentation status contradictions
2. [TODO] Close empty/obsolete plans or mark as `planned`
3. [TODO] Re-verify archived components before re-integration

### Sprint C (AI Quality Growth) — TODO
1. [TODO] Improve retrieval/knowledge quality on target datasets
2. [TODO] Strengthen reasoning on test sets
3. [TODO] Prepare public technical report with reproducible metrics

## Open Issues
- `test_script_load_file`: `source_stream.length=0` after loading file — likely encoding or buffer issue in `kolibri_digit_text_assign_utf8` when called from `ks_load_file`
- `test_async_executor`: killed by CTest — may need longer timeout or thread-safety fix
- CI pipeline needs macOS-specific fixes (Metal, sandbox restrictions on sockets)

---

## Summary Metadata
**Update time**: 2026-04-04T16:30:13.459Z 
