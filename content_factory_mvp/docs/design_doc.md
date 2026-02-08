# Content Factory MVP — System Design Doc (RU)

## 1) Цели MVP
**Контент‑конвейер** от анализа до публикации и ROMI с human‑in‑the‑loop.

Стадии:
1. Анализ ниши/аудитории/конкурентов
2. Поиск лучших видео (референсы)
3. Генерация идей (hook/angle/CTA/format/funnel stage)
4. Утверждение идеи (OK/правки/стоп)
5. Генерация сценария и раскадровки
6. Производство (subtitles/thumbnail/metadata/render stub)
7. Утверждение финала
8. Публикация (YouTube Shorts, mock adapter)
9. Метрики + ROMI/ROAS
10. Рекомендации для масштабирования

**Важно:** Все операции безопасны, с guardrails и audit trail.

---

## 2) Архитектура компонентов

**Backend (FastAPI)**
- API слой
- Orchestrator (state machine)
- Сервисы (Trend, Video Finder, Ideation, Script, Production, Guardrails, Publisher, Analytics)
- Storage (Postgres)
- Assets (локальная папка, интерфейс под S3)

**Worker (Celery + Redis)**
- Очереди: trend, ideation, production, analytics
- Ретраи/идемпотентность задач

**UI (server‑rendered FastAPI + Jinja)**
- Канбан
- Карточка контента
- Кнопки approve/reject

**Infra**
- docker‑compose (postgres + redis + backend + worker + ui)

---

## 3) Границы модулей

### A) Trend/Market Analyzer
- Вход: niche, competitors
- Выход: TrendInsight[]
- MVP: мок‑данные с объяснением

### B) Best Video Finder
- Вход: niche
- Выход: SourceVideo[] + score + rationale
- MVP: мок‑датасет + scoring

### C) Ideation
- Вход: Trend + SourceVideo
- Выход: Idea[] (hook/angle/cta)

### D) Script/Storyboard
- Вход: Idea
- Выход: script + storyboard_json

### E) Production
- Вход: script
- Выход: subtitles.srt, thumbnail.png, metadata.json, render_stub.mp4

### F) Guardrails
- Вход: script + BrandGuidelines
- Выход: risk_score + flags + required_disclaimers

### G) Publisher
- Вход: ContentItem + metadata
- Выход: Publication (mock URL)

### H) Analytics + ROMI
- Вход: Publication
- Выход: MetricsDaily + Attribution

---

## 4) Статусы ContentItem
```
analysis → idea_approval → production → content_approval → publishing → analytics → done
```

Переходы контролирует Orchestrator, все переходы логируются.

---

## 5) Idempotency, retries, timeouts
- Все задачи принимают `content_item_id` и проверяют статус.
- Повторные запуски не создают дубликаты.
- Celery retry policy: 3 попытки, экспоненциальная задержка.

---

## 6) Guardrails
- Запрещены чужие ассеты (только анализ).
- Факт‑чекинг: числовые утверждения без источника → риск ↑
- Prompt injection: санитайзинг входных данных
- Бренд‑тон: banned phrases + required disclaimers

---

## 7) Компромиссы MVP
- Mock‑адаптер YouTube Shorts
- Mock‑метрики и мок‑референсы
- Локальное хранилище ассетов

**TODO:** интеграция реальных источников (YouTube API/GA/CRM)

---

## 8) Диаграмма взаимодействий (High Level)
```
UI → API → Orchestrator → Service → DB/Assets
                     ↘ Celery Worker ↗
```

---

## 9) Структура репозитория
```
content_factory_mvp/
  backend/
  worker/
  ui/
  infra/
  docs/
  docker-compose.yml
  Makefile
```

---

## 10) Security
- CORS open for MVP (TODO: restrict)
- No external writes without approvals

---

## 11) Демонстрация
Одна команда `make demo` поднимает сервисы и прогоняет pipeline end‑to‑end.
