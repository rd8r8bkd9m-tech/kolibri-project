# Kolibri Repository Architecture Analysis

Date: 2026-04-10

## 1. Executive Summary

Kolibri is not a single-application repository. It is a multi-contour monorepo that currently contains:

- one active AI product contour centered on `frontend/src`, `backend/service`, `backend/src`, `backend/include/kolibri`, `apps/`, CMake/CI, and the WASM delivery path;
- several adjacent or parallel product contours, most notably license-management UI/mobile code in `frontend/kolibri-web` and `mobile/kolibri-app`;
- multiple experimental or operational contours such as `cloud-storage`, `content_factory_mvp*`, `swarm`, `sdk/python`, `kernel`, and `web-app`;
- large checked-in artifacts and datasets that materially affect repository weight and local reproducibility.

The main architectural truth is consistent across root docs and the codebase: the intended system is `frontend/PWA -> FastAPI gateway -> C core -> knowledge/provenance/swarm -> WASM/offline`. The main repository problem is not lack of ambition or lack of code. It is contour sprawl: the repo mixes active runtime, experiments, archived UX lines, generated assets, huge data payloads, and parallel products in one tree with uneven quality boundaries.

The single biggest architectural tension is the gateway split:

- the current production-facing surface is still largely FastAPI/Python;
- the repository also carries a large standalone C HTTP runtime with overlapping endpoints;
- documentation explicitly describes this as a transition, but local build/test reality does not currently prove the transition end to end.

## 2. Repository Map

### 2.1 Primary Contours

| Path | Role | Status | Notes |
| --- | --- | --- | --- |
| `backend/service` | FastAPI gateway and Python orchestration layer | Active | Main HTTP surface for chat, auth, learning, swarm runtime, account/profile, quality endpoints |
| `backend/src` | C core and native runtime modules | Active | Formula, inference, script runtime, HTTP server, memory, compression, swarm-related logic |
| `backend/include/kolibri` | Public C headers | Active | Stable and experimental headers live side by side |
| `frontend/src` | Active React/Vite AI chat PWA | Active | Current UI shell, streaming client, workspace, settings, WASM bridge |
| `apps/` | Native CLIs and operational binaries | Active | Mix of public CLIs and internal tools |
| `core/` | Stable Python simulation/testing API | Active but secondary | Referenced by public interface docs and Python tests |
| `.github/workflows` | CI source of truth | Active | Defines expected build, test, wasm, docker, and release smoke flows |

### 2.2 Secondary / Parallel Contours

| Path | Role | Integration Level | Notes |
| --- | --- | --- | --- |
| `frontend/kolibri-web` | Separate SPA/PWA for license management | Parallel product | Different domain from AI chat; its own stack, routes, docs, and dependencies |
| `mobile/kolibri-app` | React Native / Expo license-management client | Parallel product | Mirrors license-management domain, not AI chat |
| `cloud-storage` | Standalone Express-based storage API | Low | Demo/test service with separate auth/storage model |
| `content_factory_mvp` | Containerized MVP | Low | Separate app/backend/worker contour |
| `content_factory_mvp2` | Second containerized MVP | Low | Parallel prototype branch of the same idea |
| `swarm` | Swarm shell scripts and configs | Medium | Operational wrapper around the swarm story, but not the only swarm implementation |
| `sdk/python` | Packaging scaffold for Python SDK | Low | Separate packaging contour, limited footprint |
| `kernel` | OS/kernel contour | Experimental | Bare-metal style contour, conceptually related but operationally separate |
| `web-app` | Legacy web experiment | Legacy | Single-file JS application, not aligned with active frontend |

### 2.3 Artifacts and Noise Sources

These are not source contours, but they materially affect repository ergonomics and local analysis:

- `data`: 2.5G
- `frontend/node_modules`: 278M
- `test_results`: 55M
- `knowledge`: 12M
- `build*`: multiple local build trees totaling tens of MB
- `frontend/dist`: built artifact checked in
- `archived`, `release`, `__pycache__`, `.playwright-cli`

These directories make the repository feel larger and more coupled than the active runtime actually is.

## 3. System Architecture

### 3.1 Canonical Product Story

The most accurate high-level system story is:

```text
frontend/src (React PWA)
  -> backend/service (FastAPI compatibility/runtime gateway)
    -> backend/src + backend/include/kolibri (C core)
      -> data/ + knowledge/ + swarm live memory / provenance
        -> optional distribution modes: WASM, swarm, CLI, benchmarks
```

This matches `README.md` and broadly matches `docs/PUBLIC_ARCHITECTURE.md`.

### 3.2 Active Runtime Contour

#### Frontend

`frontend/src` is the active AI UI. Current structure is feature-oriented:

- `App.tsx` bootstraps theme and viewport handling and lazy-loads `AppShellV3`
- `api.ts` is the central network client and request shaping layer
- `lib/kolibriBridge.ts` is the WASM bridge and local execution path
- `providers`, `store`, `features/*`, `components/*` implement the chat shell, workspace, settings, live queue, and streaming UX
- `public/` carries `kolibri.wasm`, loader JS, service worker, and PWA assets

The UI can operate in different response modes via `VITE_KOLIBRI_RESPONSE_MODE`, with an explicit WASM/local path and backend path.

#### Backend Gateway

`backend/service/main.py` is the integration hub:

- always-on routers: AI chat, auth, account, health, swarm runtime, streaming chat, continuous learning control
- conditionally enabled routers when chat-only mode is off: GPU store, OS bridge, crawler, agent, swarm sync, distributed crawler, delta sync, archiver, cognition API
- lifecycle hooks can autostart engine preloading, swarm runtime, background learning, and continuous learning

This is not a thin wrapper. It is a large orchestration gateway that combines product APIs, runtime toggles, and background services.

#### C Core and Native Runtime

`backend/src` plus `backend/include/kolibri` contains the native intelligence/runtime substrate:

- formula and inference engines
- script runtime
- memory layers: logical, fractal, knowledge, context
- compression and indexing modules
- swarm, learning, explanation, reasoning, verification helpers
- native HTTP runtime in `backend/src/kolibri_http_server.c`
- WASM bridge in `backend/src/wasm_bridge.c`

The C layer is both a library and an alternative runtime surface.

### 3.3 Secondary Product Lines

The repository also contains a separate license-management line:

- `frontend/kolibri-web` is a React SPA/PWA for licenses/payments/profile/settings
- `mobile/kolibri-app` is a React Native/Expo client for the same business domain
- `cloud-storage` is a storage service aligned more with files/licenses/demo tooling than with the AI chat path

These are not simple platform shells around the active AI frontend. They represent a parallel domain and broaden the repo beyond the AI system described in root docs.

## 4. Runtime Surface and Entry Points

### 4.1 FastAPI Surface

#### Core application entry

- `backend/service/main.py`

#### Main router families present in the running app

- AI chat surface under `/api/v1/ai/*`
- auth under `/api/v1/auth/*`
- account/profile/preferences under `/api/v1/account/*`
- health under `/api/v1/health/*`, plus `/api/health`
- swarm runtime under `/api/v1/swarm/*`
- continuous learning under `/api/v1/learning/*`
- streaming chat under `/api/v1/ai/chat/stream`
- optional subsystems when chat-only mode is disabled:
  - GPU store
  - crawler and model endpoints
  - autonomous agent endpoints
  - distributed crawler
  - delta sync
  - cognition endpoints
  - OS bridge and terminal/dev APIs

#### AI chat router highlights

`backend/service/ai_chat.py` is a very broad router, not a single chat endpoint. It includes:

- `/chat`, `/chat/stream`
- `/demo/learn/text`
- `/imagine`, `/vision/analyze`
- `/train`, `/pattern`, `/embedding`
- `/models`, `/stats`
- quality benchmark and uniqueness proof endpoints
- conversation CRUD
- knowledge query and analytics

This router is a large product/API aggregation point and one of the main architectural hotspots.

### 4.2 C Runtime Surface

#### Native HTTP runtime entry

- `backend/src/kolibri_http_server.c`

#### Observed route families in the C runtime

- `/api/v1/health`
- `/api/v1/ai/chat`
- `/api/v1/ai/chat/stream`
- `/api/v1/ai/verify`
- `/api/v1/ai/explain`
- `/api/v1/ai/reason`
- `/api/v1/ai/math/solve`
- `/api/v1/ai/tokenize`
- `/api/v1/ai/models`
- `/api/v1/ai/domain/stats`
- `/api/v1/world_model/*`
- `/api/v1/corpus/*`
- `/api/v1/formula/status`
- `/api/v1/fractal/*`
- `/api/v1/autolearn/*`
- `/api/v1/system/status`
- `/api/v1/auth*`, `/api/v1/account*`, `/api/v1/swarm*`, `/api/v1/learning*`

The C runtime covers overlapping API territory with FastAPI, but it is not obviously the single canonical runtime yet. It is a parallel runtime surface under active transition.

### 4.3 Public C and Python Interfaces

#### Public C headers present

The include tree contains 57 headers, including:

- stable/documented core headers such as `script.h`, `knowledge.h`, `net.h`, `genome.h`, `formula.h`, `decimal.h`, `digits.h`, `random.h`
- many additional headers for attention, audio, vision, auto-learn, swarm, reasoning, world-model, and other subsystems

`docs/public_interfaces.md` intentionally documents only a subset as stable, which is a reasonable boundary, but implementers can easily mistake the larger include tree for a fully stable public surface.

#### Public Python surface

- `core/kolibri_sim.py`
- `core/kolibri_script/genome.py`
- `core/tracing.py`

This is a real secondary public API used for tests and simulation, and it sits outside the main backend package.

### 4.4 CLI and Operational Entry Points

The `apps/` directory is a sizable product/tooling surface, including:

- `kolibri_infer_cli.c`
- `kolibri_formula_trainer.c`
- `kolibri_node.c`
- `kolibri_indexer.c`
- `kolibri_queue.c`
- `kolibri_sim_cli.c`
- `kolibri_mass_trainer.c`
- `kolibri_archiver.c`
- multiple additional operational or experimental binaries

This matters architecturally because Kolibri is not only a web system. It also exposes CLI and offline/operator execution paths.

### 4.5 Root Orchestration

- `Makefile` defines build/test/wasm/frontend/iso/ci flows
- `scripts/run_all.sh` orchestrates CMake build, CTest inventory check, C tests, benchmark, frontend smoke/build, WASM build, ISO build, and optional cluster run

This is the intended developer/release path, but local reproducibility currently falls short of what those scripts assume.

## 5. Documentation vs Code Reality

### 5.1 Documents That Are Mostly Aligned

#### `README.md`

Root README is directionally accurate:

- names `backend/service`, `backend/src`, `frontend` as active
- positions the repo as one product contour
- points to `docs/plans/ROADMAP_SINGLE_SOURCE_OF_TRUTH.md`

It is intentionally selective and does not attempt to catalog every side contour.

#### `docs/PUBLIC_ARCHITECTURE.md`

This is the closest high-level description to the real active system:

- frontend/PWA
- FastAPI compatibility gateway
- C core
- knowledge/provenance
- distribution modes including swarm and WASM

Its key claim that the gateway is "in transition" is consistent with the codebase.

#### `docs/public_interfaces.md`

This document does a useful job of defining a narrower stable surface than the implementation tree exposes. That is good architecture hygiene, provided consumers actually follow the doc instead of browsing headers directly.

### 5.2 Confirmed Drift

#### `docs/plans/ROADMAP_SINGLE_SOURCE_OF_TRUTH.md`

The roadmap contains incorrect "not found in code" claims for:

- `backend/src/vision.c` - exists
- `backend/src/audio.c` - exists
- `engine/gpu_encoder/gpu_encoder_cuda.cu` - exists

It is correct that these are not automatically equivalent to production readiness, but the document currently mixes "not production-proven" with "not present in repo".

#### `frontend/README.md`

This document is significantly stale relative to `frontend/src`:

- describes a `manus/` structure that is not the current active source layout
- refers to `core/kolibri-bridge.ts`, while the live code uses `lib/kolibriBridge.ts`
- describes a different component tree than the current `AppShellV3` / feature-based layout
- describes Vitest testing, while the current package test is a Node-based smoke contract script

Architecturally, this is one of the clearest documentation drifts in the repo.

#### `backend/service/README.md`

This file understates the backend:

- describes 14 Python modules, while the actual package contains many more service modules and optional routers
- presents a smaller, earlier view of the backend surface

#### `requirements.txt`

The file header says it is a placeholder manifest and suggests no runtime packages are required, but the file now contains actual tool/runtime dependencies and duplicate entries. This is not catastrophic, but it is no longer an honest placeholder.

### 5.3 Narrative Misalignment in Secondary Contours

- `frontend/kolibri-web/README.md` documents a separate license-management product line
- `mobile/README.md` declares "production ready" for another license-management contour
- `cloud-storage/README.md` also declares "production ready" for a standalone demo service

Taken together, the repo contains several "production-ready" narratives for different products. That makes the overall product boundary ambiguous.

## 6. Build and Test Reality Check

### 6.1 What CI Claims to Validate

`.github/workflows/ci.yml` defines a substantial pipeline:

- Python lint and tests with `ruff`, `pyright`, `pytest`, `coverage`
- CMake build and `ctest`
- phase-1 C runtime benchmark
- ISO build and signing
- WASM build and signing
- frontend install/build/smoke contracts
- Docker smoke for backend/indexer/frontend
- full release-script smoke via `scripts/run_all.sh`

This is a serious declared quality bar.

### 6.2 What Local Repo State Currently Reproduces

Observed locally during analysis:

- `ctest --test-dir build -N` returned `Total Tests: 0`
- `build/CTestTestfile.cmake` is missing
- `build/` contains object files and `libkolibri_core.a`, so it is not an empty build tree, but it is not a complete CTest-ready tree
- `python3 -m pytest --collect-only tests -q` fails because `pytest` is not installed in the current Python environment
- frontend test is currently `node ./scripts/smoke-contracts.mjs`, which validates stream protocol parsing rather than UI behavior

### 6.3 Interpretation

The repo currently has three quality layers:

1. Declared CI quality bar: broad and ambitious
2. Build-script assumptions: broad, integrated, end-to-end
3. Local workstation reality captured in this analysis: incomplete environment and incomplete test inventory

This is not just an environment issue. It is an architecture/process issue because the repository advertises a tighter "single source of truth" and reproducibility story than the local state presently demonstrates.

## 7. Hotspots

### 7.1 Largest Active Code Hotspots

| File | Approx. lines | Why it matters |
| --- | ---: | --- |
| `backend/service/ai_engine.py` | 11680 | Central orchestration brain; oversized integration point |
| `backend/src/compress.c` | 6675 | Large native subsystem, hard to reason about locally |
| `backend/src/inference.c` | 4844 | Core native inference hotspot |
| `backend/src/kolibri_http_server.c` | 3315 | Native runtime gateway with broad HTTP surface |
| `backend/src/script.c` | 3260 | Core script/runtime logic |
| `backend/service/ai_chat.py` | 2634 | Large product API aggregation layer |
| `backend/service/number_mind.py` | 2370 | Core Python intelligence layer |
| `tests/test_ai_engine_integration.py` | 2237 | Massive test hotspot, indicates high coupling around engine behavior |
| `frontend/src/api.ts` | 929 | Frontend request orchestration hotspot |
| `frontend/src/components/LiveQueueDashboard.tsx` | 873 | Large frontend dashboard surface |

### 7.2 Architectural Meaning of These Hotspots

- The backend is dominated by large integration files rather than many small isolated modules.
- The native runtime also centralizes large amounts of behavior into a few files.
- The frontend is smaller overall, but `api.ts` is a strong coupling point between UI, session logic, caching, streaming, fallback, and model mode selection.
- The test suite mirrors this coupling by concentrating many assertions around `ai_engine`.

## 8. Subsystem Matrix

| Subsystem | Runtime Purpose | Integration | Test Status Seen Locally | Risk |
| --- | --- | --- | --- | --- |
| `frontend/src` | Main AI PWA/chat client | High | Smoke contract only observed | Medium |
| `backend/service` | Main HTTP gateway and orchestration | High | Python env missing locally | High |
| `backend/src` | Native core and alternate HTTP runtime | High | Build artifacts exist, CTest inventory absent locally | High |
| `backend/include/kolibri` | Native public API surface | High | No direct validation in this analysis | Medium |
| `apps/` | CLI/operator surface | Medium | Indirect via CMake/CI only | Medium |
| `core/` | Python simulation/testing API | Medium | Present and documented | Medium |
| `swarm` | Operational scripts/config | Medium | Not executed | Medium |
| `frontend/kolibri-web` | Separate license-management SPA | Low to main AI contour | Not validated here | Medium |
| `mobile/kolibri-app` | Separate mobile product line | Low to main AI contour | Not validated here | Medium |
| `cloud-storage` | Standalone storage demo service | Low | Not validated here | Medium |
| `content_factory_mvp*` | Prototype product contours | Low | Local tests not run | Medium |
| `kernel` | Experimental OS/kernel contour | Very low | Not validated here | High as complexity source |
| `web-app` | Legacy single-file web contour | Very low | Not validated here | Low as runtime, medium as confusion source |
| `data`, `knowledge`, `test_results` | Knowledge/artifacts/bench outputs | Support | N/A | High as repo ballast |

## 9. Main Risks

### 9.1 Structural Risks

1. **Gateway bifurcation**
   - FastAPI and C HTTP runtime overlap materially.
   - The transition is documented, but the repo still exposes both as meaningful surfaces.
   - Risk: duplicated behavior, drift in contracts, and split testing effort.

2. **Parallel product lines in one repo**
   - AI chat/PWA, license-management SPA, mobile license app, cloud storage, and content factory all coexist.
   - Risk: unclear ownership, mixed priorities, and documentation incoherence.

3. **Oversized orchestration files**
   - `ai_engine.py`, `ai_chat.py`, `kolibri_http_server.c`, `inference.c`, `compress.c` are all large and central.
   - Risk: change amplification, difficult review, shallow isolation, high regression surface.

### 9.2 Process and Quality Risks

4. **CI story stronger than local reproducibility**
   - CI advertises a high bar.
   - Local observed state does not currently reproduce the same baseline.
   - Risk: confidence mismatch and harder contributor onboarding.

5. **Documentation drift**
   - Some top-level/public docs are healthy.
   - Several local/subsystem READMEs are stale or describe previous architectures.
   - Risk: wrong implementation assumptions and slow navigation.

6. **Checked-in artifacts and dependencies**
   - `frontend/node_modules`, `frontend/dist`, large `data`, `test_results`, multiple `build*` directories.
   - Risk: clone weight, review noise, accidental coupling to local state, harder repo hygiene.

### 9.3 Secondary Technical Risks

7. **Boundary ambiguity around stable APIs**
   - Public interface docs define a narrow stable surface, but the implementation tree exposes much more.
   - Risk: consumers depending on non-stable headers or modules by accident.

8. **Prototype accumulation**
   - `content_factory_mvp`, `content_factory_mvp2`, `web-app`, `kernel`, assorted tools and archives remain in-tree.
   - Risk: architectural false complexity and maintenance drag.

## 10. Priority Conclusions

If this repository is treated as a single product, the active architectural center is clear:

- `frontend/src`
- `backend/service`
- `backend/src`
- `backend/include/kolibri`
- `apps/`
- CI/CMake/WASM packaging

Everything else should be framed explicitly as one of:

- secondary supported product line
- experimental contour
- archived/legacy contour
- artifact/data ballast

Without that boundary, the repo reads as larger and less coherent than the active system actually is.

The most important next architectural cleanup targets are:

1. make the gateway boundary explicit: current FastAPI truth vs target C runtime truth;
2. separate the AI product contour from license-management and demo/service side projects at the documentation level;
3. reduce drift in subsystem READMEs, especially `frontend/README.md` and `backend/service/README.md`;
4. restore honest local reproducibility for at least one canonical path: build, tests, and runtime smoke;
5. define a repo hygiene policy for checked-in artifacts and giant local-state directories.

## 11. Appendix: Key Facts Used in This Analysis

- Root active contour named in `README.md`: backend/service, backend/src, frontend
- Public architecture source: `docs/PUBLIC_ARCHITECTURE.md`
- Public stable interfaces source: `docs/public_interfaces.md`
- Roadmap source: `docs/plans/ROADMAP_SINGLE_SOURCE_OF_TRUTH.md`
- Native HTTP runtime route scan: `backend/src/kolibri_http_server.c`
- FastAPI route scan: `backend/service/main.py` and included routers
- Frontend runtime surface: `frontend/src/App.tsx`, `frontend/src/api.ts`, `frontend/src/lib/kolibriBridge.ts`
- CLI/tooling surface: `apps/*.c`, `Makefile`, `scripts/run_all.sh`
- Local build/test reality:
  - `ctest --test-dir build -N` -> `Total Tests: 0`
  - `build/CTestTestfile.cmake` missing
  - `python3 -m pytest` unavailable in current environment
  - frontend test script is a smoke-contract parser test, not full UI coverage
