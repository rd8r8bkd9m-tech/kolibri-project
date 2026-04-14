# Kolibri Backend Service

`backend/service` является текущим shipping gateway проекта Kolibri.

Это означает:

- именно FastAPI-сервис сейчас считается официальной runtime truth для web product;
- `backend/src/kolibri_http_server.c` остаётся важным native runtime-path, но до прохождения того же release gate имеет статус `parity-target`;
- все публичные product flows для chat/account/preferences/conversations/swarm runtime должны быть воспроизводимы через этот сервис.

## Основные entrypoints

- `main.py` — приложение FastAPI и composition root
- `ai_chat.py` — chat, conversations, quality, vision, imagine, knowledge helpers
- `ai_engine.py` — singleton runtime engine
- `swarm_runtime_api.py` — swarm runtime, ingest, refresh, `.kpack`, background learning

## Shipping routers

По умолчанию backend включает следующий официальный product surface:

- `/api/health`
- `/api/knowledge/healthz`
- `/api/v1/auth/*`
- `/api/v1/account/*`
- `/api/v1/ai/*`
- `/api/v1/ai/chat/stream`
- `/api/v1/swarm/runtime/*`

Дополнительно backend публикует `/api/v1/learning/*`, но этот контур пока не является частью канонического public release narrative.

## Chat-only mode

По умолчанию сервис стартует в `chat-only` режиме и не рекламирует старые дополнительные роутеры как часть shipping surface.

Если `KOLIBRI_CHAT_ONLY_MODE=0`, сервис может подключать дополнительные compatibility routers, включая GPU/dev/crawler/agent/swarm sync и другие исторические поверхности. Они не входят в ближайший release scope.

## Команды

Локальный запуск:

```bash
python3 -m uvicorn backend.service.main:app --host 0.0.0.0 --port 8001
```

Release gate backend:

```bash
./scripts/release_gate.sh backend
```

Полный shipping gate:

```bash
./scripts/release_gate.sh all
```

## Shipping contract

Backend обязан быть source of truth для:

- auth status, login, logout
- account profile и preferences
- conversation metadata и turns
- sync chat response
- streaming chat
- swarm runtime status / ingest / refresh
- `.kpack` import/export

Если endpoint не относится к этому списку и не проходит release gate, он не должен рекламироваться в public docs как стабильный продуктовый API.
