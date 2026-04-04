# Kolibri Public Architecture

> Официальный публичный архитектурный документ Kolibri.
>
> Если нужен глубокий внутренний разбор модулей, см. [ARCHITECTURE.md](ARCHITECTURE.md), [C_CORE_REFERENCE.md](C_CORE_REFERENCE.md) и [public_interfaces.md](public_interfaces.md). Этот документ является главным публичным описанием системы.

## 1. Что такое Kolibri

Kolibri — это не набор разрозненных фич, а одна система с единым контуром:

`чат / PWA -> backend gateway -> C-core -> формульная память -> геном provenance -> рой -> WASM/offline`

Главная идея проекта:

- ядро интеллекта живёт в C;
- знания выражаются через формулы, ассоциации и цифровые структуры, а не через “скрытую магию” web-обвязки;
- provenance и рой являются частью того же контура, а не отдельными экспериментами;
- WASM — это не другой продукт, а браузерный режим того же ядра.

## 2. Слои системы

```text
┌───────────────────────────────────────────────────────────────┐
│ Product Surface                                              │
│ React/PWA chat, mobile + desktop, settings, demo UX          │
│ frontend/src                                                 │
└───────────────────────────────┬───────────────────────────────┘
                                │
┌───────────────────────────────▼───────────────────────────────┐
│ Runtime Gateway                                               │
│ FastAPI routes, dialogue orchestration, demo and swarm API    │
│ backend/service                                               │
└───────────────────────────────┬───────────────────────────────┘
                                │
┌───────────────────────────────▼───────────────────────────────┐
│ C Core                                                        │
│ inference, formula pool, script runtime, genome, memory       │
│ backend/src + backend/include/kolibri                         │
└───────────────────────────────┬───────────────────────────────┘
                                │
┌───────────────────────────────▼───────────────────────────────┐
│ Knowledge + Provenance                                        │
│ formula domains, live memory, genome chain, ingest artifacts  │
│ data/formula_domains, data/swarm/live_formula_memory          │
└───────────────────────────────┬───────────────────────────────┘
                                │
┌───────────────────────────────▼───────────────────────────────┐
│ Distribution Modes                                            │
│ swarm runtime, offline wasm, CLI, backend chat                │
│ benchmarks/, apps/, frontend/public/kolibri.wasm             │
└───────────────────────────────────────────────────────────────┘
```

## 3. Официальные публичные поверхности

### 3.1 Frontend / Product Surface

- UI: `frontend/src`
- Web chat entrypoint: `frontend/src/api.ts`
- WASM bridge: `frontend/src/lib/kolibriBridge.ts`

### 3.2 Backend Gateway

- Chat API: `backend/service/ai_chat.py`
- Dialogue engine: `backend/service/ai_engine.py`
- Swarm runtime API: `backend/service/swarm_runtime_api.py`

### 3.3 C Core

- Formula engine: `backend/include/kolibri/formula.h`, `backend/src/formula.c`
- KolibriScript runtime: `backend/include/kolibri/script.h`, `backend/src/script.c`
- Logical memory: `backend/include/kolibri/logical_memory.h`, `backend/src/logical_memory.c`
- Fractal memory: `backend/include/kolibri/fractal_memory.h`, `backend/src/fractal_memory.c`
- Genome / provenance: `backend/include/kolibri/genome.h`, `backend/src/genome.c`
- Browser core bridge: `backend/src/wasm_bridge.c`

### 3.4 CLI / Tooling

- Inference CLI: `apps/kolibri_infer_cli.c`
- Formula ingest trainer: `apps/kolibri_formula_trainer.c`

## 4. Один системный story вместо набора фич

### 4.1 Learn Path

1. Пользователь или ingest-утилита подают текст/URL.
2. Backend сохраняет материал в live formula memory.
3. C-core использует ассоциации и формулы как рабочую память.
4. Refresh роя пересчитывает `1 vs 10` и распространяет выигрыш по узлам.
5. Следующий ответ чата опирается уже на обновлённую память.

### 4.2 Answer Path

1. UI отправляет запрос.
2. Backend выбирает маршрут: `c-core-formula`, `dialog-context`, `math-eval`, `weather`, `self-meta`, `demo`, `vision` и т.д.
3. Если используется C-core, запрос проходит через formula/logical/inference pipeline.
4. Ответ возвращается пользователю вместе с методом и метриками.

### 4.3 Offline / WASM Path

1. Фронтенд загружает `kolibri.wasm`.
2. WASM bridge обращается к тому же script/formula runtime, что и backend, только в браузере.
3. Если автономный ответ слабый, UI может переключиться на backend path.

### 4.4 Provenance Path

1. Формула, ответ или знание могут быть сохранены в геном.
2. Геном фиксирует происхождение и историю изменений.
3. Этот же provenance-контур является источником доверия для роя и обучающего demo-path.

## 5. Как рой, provenance, offline и WASM связаны между собой

- `Рой` ускоряет распространение полезной формульной памяти.
- `Provenance` объясняет, откуда пришли формулы и ответы.
- `WASM` даёт локальный браузерный запуск того же ядра.
- `Offline` означает, что часть сценариев работает без облака и без внешнего LLM как обязательного центра системы.

Это не четыре разные истории. Это одна архитектура Kolibri:

`same core -> different runtime surfaces`

## 6. Официальный demo-path

Официальный demo-path для публичной демонстрации Kolibri:

1. Добавить новое знание.
2. Принудительно пересчитать рой.
3. Сравнить `1 узел` и `10 узлов`.
4. Задать вопрос по только что добавленному знанию.
5. Показать, что ответ идёт через `c-core-formula`, а преимущество роя выросло.

Подробная инструкция: [DEMO_PATH.md](DEMO_PATH.md)

Воспроизводимый скрипт: `scripts/demo_public_path.sh`

## 7. Честный статус на текущий момент

| Контур | Статус | Что это значит |
|---|---|---|
| `C-core formula answers` | implemented | короткие и средние русские ответы по доменным знаниям уже идут через `c-core-formula` |
| `Swarm runtime` | implemented | есть живой runtime, refresh, demo ingest и сравнение `1 vs 10` |
| `WASM path` | implemented with fallback | автономный путь есть, но качество ниже backend path |
| `Provenance / genome` | implemented partially in public flow | геном и журнал есть в ядре, но не весь product UX ещё строится вокруг provenance |
| `Knowledge pack (.kpack)` | implemented | есть публичная спецификация, CLI-утилита и runtime import/export endpoints |

## 8. Официальный набор публичных документов

- [PUBLIC_ARCHITECTURE.md](PUBLIC_ARCHITECTURE.md) — главный публичный архитектурный документ
- [PRODUCT_SPEC_V2.md](PRODUCT_SPEC_V2.md) — каноническая продуктовая спецификация
- [API_REFERENCE.md](API_REFERENCE.md) — боевые API-контракты
- [FORMULA_API_DSL.md](FORMULA_API_DSL.md) — публичный Formula API и честный DSL
- [KPACK_FORMAT.md](KPACK_FORMAT.md) — пакетный формат знаний
- [DEMO_PATH.md](DEMO_PATH.md) — один стабильный demo-path
- [SWARM_50_ARCHITECTURE.md](SWARM_50_ARCHITECTURE.md) — целевая архитектура production swarm
- [NUMERIC_VOTING_MODEL.md](NUMERIC_VOTING_MODEL.md) — decision layer `0..9`
- [MORPHOLOGY_SEMANTICS_SPEC.md](MORPHOLOGY_SEMANTICS_SPEC.md) — морфология и семантика
- [QA_ACCEPTANCE.md](QA_ACCEPTANCE.md) — release gates
- [DEPLOY_RUNBOOK.md](DEPLOY_RUNBOOK.md) — runbook выкладки и rollback
- [public_interfaces.md](public_interfaces.md) — перечень стабильных интерфейсов
