# Kolibri Deploy Runbook

## 1. Scope

Этот runbook относится только к active shipping contour:

- `frontend/src`
- `backend/service`
- `backend/src`
- `backend/include/kolibri`
- `build/wasm/kolibri.wasm`
- `apps/`

`backend/src/kolibri_http_server.c` пока не рассматривается как отдельный production deploy target. Для текущего релиза shipping gateway остаётся FastAPI service.

## 2. Preconditions

Перед выкладкой обязаны быть зелёными:

```bash
./scripts/release_gate.sh all
```

Дополнительно должны быть обновлены:

- `README.md`
- `docs/PRODUCT_SPEC_V2.md`
- `docs/API_REFERENCE.md`
- `docs/QA_ACCEPTANCE.md`
- `docs/RELEASE_CHECKLIST.md`
- `docs/release_notes.md`

## 3. Backend deploy

1. Обновить код на целевом сервере.
2. Установить Python dependencies из `requirements.txt`.
3. Перезапустить `uvicorn backend.service.main:app`.
4. Проверить:
   - `GET /api/health`
   - `GET /api/v1/auth/status`
   - `POST /api/v1/ai/chat`
   - `GET /api/v1/swarm/runtime/status`

## 4. Frontend deploy

1. Собрать WASM:

```bash
./scripts/release_gate.sh wasm
```

2. Собрать production frontend:

```bash
npm run build --prefix frontend
```

3. Опубликовать `frontend/dist`.
4. Убедиться, что опубликованный bundle использует актуальные assets, включая `kolibri.wasm`.

## 5. Mandatory smoke after deploy

1. Открыть shipping shell.
2. Проверить auth status.
3. Открыть settings и убедиться, что profile/preferences загружаются.
4. Создать conversation.
5. Отправить sync chat запрос.
6. Проверить streaming chat.
7. Открыть workspace.
8. Проверить `swarm runtime status`.
9. Выполнить один ingest или `.kpack` import/export smoke.

## 6. Rollback triggers

Rollback обязателен, если после выкладки:

- chat endpoint перестаёт отвечать рабочим contract;
- frontend shell становится unusable;
- auth/profile/preferences ломают settings flow;
- conversation sidebar перестаёт резолвить server state;
- `swarm runtime status` или `.kpack` flows перестают отвечать.

## 7. Rollback actions

1. Вернуть предыдущую backend revision.
2. Вернуть предыдущий frontend bundle.
3. Перезапустить backend.
4. Повторить smoke:
   - `/api/health`
   - `/api/v1/auth/status`
   - `/api/v1/ai/chat`
   - `/api/v1/swarm/runtime/status`
