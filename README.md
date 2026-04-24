# Kolibri

Kolibri сейчас официально описывается как один продуктовый контур:

`web -> services -> core -> WASM -> apps`

Это означает следующее:

- `web/` является текущим shipping web shell.
- `services/` является текущим shipping FastAPI gateway и source of truth для chat/account/preferences/conversations/swarm runtime.
- `core/` и `backend/include/kolibri/` являются основным native C23 core и стабильным public C surface.
- `infra/build_wasm.sh`, `build/wasm/kolibri.wasm` и web public assets являются browser/offline runtime path того же продукта.
- `apps/` содержит поддерживаемые CLI и product-side native utilities.

`core/kolibri_http_server.c` остаётся важным runtime-path, но до прохождения того же release gate считается `parity-target`, а не shipping gateway.

## Официальный scope

### Shipping contour

- `web/`
- `services/`
- `core/`
- `backend/include/kolibri/`
- `infra/`
- `apps/`

### Integration-only / secondary contours

- `frontend/`
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
./infra/release_gate.sh bootstrap

python3 -m uvicorn services.main:app --host 0.0.0.0 --port 8001
npm run dev --prefix web -- --host 0.0.0.0 --port 3000
```

Web shell ожидает backend на `http://localhost:8001` и использует server-backed flows для auth, profile, preferences, conversations и chat. Browser/offline режим идёт через WASM bridge.

## Release Gate

Канонический локальный gate для shipping-контура:

```bash
./infra/release_gate.sh all
```

Или через `make`:

```bash
make release-gate
```

Состав release gate:

- native: CMake build + release-blocking CTest inventory;
- backend: целевой pytest-набор для auth/account/preferences/chat/swarm/kpack/rate limiting;
- wasm: сборка `kolibri.wasm` и обновление web public artifacts;
- frontend: `npm run test`, `npm run lint`, `npm run build` через `web/`.

Всё остальное относится к `Extended CI`:

```bash
make extended-ci
```

## Документы Source Of Truth

### Product truth

- `docs/CANONICAL_REPO_CONTOUR.md`
- `docs/PRODUCT_SPEC_V2.md`
- `docs/API_REFERENCE.md`
- `docs/PUBLIC_ARCHITECTURE.md`
- `docs/public_interfaces.md`

### Engineering truth

- `docs/QA_ACCEPTANCE.md`
- `docs/DEPLOY_RUNBOOK.md`
- `docs/RELEASE_CHECKLIST.md`
- `docs/release_process.md`
- `AGENTS.md`

### Integration truth

- `docs/INTEGRATION_SURFACES.md`
- `docs/plans/ROADMAP_SINGLE_SOURCE_OF_TRUTH.md`

## Основные entrypoints

- web shell: `web/src/App.tsx`
- web WASM bridge: `web/src/lib/kolibriBridge.ts`
- backend app: `services/main.py`
- backend chat router: `services/ai_chat.py`
- backend engine: `services/ai_engine.py`
- backend swarm runtime: `services/swarm_runtime_api.py`
- native HTTP runtime: `core/kolibri_http_server.c`

## Статусы

- `shipping` — часть официального релизного контура и release gate
- `parity-target` — официальный runtime-path, но ещё не проходит тот же release gate
- `integration-only` — подключается к shipping-контуру через интерфейсы, но не входит в релизный scope
- `experimental` — код существует, но не должен рекламироваться как стабильный продуктовый surface
