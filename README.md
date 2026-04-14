# Kolibri

Kolibri сейчас официально описывается как один продуктовый контур:

`frontend/src -> backend/service -> backend/src -> WASM -> apps`

Это означает следующее:

- `frontend/src` является единственным shipping web shell.
- `backend/service` является текущим shipping gateway и source of truth для chat/account/preferences/conversations/swarm runtime.
- `backend/src` и `backend/include/kolibri` являются основным native core.
- `build/wasm/kolibri.wasm` и `frontend/public/kolibri.wasm` являются browser/offline runtime path того же продукта.
- `apps/` содержит поддерживаемые CLI и product-side native utilities.

`backend/src/kolibri_http_server.c` остаётся важным runtime-path, но до прохождения того же release gate считается `parity-target`, а не shipping gateway.

## Официальный scope

### Shipping contour

- `frontend/src`
- `backend/service`
- `backend/src`
- `backend/include/kolibri`
- `scripts/build_wasm.sh`
- `apps/`

### Integration-only / secondary contours

- `frontend/kolibri-web`
- `mobile/kolibri-app`
- `cloud-storage`
- `content_factory_*`
- `swarm`
- `sdk/python`
- `kernel`
- `web-app`

Эти каталоги остаются в репозитории, но не входят в ближайший shipping release.

## Быстрый старт

```bash
python3 -m venv .venv
source .venv/bin/activate
./scripts/release_gate.sh bootstrap

python3 -m uvicorn backend.service.main:app --host 0.0.0.0 --port 8001
npm run dev --prefix frontend -- --host 0.0.0.0 --port 3000
```

Frontend ожидает backend на `http://localhost:8001` и использует server-backed flows для auth, profile, preferences, conversations и chat.

## Release Gate

Канонический локальный gate для shipping-контура:

```bash
./scripts/release_gate.sh all
```

Или через `make`:

```bash
make release-gate
```

Состав release gate:

- native: `test_kolibri_http_server_api`, `test_kolibri_http_stream_api`, `test_kolibri_http_phase1_benchmark`
- backend: целевой pytest-набор для auth/account/preferences/chat/swarm/kpack/rate limiting
- wasm: сборка `kolibri.wasm` и обновление `frontend/public`
- frontend: `npm run test`, `npm run lint`, `npm run build`

Всё остальное относится к `Extended CI`:

```bash
make extended-ci
```

## Документы Source Of Truth

### Product truth

- `docs/PRODUCT_SPEC_V2.md`
- `docs/API_REFERENCE.md`
- `docs/PUBLIC_ARCHITECTURE.md`
- `docs/public_interfaces.md`

### Engineering truth

- `docs/QA_ACCEPTANCE.md`
- `docs/DEPLOY_RUNBOOK.md`
- `docs/RELEASE_CHECKLIST.md`
- `docs/release_process.md`

### Integration truth

- `docs/INTEGRATION_SURFACES.md`
- `docs/plans/ROADMAP_SINGLE_SOURCE_OF_TRUTH.md`

## Основные entrypoints

- frontend shell: `frontend/src/App.tsx`
- frontend API client: `frontend/src/api.ts`
- frontend WASM bridge: `frontend/src/lib/kolibriBridge.ts`
- backend app: `backend/service/main.py`
- backend chat router: `backend/service/ai_chat.py`
- backend engine: `backend/service/ai_engine.py`
- backend swarm runtime: `backend/service/swarm_runtime_api.py`
- native HTTP runtime: `backend/src/kolibri_http_server.c`

## Статусы

- `shipping` — часть официального релизного контура и release gate
- `parity-target` — официальный runtime-path, но ещё не проходит тот же release gate
- `integration-only` — подключается к shipping-контуру через интерфейсы, но не входит в релизный scope
- `experimental` — код существует, но не должен рекламироваться как стабильный продуктовый surface
