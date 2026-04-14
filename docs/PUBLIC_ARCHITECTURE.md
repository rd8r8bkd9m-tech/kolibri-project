# Kolibri Public Architecture

## 1. Official system story

Kolibri публично описывается как одна система, а не как набор репозиторных веток:

`frontend/PWA -> FastAPI gateway -> C core -> knowledge/provenance/swarm -> WASM/offline -> apps`

При этом текущие статусы такие:

- FastAPI gateway — `shipping`
- C HTTP runtime (`backend/src/kolibri_http_server.c`) — `parity-target`
- secondary contours — `integration-only`

## 2. Layers

### Product surface

- `frontend/src`
- `frontend/src/api.ts`
- `frontend/src/lib/kolibriBridge.ts`

### Shipping gateway

- `backend/service/main.py`
- `backend/service/ai_chat.py`
- `backend/service/ai_engine.py`
- `backend/service/swarm_runtime_api.py`

### Native core

- `backend/src`
- `backend/include/kolibri`

### Browser/offline runtime

- `scripts/build_wasm.sh`
- `build/wasm/kolibri.wasm`
- `frontend/public/kolibri.wasm`

### Product-side utilities

- `apps/`

## 3. Main runtime rule

Один и тот же продукт должен быть объясним через один контракт:

- frontend не придумывает отдельный продуктовый API;
- backend/service является текущей server truth;
- C runtime обязан догонять тот же контракт, а не иметь собственную несовместимую историю;
- WASM path является другим runtime mode того же продукта, а не отдельным приложением.

## 4. Secondary contours

Следующие каталоги не входят в ближайший shipping scope:

- `frontend/kolibri-web`
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
- `docs/PRODUCT_SPEC_V2.md`
- `docs/API_REFERENCE.md`
- `docs/public_interfaces.md`
- `docs/QA_ACCEPTANCE.md`
- `docs/DEPLOY_RUNBOOK.md`
- `docs/INTEGRATION_SURFACES.md`
- `docs/RELEASE_CHECKLIST.md`
