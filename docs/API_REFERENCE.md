# Kolibri OS — REST API Reference

## Базовый URL

```
http://localhost:8001
```

Swagger UI: `http://localhost:8001/docs`

---

## Аутентификация

Обычный chat/runtime API может работать без логина, но product shell использует auth/account endpoints для server-backed профиля, настроек и conversation metadata.

Боевые auth endpoints:

- `GET /api/v1/auth/status`
- `POST /api/v1/auth/login`
- `POST /api/v1/auth/logout`
- `POST /api/v1/auth/register` (admin flow)

Для LLM-прокси необходимо установить переменные окружения:

```bash
export KOLIBRI_RESPONSE_MODE=llm
export KOLIBRI_LLM_ENDPOINT="https://your-llm-endpoint/v1/chat"
export KOLIBRI_LLM_API_KEY="your-key"
```

---

## Health & System

### `GET /api/health`

Проверка статуса сервиса.

**Ответ:**
```json
{
  "status": "ok",
  "response_mode": "script"
}
```

### `GET /api/knowledge/healthz`

Health-check базы знаний.

**Ответ:**
```json
{
  "status": "ok",
  "response_mode": "script"
}
```

---

## Account — `/api/v1/account`

- `GET /api/v1/account/profile`
- `PUT /api/v1/account/profile`
- `GET /api/v1/account/preferences`
- `PUT /api/v1/account/preferences`

Эти endpoints дают server-backed профиль пользователя/клиента и runtime preferences (`theme`, `persona`, `memory_enabled`, `model`).

---

## Swarm Runtime & Background Learning — `/api/v1/swarm/runtime`

Основные runtime endpoints:

- `GET /api/v1/swarm/runtime/status`
- `POST /api/v1/swarm/runtime/start`
- `POST /api/v1/swarm/runtime/run`
- `POST /api/v1/swarm/runtime/refresh`
- `POST /api/v1/swarm/runtime/ingest/text`
- `POST /api/v1/swarm/runtime/ingest/url`
- `POST /api/v1/swarm/runtime/kpack/export`
- `GET /api/v1/swarm/runtime/kpack/download/{filename}`
- `POST /api/v1/swarm/runtime/kpack/import`

Continuous background learning endpoints:

- `GET /api/v1/swarm/runtime/learning/status`
- `POST /api/v1/swarm/runtime/learning/start`
- `POST /api/v1/swarm/runtime/learning/run`
- `GET /api/v1/swarm/runtime/learning/history`
- `GET /api/v1/swarm/runtime/learning/sources`
- `PUT /api/v1/swarm/runtime/learning/sources`

`learning/status` возвращает:

- включён ли background learning
- работает ли daemon сейчас
- честный `internet_runtime` со статусом daemon/source path
- интервал цикла
- количество источников
- количество `eligible/backoff/failing/no-change` источников
- количество recent-success источников, чтобы статус не врал при успешных недавних циклах и деградировавшем probe
- последний успешный цикл
- последний error
- latest result по web-ingest
- history count
- source health с `consecutive_failures`, `consecutive_no_change`, `next_eligible_at`

`learning/sources` хранит список постоянных URL-источников для фонового internet-ingest. После успешного background cycle ingest пишет delta в live formula memory и ставит swarm refresh в очередь.

`learning/history` возвращает недавние циклы фонового обучения и health по каждому источнику. Ручной `POST /learning/run` выполняется в режиме operator override: он игнорирует временный backoff, чтобы можно было сразу перепроверить восстановившийся источник.

---

## AI Chat — `/api/v1/ai`

### `POST /api/v1/ai/chat`

Главный AI-чат с числовым формульным мышлением.

**Запрос:**
```json
{
  "message": "Что такое колибри?",
  "conversation_id": null,
  "temperature": 0.7
}
```

| Поле | Тип | Обязательно | Описание |
|------|-----|-------------|----------|
| `message` | string | ✅ | Сообщение (1–4096 символов) |
| `conversation_id` | string | ❌ | ID разговора (для контекста) |
| `temperature` | float | ❌ | Температура (0.0–2.0, по умолчанию 0.7) |

**Ответ:**
```json
{
  "response": "Колибри — самые маленькие птицы на Земле...",
  "confidence": 0.82,
  "conversation_id": "conv_abc123",
  "sources": ["knowledge_graph", "sentence_store"],
  "knowledge_hits": 5,
  "method": "graph+formula",
  "duration_ms": 45.2,
  "model_available": true,
  "formula_data": {
    "predict": 0.734,
    "generation": 42,
    "fitness": 0.362
  },
  "graph_stats": {
    "patterns": 1250,
    "edges": 3400
  }
}
```

### Conversation metadata

- `GET /api/v1/ai/conversations`
- `POST /api/v1/ai/conversations`
- `PATCH /api/v1/ai/conversations/{conversation_id}`
- `DELETE /api/v1/ai/conversations/{conversation_id}`
- `GET /api/v1/ai/conversations/{conversation_id}/turns`

Эти endpoints хранят список диалогов, title/pinned state и историю сообщений. Они служат серверной правдой для V3 sidebar и V3 thread.

`GET /api/v1/ai/conversations/{conversation_id}/turns` возвращает хронологический список turns и может резолвить raw client conversation id в server-scoped conversation id.

---

### `POST /api/v1/ai/train`

Обучить AI на тексте с верификацией (показывает что изменилось).

**Запрос:**
```json
{
  "text": "Колибри — самые маленькие птицы на Земле. Они умеют летать назад и зависать в воздухе.",
  "verify": true
}
```

| Поле | Тип | Обязательно | Описание |
|------|-----|-------------|----------|
| `text` | string | ✅ | Текст для обучения (10–100000 символов) |
| `verify` | bool | ❌ | Показать before/after (по умолчанию true) |

**Ответ:**
```json
{
  "status": "ok",
  "patterns": 1255,
  "edges": 3420,
  "new_patterns": 5,
  "new_edges": 20,
  "tokens": 12,
  "sample_patterns": {
    "колибри": "3815720946...",
    "птицы": "7291038465..."
  },
  "before": {"patterns": 1250, "edges": 3400},
  "after": {"patterns": 1255, "edges": 3420},
  "formula_generation": 43,
  "formula_fitness": 0.365
}
```

---

### `POST /api/v1/ai/pattern`

Получить числовой паттерн слова.

**Запрос:**
```json
{
  "word": "колибри"
}
```

**Ответ:**
```json
{
  "word": "колибри",
  "pattern": "3815720946381572094638157209463815720946381572094638157209463815",
  "hash_djb2": 2834192456,
  "hash_fnv1a": 1937281023,
  "digits": [208, 186, 208, 190, 208, 187, ...],
  "recovered_text": "колибри",
  "similar_words": [
    {"word": "птица", "similarity": 0.45},
    {"word": "летать", "similarity": 0.32}
  ]
}
```

---

### `POST /api/v1/ai/embedding`

Числовой embedding текста (на основе DJB2-паттернов).

**Запрос:**
```json
{
  "text": "Маленькая быстрая птица",
  "dimensions": 64
}
```

**Ответ:**
```json
{
  "embedding": [0.123, -0.456, 0.789, ...],
  "dimensions": 64,
  "text_length": 23,
  "pattern": "7291038465...",
  "hash_djb2": 1234567890,
  "hash_fnv1a": 987654321
}
```

---

### `GET /api/v1/ai/stats`

Статистика AI-движка.

**Ответ:**
```json
{
  "model_available": true,
  "graph_patterns": 1250,
  "graph_edges": 3400,
  "graph_documents": 15,
  "graph_tokens": 8500,
  "graph_avg_fitness": 0.36,
  "graph_avg_weight": 2.1,
  "formula_generation": 42,
  "formula_fitness": 0.362,
  "formula_genome_hex": "a3f2b7c819d4e5f6...",
  "c_model_patterns": 5000,
  "c_model_edges": 12000,
  "c_model_size_mb": 2.4,
  "c_model_documents": 50,
  "c_model_epoch": 100,
  "c_model_avg_fitness": 0.28,
  "c_model_avg_weight": 1.8,
  "active_conversations": 3,
  "sentence_store_size": 500
}
```

---

### `POST /api/v1/ai/reload`

Перезагрузить корпус и пересобрать граф знаний.

**Ответ:**
```json
{
  "corpus_loaded": true,
  "documents": 15,
  "vocab_size": 1250,
  "edges": 3400,
  "formula_generation": 42,
  "formula_fitness": 0.362
}
```

---

### `DELETE /api/v1/ai/conversations/{conv_id}`

Удалить разговор.

**Ответ:**
```json
{
  "status": "deleted",
  "conversation_id": "conv_abc123"
}
```

---

## Автономный агент — `/api/v1/agent`

### `POST /api/v1/agent/start`

Запустить автономное обучение на заданную тему.

**Запрос:**
```json
{
  "topic": "Квантовая физика",
  "max_urls": 30,
  "engines": ["ddg", "wiki", "bing"]
}
```

| Поле | Тип | Обязательно | Описание |
|------|-----|-------------|----------|
| `topic` | string | ✅ | Тема для обучения |
| `max_urls` | int | ❌ | Макс URL (5–100, по умолчанию 30) |
| `engines` | list[str] | ❌ | Поисковые системы (ddg, wiki, bing) |

**Ответ:**
```json
{
  "task_id": "task_abc123",
  "status": "started",
  "topic": "Квантовая физика"
}
```

### `GET /api/v1/agent/status/{task_id}`

Статус задачи обучения.

**Ответ:**
```json
{
  "task_id": "task_abc123",
  "status": "running",
  "progress": 0.65,
  "pages_crawled": 20,
  "pages_total": 30,
  "patterns_learned": 3500,
  "edges_learned": 8900,
  "errors": []
}
```

### `POST /api/v1/agent/stop/{task_id}`

Остановить задачу.

---

## P2P Рой — `/api/v1/swarm`

### `POST /api/v1/swarm/register`

Зарегистрировать узел в рое.

**Запрос:**
```json
{
  "address": "192.168.1.10",
  "port": 8001,
  "node_id": null,
  "patterns_count": 1250,
  "edges_count": 3400,
  "epoch": 42
}
```

**Ответ:**
```json
{
  "node_id": "node_generated_uuid",
  "status": "registered",
  "peers_count": 5
}
```

### `POST /api/v1/swarm/sync`

Синхронизация знаний с другими узлами.

**Запрос:**
```json
{
  "node_id": "node_abc123",
  "epoch": 42,
  "patterns": {"колибри": [3,8,1,5,...], "птица": [7,2,9,1,...]},
  "edges": {"колибри:птица": 3.5},
  "checksum": "sha256hex..."
}
```

**Ответ:**
```json
{
  "merged_patterns": 50,
  "merged_edges": 120,
  "total_patterns": 5000,
  "total_edges": 12000,
  "patterns": {"новое_слово": [...]},
  "edges": {"новая:связь": 1.0}
}
```

### `GET /api/v1/swarm/peers`

Список активных пиров.

### `GET /api/v1/swarm/status`

Общий статус роя.

---

## Веб-краулер — `/api/v1`

### `POST /api/v1/crawl`

Краулинг URL и обучение C-модели.

**Запрос:**
```json
{
  "url": "https://ru.wikipedia.org/wiki/Колибри",
  "mode": "url",
  "depth": 1,
  "max_pages": 10,
  "delay": 0.3
}
```

| Поле | Тип | Обязательно | Описание |
|------|-----|-------------|----------|
| `url` | string | ✅ | Seed URL |
| `mode` | string | ❌ | `url` (одна страница) / `crawl` (рекурсивно) |
| `depth` | int | ❌ | Глубина краулинга (0–5) |
| `max_pages` | int | ❌ | Макс страниц (1–200) |
| `delay` | float | ❌ | Задержка между запросами (0–5 сек) |

**Ответ:**
```json
{
  "status": "ok",
  "pages_crawled": 10,
  "patterns": 2500,
  "edges": 6000,
  "tokens": 15000,
  "model_size_mb": 1.8,
  "time_sec": 12.5
}
```

---

## GPU-хранилище — `/api/gpu`

### `POST /api/gpu/store`

Сохранить вектор в базу.

**Запрос:**
```json
{
  "path": "/docs/example.md",
  "sha256": "abc123...",
  "class": "document",
  "embedding": [0.1, 0.2, ...]
}
```

### `POST /api/gpu/search`

Cosine similarity поиск.

**Запрос:**
```json
{
  "embedding": [0.1, 0.2, ...],
  "limit": 10
}
```

### `GET /api/gpu/stats`

Статистика хранилища.

---

## Контент-фабрика — `/api/factory`

### `POST /api/factory/content`

Создать контент-запись.

### `GET /api/factory/trends`

Получить тренды.

### `GET /api/factory/analytics`

Аналитика контента.

---

## ОС-мост — `/api/dev`

### `GET /api/dev/ls?path=.`

Листинг директории (sandboxed: только `/workspaces/kolibri-project`).

**Ответ:**
```json
[
  {"name": "backend", "is_dir": true, "size": 0},
  {"name": "README.md", "is_dir": false, "size": 12345}
]
```

### `POST /api/dev/read`

Чтение файла.

**Запрос:**
```json
{"path": "README.md"}
```

### `POST /api/dev/save`

Сохранение файла.

**Запрос:**
```json
{"path": "test.txt", "content": "Hello Kolibri!"}
```

### `GET /api/dev/system`

Системная информация (psutil).

---

## LLM-прокси

### `POST /api/v1/infer`

Прокси к внешнему LLM (только если `KOLIBRI_RESPONSE_MODE=llm`).

**Запрос:**
```json
{
  "prompt": "Расскажи о Kolibri OS",
  "max_tokens": 500,
  "temperature": 0.7
}
```

**Ответ:**
```json
{
  "response": "Kolibri OS — это экспериментальная платформа...",
  "provider": "openai",
  "latency_ms": 1200.5
}
```

**Ошибка (режим отключён):**
```json
{
  "detail": "LLM mode is disabled"
}
```
(HTTP 503)

---

## Коды ошибок

| Код | Описание |
|-----|----------|
| 200 | Успех |
| 400 | Неверный запрос (валидация Pydantic) |
| 403 | Доступ запрещён (ОС-мост: путь за пределами проекта) |
| 404 | Не найдено (разговор, задача) |
| 500 | Внутренняя ошибка (AI engine, crawler) |
| 503 | LLM-режим отключён |
