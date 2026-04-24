# Kolibri Public Architecture

## 1. Official system story

Kolibri публично описывается как одна система, а не как набор репозиторных веток:

`web/PWA -> FastAPI services gateway -> C23 core -> knowledge/provenance/swarm -> WASM/offline -> apps`

При этом текущие статусы такие:

- FastAPI gateway in `services/` — `shipping`
- C HTTP runtime (`core/kolibri_http_server.c`) — `parity-target`
- secondary contours — `integration-only`

## 2. Layers

### Product surface

- `web/`
- `web/src/App.tsx`
- `web/src/lib/kolibriBridge.ts`

### Shipping gateway

- `services/main.py`
- `services/ai_chat.py`
- `services/ai_engine.py`
- `services/swarm_runtime_api.py`

### Native core

- `core/`
- `backend/include/kolibri/`

### Browser/offline runtime

- `infra/build_wasm.sh`
- `build/wasm/kolibri.wasm`
- web public assets consumed by `web/src/lib/kolibriBridge.ts`

### Product-side utilities

- `apps/`

## 3. Main runtime rule

Один и тот же продукт должен быть объясним через один контракт:

- web не придумывает отдельный продуктовый API;
- `services/` является текущей server truth;
- C runtime обязан догонять тот же контракт, а не иметь собственную несовместимую историю;
- WASM path является другим runtime mode того же продукта, а не отдельным приложением.

## 4. Secondary contours

Следующие каталоги не входят в ближайший shipping scope:

- `frontend/`
- `mobile/kolibri-app`
- `cloud-storage`
- `content_factory_*`
- `swarm`
- `sdk/python`
- `kernel`
- `web-app`

Они допускаются только как `integration-only` потребители public interfaces shipping-контура.

## 5. Official document set

- `README.md`
- `AGENTS.md`
- `docs/CANONICAL_REPO_CONTOUR.md`
- `docs/PRODUCT_SPEC_V2.md`
- `docs/API_REFERENCE.md`
- `docs/public_interfaces.md`
- `docs/QA_ACCEPTANCE.md`
- `docs/DEPLOY_RUNBOOK.md`
- `docs/INTEGRATION_SURFACES.md`
- `docs/RELEASE_CHECKLIST.md`
