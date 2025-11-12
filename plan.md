# Kolibri Development Plan

## 1. Policy Context
- Follow `AGENTS.md` ownership rules: Kolibri controls build/code/docs; prefer in-tree sources for `backend/`, `frontend/`, `scripts/`, but respect upstream versions for `docs/` and `README.md`.
- Honor platform budgets: keep WebAssembly bundles under **60 MB**, target step latency below **250 ms**, and maintain minimum coverage (**75 lines / 60 branches**).

## 2. Objectives
1. Ship a unified KolibriSim stack in pure C/WASM with automated CLI + frontend delivery.
2. Complete the Kolibri OS boot chain (loader, RAM disk genome, SLIP/UDP, telemetry).
3. Provide enterprise readiness: SSO, RBAC, audit logging, and admin workflows.
4. Harden the knowledge pipeline with moderation UI, versioning, ACLs, and contracts.
5. Enforce deterministic tooling (tests, coverage, fuzzing, CI) to meet policy budgets.

## 3. Workstreams & Key Deliverables
### 3.1 KolibriSim C/WASM Unification
- Implement `backend/include/kolibri/sim.h` API plus `backend/src/sim/*`.
- Build `kolibri_sim_cli` (tick/soak/export) and wasm wrapper exposed via `frontend/src/core/kolibri-bridge.ts`.
- Update `scripts/build_wasm.sh`, `scripts/soak.py`, `ci_bootstrap.sh`, and tests (`tests/test_sim*.c`) to rely on the new runtime; retire Python shim.

### 3.2 WASM Build & Delivery Pipeline
- Extend CMake to emit `kolibri.wasm`/`kolibri_sim.wasm` with symbol exports and size guard (< 60 MB).
- Integrate artifacts into the React app, verify initialization latency (< 250 ms), and add CI checks for bundle size regression.

### 3.3 Kolibri OS Boot & Runtime
- Author `boot/kolibri.asm`, RAM-disk genome persistence, SLIP/UDP driver, and serial monitor per `docs/kolibri_os.md`.
- Automate packaging via `scripts/package_release.sh`, produce `build/kolibri.img`, and add QEMU smoke tests.

### 3.4 Enterprise IAM & Audit
- Stage OAuth2/OIDC → SAML SSO, role-based middleware, UI gating, and signed JSONL audit export (SIEM-ready).
- Document configuration in `docs/deployment.md` and update frontend admin console UX.

### 3.5 Knowledge Pipeline & Governance
- Build visual moderator React app, implement snapshot versioning + ACL enforcement, and publish REST/gRPC schemas (Swagger/AsyncAPI).
- Enhance `scripts/knowledge_pipeline.sh` and backend services to honor approvals and track provenance.

## 4. Quality & Compliance
- Maintain `make test`, fuzz (`build-fuzz`), wasm smoke, and KolibriSim soak as mandatory gates.
- Track coverage thresholds (>= 75 lines / 60 branches) via CI reports and require waivers for exceptions.
- Monitor wasm size/latency budgets and log deviations for triage.

## 5. Next Immediate Steps
1. Land KolibriSim C core skeleton with unit tests.
2. Wire wasm build targets and ensure frontend bridge loads new module.
3. Draft bootloader prototype + packaging script to unblock Kolibri OS QA.
