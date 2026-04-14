# Kolibri API Reference

## 1. Runtime status

- `backend/service` — текущая `shipping` HTTP truth.
- `backend/src/kolibri_http_server.c` — `parity-target` для того же product contract.
- `frontend/src/api.ts` — canonical web client к этому surface.
- `frontend/src/lib/kolibriBridge.ts` + `kolibri.wasm` — совместимый WASM/offline path.

Базовый backend URL по умолчанию:

```text
http://localhost:8001
```

Swagger UI:

```text
http://localhost:8001/docs
```

## 2. Canonical HTTP surface

### Health

- `GET /api/health`
- `GET /api/knowledge/healthz`
- `GET /live`
- `GET /ready`
- `GET /detail`

Для product shell обязательными считаются `GET /api/health` и `GET /api/knowledge/healthz`.

### Auth

- `GET /api/v1/auth/status`
- `POST /api/v1/auth/login`
- `POST /api/v1/auth/logout`
- `POST /api/v1/auth/register`

`/register` используется как operator/admin flow и не является основной end-user точкой входа.

### Account

- `GET /api/v1/account/profile`
- `PUT /api/v1/account/profile`
- `GET /api/v1/account/preferences`
- `PUT /api/v1/account/preferences`

Эти endpoints являются server source of truth для:

- `name`
- `facts`
- `theme`
- `persona`
- `memory_enabled`
- `model`

### Chat

- `POST /api/v1/ai/chat`
- `POST /api/v1/ai/chat/stream`
- `GET /api/v1/ai/models`
- `GET /api/v1/ai/stats`

Shipping chat contract:

- sync chat должен возвращать `response`, `conversation_id`, `confidence`, `method`, `duration_ms`
- stream endpoint должен поддерживать thinking/search/generate lifecycle без broken state в shell
- frontend sidebar и thread должны опираться на server conversation state

### Conversations

- `GET /api/v1/ai/conversations`
- `POST /api/v1/ai/conversations`
- `GET /api/v1/ai/conversations/{conversation_id}/turns`
- `PATCH /api/v1/ai/conversations/{conversation_id}`
- `DELETE /api/v1/ai/conversations/{conversation_id}`

### Swarm runtime

- `GET /api/v1/swarm/runtime/status`
- `POST /api/v1/swarm/runtime/start`
- `POST /api/v1/swarm/runtime/run`
- `POST /api/v1/swarm/runtime/refresh`
- `POST /api/v1/swarm/runtime/ingest/text`
- `POST /api/v1/swarm/runtime/ingest/url`
- `POST /api/v1/swarm/runtime/kpack/export`
- `GET /api/v1/swarm/runtime/kpack/download/{filename}`
- `POST /api/v1/swarm/runtime/kpack/import`

### Background learning inside swarm runtime

- `GET /api/v1/swarm/runtime/learning/status`
- `POST /api/v1/swarm/runtime/learning/start`
- `POST /api/v1/swarm/runtime/learning/run`
- `GET /api/v1/swarm/runtime/learning/history`
- `GET /api/v1/swarm/runtime/learning/sources`
- `PUT /api/v1/swarm/runtime/learning/sources`

## 3. Public surface that exists but is not canonical for release marketing

Следующие endpoints существуют в коде, но не должны рекламироваться как основной shipping narrative, пока не становятся частью release gate и product story:

- `POST /api/v1/ai/imagine`
- `POST /api/v1/ai/vision/analyze`
- `POST /api/v1/ai/train`
- `POST /api/v1/ai/pattern`
- `POST /api/v1/ai/embedding`
- `GET|POST /api/v1/ai/quality/*`
- `GET|POST /api/v1/rag/*`
- `POST /api/v1/learning/*`

Они остаются доступными как `experimental` или `operator-facing`, но не являются причиной расширять официальный release scope.

## 4. Stable C interfaces

Stable C API документируется в `docs/public_interfaces.md` и реализуется в `backend/include/kolibri/*.h`.

Для shipping-контура главным образом считаются следующие header families:

- `formula.h`
- `script.h`
- `knowledge.h`
- `genome.h`
- `logical_memory.h`
- `net.h`
- `context.h`
- `corpus.h`
- `corpus_trainer.h`

Остальные заголовки могут использоваться внутри native core, но не должны без отдельного решения рекламироваться как стабильный внешний ABI.

## 5. WASM surface

WASM path использует тот же продуктовый contour:

- artifact: `build/wasm/kolibri.wasm`
- shipped copy: `frontend/public/kolibri.wasm`
- frontend bridge: `frontend/src/lib/kolibriBridge.ts`

WASM path считается совместимым runtime surface, но не заменяет backend shipping truth.

## 6. CLI / native utilities

Product-side native utilities расположены в `apps/`. В ближайшем релизном нарративе важны:

- `apps/kolibri_node.c`
- `apps/kolibri_infer_cli.c`
- `apps/kolibri_ingest.c`
- `apps/kolibri_inspect.c`
- `apps/kolibri_learn.c`

Их статус и ожидаемая стабильность фиксируются в `docs/public_interfaces.md`.
