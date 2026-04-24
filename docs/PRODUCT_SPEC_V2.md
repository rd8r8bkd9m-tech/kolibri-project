# Kolibri Product Spec V2

## 1. Product promise

Kolibri сейчас shipping как один chat-first продукт с единым системным контуром:

`web -> services -> core -> WASM -> apps`

Ближайший релиз не обещает весь репозиторий. Он обещает один chat shell, один backend gateway, один набор canonical API, один release gate и один demo-path вокруг chat, conversations, swarm runtime и kpack.

## 2. Runtime truth

- `services/` — текущая `shipping` truth.
- `core/kolibri_http_server.c` — `parity-target`.
- `web/src/lib/kolibriBridge.ts` плюс WASM artifacts — browser/offline runtime path того же продукта.

Никакой внешний документ не должен представлять C runtime как текущий shipping gateway, пока он не проходит тот же release gate, что и FastAPI path.

## 3. Primary user flow

Канонический пользовательский сценарий:

1. открыть chat shell;
2. проверить auth status;
3. загрузить profile и preferences;
4. создать или выбрать conversation;
5. отправить сообщение;
6. использовать streaming и stop;
7. открыть workspace;
8. выполнить ingest, refresh, kpack import/export;
9. при необходимости использовать WASM/offline path.

## 4. Shipping surfaces

### Web

- chat shell;
- thread viewport;
- composer;
- settings and workspace surfaces;
- WASM bridge path.

### Backend

- health;
- auth;
- account profile/preferences;
- chat;
- conversations;
- swarm runtime;
- kpack.

### Native / WASM

- C23 core как основное вычислительное ядро;
- browser-delivered WASM runtime;
- product-side CLI utilities из `apps/`.

## 5. Canonical APIs

Канонический public surface для ближайшего релиза:

- `/api/v1/ai/chat`
- `/api/v1/ai/chat/stream`
- `/api/v1/ai/conversations`
- `/api/v1/account/profile`
- `/api/v1/account/preferences`
- `/api/v1/auth/*`
- `/api/v1/swarm/runtime/*`
- kpack import/export через swarm runtime

## 6. Explicit non-scope

Следующие контуры не входят в ближайший shipping scope:

- `frontend/`
- `mobile/kolibri-app`
- `cloud-storage`
- `content_factory_*`
- `swarm`
- `sdk/python`
- `kernel`
- `web-app`

Они могут пользоваться интерфейсами shipping-контура, но не должны описываться как часть официального релиза.

## 7. Product quality bar

Релиз считается честным только если зелёны или явно зафиксированы как не запущенные:

- native release gate;
- backend release gate;
- wasm build;
- web smoke/typecheck/build;
- документация из `README.md`, `docs/CANONICAL_REPO_CONTOUR.md`, `docs/API_REFERENCE.md`, `docs/QA_ACCEPTANCE.md`, `docs/DEPLOY_RUNBOOK.md`, `docs/RELEASE_CHECKLIST.md`.

## 8. Status taxonomy

- `shipping` — официально входит в релизный контур и release gate
- `parity-target` — должен догнать shipping-контур по контракту и проверкам
- `integration-only` — использует public interfaces, но не входит в релизный scope
- `experimental` — код существует, но не обещан как стабильный продуктовый surface
