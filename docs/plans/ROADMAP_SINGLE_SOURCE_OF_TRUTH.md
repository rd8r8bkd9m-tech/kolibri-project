# Kolibri Single Source Of Truth Roadmap

Дата обновления: 2026-04-10

## 1. Current official contour

Официальным продуктом считается только следующий shipping contour:

`frontend/src + backend/service + backend/src + WASM + apps`

Все остальные контуры должны описываться как `integration-only` или `experimental`, пока не попадают в тот же release gate.

## 2. Confirmed in code

### 2.1 Shipping gateway

- `backend/service/main.py`
- `backend/service/ai_chat.py`
- `backend/service/ai_engine.py`
- `backend/service/swarm_runtime_api.py`
- `backend/service/auth.py`
- `backend/service/account.py`

### 2.2 Native core

- `backend/src/formula.c`
- `backend/src/script.c`
- `backend/src/knowledge.c`
- `backend/src/logical_memory.c`
- `backend/src/context_window.c`
- `backend/src/reasoning_engine.c`
- `backend/src/world_model.c`
- `backend/src/vision.c`
- `backend/src/audio.c`

### 2.3 Frontend and WASM

- `frontend/src/App.tsx`
- `frontend/src/api.ts`
- `frontend/src/lib/kolibriBridge.ts`
- `scripts/build_wasm.sh`
- `backend/src/wasm_bridge.c`

### 2.4 Product-side utilities

- `apps/kolibri_node.c`
- `apps/kolibri_infer_cli.c`
- `apps/kolibri_ingest.c`
- `apps/kolibri_inspect.c`
- `apps/kolibri_learn.c`

## 3. Drift corrected

Подтверждено, что ранее в документах были неверно помечены как отсутствующие следующие файлы:

- `engine/gpu_encoder/gpu_encoder_cuda.cu`
- `backend/src/vision.c`
- `backend/src/audio.c`

По состоянию на эту дату всё ещё не подтверждены как существующие:

- `backend/src/reasoning.c`
- `backend/src/knowledge_base.c`
- `apps/kolibri_memory.c`

## 4. Current priorities

### Priority A: documentation truth

- держать `README.md`, `docs/PRODUCT_SPEC_V2.md`, `docs/API_REFERENCE.md`, `docs/QA_ACCEPTANCE.md`, `docs/DEPLOY_RUNBOOK.md` синхронизированными с живым кодом;
- не путать `not in shipping scope` с `not implemented`.

### Priority B: release gate parity

- один локальный gate через `scripts/release_gate.sh`;
- те же команды в `Makefile`, `scripts/run_all.sh` и `.github/workflows/ci.yml`;
- честная проверка CTest inventory.

### Priority C: release story

- один canonical product narrative;
- один evidence pack;
- один список public interfaces.

## 5. Status rules

- `shipping` — код + релизный контур + release gate
- `parity-target` — официальный target, но ещё не доказан тем же gate
- `integration-only` — использует shipping interfaces, но не входит в релиз
- `experimental` — код есть, но стабильность не обещана
