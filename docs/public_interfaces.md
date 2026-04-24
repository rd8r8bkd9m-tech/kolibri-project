# Kolibri Public Interfaces

Этот документ фиксирует интерфейсы, которые считаются официальными для текущего shipping-контура Kolibri. Всё, что не перечислено здесь, считается `experimental`, `integration-only` или `parity-target`.

## 1. Status taxonomy

- `shipping` — входит в официальный релизный контур и release gate
- `parity-target` — должен поддержать тот же контракт, но ещё не является shipping truth
- `integration-only` — использует shipping interfaces, но не входит в release scope
- `experimental` — может меняться без обещания стабильности

## 2. Stable C API

Следующие header families под `backend/include/kolibri/` считаются основным стабильным C surface для текущего продукта:

- `script.h`
- `formula.h`
- `knowledge.h`
- `genome.h`
- `logical_memory.h`
- `net.h`
- `context.h`
- `corpus.h`
- `corpus_trainer.h`
- `decimal.h`
- `digits.h`
- `random.h`

Практическое правило:

- consumer code должен опираться только на документированные функции и типы из этого набора;
- расширение набора допустимо только после отдельного решения и отражения в release docs;
- остальные заголовки могут использоваться внутренне, но не считаются обещанным внешним ABI.

Статус:

- C API headers above — `shipping`
- `core/kolibri_http_server.c` as gateway runtime — `parity-target`

## 3. HTTP API

Stable HTTP surface реализуется текущим FastAPI gateway в `services/`:

- `/api/health`
- `/api/knowledge/healthz`
- `/api/v1/auth/status`
- `/api/v1/auth/login`
- `/api/v1/auth/logout`
- `/api/v1/auth/register`
- `/api/v1/account/profile`
- `/api/v1/account/preferences`
- `/api/v1/ai/chat`
- `/api/v1/ai/chat/stream`
- `/api/v1/ai/conversations`
- `/api/v1/ai/conversations/{conversation_id}/turns`
- `/api/v1/ai/models`
- `/api/v1/ai/stats`
- `/api/v1/swarm/runtime/status`
- `/api/v1/swarm/runtime/start`
- `/api/v1/swarm/runtime/run`
- `/api/v1/swarm/runtime/refresh`
- `/api/v1/swarm/runtime/ingest/text`
- `/api/v1/swarm/runtime/ingest/url`
- `/api/v1/swarm/runtime/kpack/export`
- `/api/v1/swarm/runtime/kpack/download/{filename}`
- `/api/v1/swarm/runtime/kpack/import`
- `/api/v1/swarm/runtime/learning/status`
- `/api/v1/swarm/runtime/learning/start`
- `/api/v1/swarm/runtime/learning/run`
- `/api/v1/swarm/runtime/learning/history`
- `/api/v1/swarm/runtime/learning/sources`

Статус:

- FastAPI HTTP surface in `services/` — `shipping`
- C HTTP runtime compatibility surface in `core/kolibri_http_server.c` — `parity-target`

## 4. Web and WASM interfaces

Stable web-facing surface:

- `web/src/App.tsx`
- `web/src/lib/kolibriBridge.ts`
- web public WASM assets produced by `infra/build_wasm.sh`

Статус:

- web shell contract — `shipping`
- browser/offline WASM path — `shipping`
- alternative frontend packages outside `web/` — `integration-only`

## 5. CLI / public binaries

Для текущего product contour публично значимыми считаются следующие binaries / utilities из `apps/`:

- `kolibri_node`
- `kolibri_infer_cli`
- `kolibri_ingest`
- `kolibri_inspect`
- `kolibri_learn`

Их роль:

- `kolibri_node` — native node/runtime utility
- `kolibri_infer_cli` — CLI-инференс
- `kolibri_ingest` — CLI-ingest в knowledge contour
- `kolibri_inspect` — inspection/debug utility
- `kolibri_learn` — learning utility

Статус:

- перечисленные product-side utilities — `shipping`
- остальные binaries из `apps/` — `experimental`, если не описаны отдельно в release docs

## 6. Secondary consumers

Следующие каталоги могут использовать shipping interfaces, но сами не считаются частью ближайшего релиза:

- `frontend/`
- `mobile/kolibri-app`
- `cloud-storage`
- `content_factory_*`
- `swarm`
- `sdk/python`
- `kernel`
- `web-app`
