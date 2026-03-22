# Calibri AI — Фаза 2: Базовые метрики качества

Дата: 2026-02-18

## Источники
1. `/Users/kolibri/kolibri-project/docs/reports/chat_latency_baseline_20260218.json`
2. `/Users/kolibri/kolibri-project/docs/reports/chat_stream_ttfb_baseline_20260218.json`
3. `/Users/kolibri/kolibri-project/docs/reports/autopilot_release_20260218_235343.json`

## Результаты
1. API `POST /api/v1/ai/chat`:
   - p50: `48556.64 ms`
   - p95: `58375.12 ms`
   - mean: `51558.54 ms`
2. API `POST /api/v1/ai/chat/stream` (время до первого `data:`):
   - mean TTFB: `1875.73 ms`
   - max TTFB: `2172.49 ms`
3. Статус кодов: `200` для всех запусков.

## Вывод
1. Интерактивность уже приемлема через streaming (первый токен < 2.2s).
2. Полный ответ пока слишком долгий для production SLA.

## Задачи оптимизации (следующий цикл)
1. Урезать контекст в backend для коротких запросов.
2. Добавить жёсткий тайм-бюджет генерации (например 10-15s) и ранний stop.
3. Кэширование ответов по нормализованному prompt hash.
4. Раздельные профили: `fast` (короткий), `deep` (длинный).
