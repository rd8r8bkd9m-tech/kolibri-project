# Content Factory — задачи разработки (RU)

## P0 — Инфраструктура и база
1. **Схема БД (MVP)**
   - Таблицы: content_items, trend_insights, video_references, analytics_snapshots.
   - Индексы по niche, status, content_item_id.
2. **API‑контракты**
   - /api/factory/trends/analyze
   - /api/factory/videos/best
   - /api/factory/items/{id}/analytics
   - /api/factory/items/{id}/analytics/snapshot
3. **UI‑интеграция**
   - Панель Trend Agent
   - Панель Best Video Finder
   - Отображение ROMI и базовых метрик

---

## P0 — Trend Agent
4. **Сбор трендов (MVP‑mock)**
   - Генерация 5–10 трендов с score и rationale.
   - Хранение и выдача в UI.
5. **Скоринг**
   - Базовый скоринг (mock), интерфейс для замены на реальный анализ.

---

## P0 — Best Video Finder
6. **Список лучших видео**
   - Генерация ссылок и reason.
   - Отображение карточек с ссылкой в UI.

---

## P0 — Analytics & ROMI
7. **Снимки аналитики**
   - CRUD: запись snapshot + список.
8. **ROMI**
   - Формула ROMI, хранение в snapshots и content_items.

---

## P1 — Продвинутые функции
9. **Источники трендов**
   - YouTube/TikTok API, Google Trends, CRM.
10. **Видео‑анализ**
   - STT транскрипт + выделение hook/CTA.
11. **Атрибуция**
   - UTM, CRM‑линковка, инкрементальные тесты.
