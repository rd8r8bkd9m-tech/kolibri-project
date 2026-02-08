# Backlog — 2 спринта (RU)

## Sprint 1 (SEO‑MVP)
1) **Схема БД и миграции**
- Acceptance: `alembic upgrade head` без ошибок, таблицы созданы.
2) **CRUD базовых сущностей**
- Acceptance: POST/GET 200 по workspace/brand/site/channel/competitors.
3) **Source collect (mock)**
- Acceptance: /sources/collect возвращает list с score+rationale.
4) **Ideas generate (mock)**
- Acceptance: /ideas/generate создает >= limit.
5) **Package create + SEO article**
- Acceptance: seo_article_md, schema_json, internal_links_json заполнены.
6) **Render artifacts**
- Acceptance: mp4/png/srt/md/json присутствуют на диске.

## Sprint 2 (Publishing + Analytics)
1) **Approval + audit trail**
- Acceptance: approvals сохраняются и видны.
2) **Scheduling + Publish adapters**
- Acceptance: публикация пишет payload_json (wp+yt). 
3) **Metrics + ROMI**
- Acceptance: metrics_daily и attribution создаются.
4) **Dashboard summary**
- Acceptance: avg ROMI считается.
5) **UI канбан**
- Acceptance: пакеты отображаются по статусам.
