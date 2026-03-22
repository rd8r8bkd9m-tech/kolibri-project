# Kolibri Project

Kolibri — проект по созданию ИИ-системы с числовым мышлением и KLM-базой знаний.

## Текущий фокус

- Единый продукт: AI чат + backend + KLM knowledge runtime.
- Упорядочивание структуры репозитория и документации.
- Воспроизводимая разработка на Ubuntu с обязательной синхронизацией в GitHub.

## Что в репозитории активно

- `backend/service/` — FastAPI backend (чат, обучение, когниция, рой, health, auth).
- `backend/src/` — C-модули ядра (semantic/context/attention/world_model и др.).
- `frontend/` — активный чатовый интерфейс (mobile + desktop, light/dark).
- `docs/plans/ROADMAP_SINGLE_SOURCE_OF_TRUTH.md` — единый roadmap со статусами.

## Что выведено из активного контура

- `backend/service/archive/content_factory.py` — временно исключен из backend runtime.
- `docs/archive/unconfirmed_reports/` — отчеты, требующие повторной верификации.

## Быстрый старт (Ubuntu)

```bash
cd ~/kolibri-project
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt

python -m uvicorn backend.service.main:app --host 0.0.0.0 --port 8001
# в другом терминале
cd frontend && npm ci && npm run dev -- --host 0.0.0.0 --port 3000
```

## API

- `POST /api/v1/ai/chat` — диалог.
- `GET /api/v1/ai/models` — состояние загруженной KLM-модели.
- `GET /api/v1/ai/stats` — статистика движка.

## Дорожная карта

Единый источник правды:

- `docs/plans/ROADMAP_SINGLE_SOURCE_OF_TRUTH.md`

## Official Public Surface

Если нужен не весь архив мыслей проекта, а один официальный публичный контур, опирайся на этот набор:

- [docs/PUBLIC_ARCHITECTURE.md](docs/PUBLIC_ARCHITECTURE.md) — главный публичный архитектурный документ
- [docs/FORMULA_API_DSL.md](docs/FORMULA_API_DSL.md) — публичный Formula API и реальный DSL поверх C-core
- [docs/KPACK_FORMAT.md](docs/KPACK_FORMAT.md) — пакетный формат знаний `.kpack`
- [docs/DEMO_PATH.md](docs/DEMO_PATH.md) — один стабильный demo-path
- [docs/public_interfaces.md](docs/public_interfaces.md) — стабильные публичные интерфейсы

Внутренние глубокие справочники при этом остаются доступными:

- `docs/ARCHITECTURE.md`
- `docs/C_CORE_REFERENCE.md`
- `docs/FRONTEND_WASM_REFERENCE.md`

Правило статусов:

- `implemented` = код + тест + воспроизводимый запуск.
- `in_progress` = частичная реализация.
- `planned` = только план/черновик.
