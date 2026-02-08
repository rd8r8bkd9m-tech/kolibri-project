# Backlog — 2 спринта (RU)

## Sprint 1 (Инфра + базовый конвейер)
1. **База и миграции**
   - Acceptance: `alembic upgrade head` без ошибок, все таблицы созданы.
2. **CRUD базовых сущностей** (Workspace, BrandGuidelines, Channel, Competitor)
   - Acceptance: POST/GET 200, данные сохраняются.
3. **SourceVideo collect (mock)**
   - Acceptance: /source-videos/collect возвращает список с score+rationale.
4. **Ideas generate (mock)**
   - Acceptance: /ideas/generate возвращает >= limit идей.
5. **ContentItems create/generate-script/produce**
   - Acceptance: создаются артефакты subtitles/thumbnail/metadata/render_stub.
6. **Approve flow + audit trail**
   - Acceptance: Approval записи сохраняются, статус меняется.
7. **Publication + metrics + ROMI**
   - Acceptance: metrics и attribution создаются и читаются.
8. **UI Канбан**
   - Acceptance: видит статусы, карточки из API.

## Sprint 2 (Guardrails + рекомендации)
1. **Guardrails**
   - Acceptance: risk_score растет при banned/forbidden/numeric.
2. **Explainable scoring**
   - Acceptance: rationale_json содержит вклад факторов.
3. **Dashboard summary**
   - Acceptance: /dashboard/summary считает avg ROMI.
4. **Demo pipeline (make demo)**
   - Acceptance: end‑to‑end прогон с артефактами.
5. **Документация OpenAPI**
   - Acceptance: openapi.yaml актуален.
