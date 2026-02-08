# Контент‑завод 2.0 — System Design (RU)

## Цель MVP
Приоритет: **Сайт + SEO**, затем мультиканальный посев (видео/посты). Единый Master Content Package → Render Profiles → публикация/аналитика.

## Компоненты
- **Backend (FastAPI)**: API, оркестратор, сервисы (Trend, Best Content, Ideation, SEO, Renderers, Guardrails, Publisher, Analytics).
- **Worker (Celery + Redis)**: асинхронные задачи пайплайна.
- **DB (Postgres + Alembic)**: хранение контента и метрик.
- **Storage**: локальная папка `/data/assets` + интерфейс под S3.
- **UI**: минимальный админ‑канбан + карточка пакета.

## Статусы ContentPackage
```
analysis → idea_approval → package_build → qa_approval → scheduling → publishing → analytics → done
```

## Idempotency
- Задачи принимают `package_id`.
- Перед действием проверяют статус и наличие артефактов.

## Guardrails
- Санитайзинг входных данных (prompt‑injection).
- Запрещённые фразы и обещания → risk_score ↑.
- Числовые утверждения без источников → mandatory approval.

## Компромиссы MVP
- YouTube/WordPress — адаптеры с мок‑реализациями.
- Метрики — мок‑генератор + интерфейс под реальных коннекторов.

## Репо
```
content_factory_mvp2/
  backend/
  worker/
  ui/
  docs/
  docker-compose.yml
  Makefile
```
