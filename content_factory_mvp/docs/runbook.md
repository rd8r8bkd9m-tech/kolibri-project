# Runbook — Content Factory MVP

## Локальный запуск
```
cd content_factory_mvp
make demo
```

## Сервисы
- API: http://localhost:8000
- UI: http://localhost:3000
- DB: localhost:5432

## Проверка здоровья
- GET /api/health

## Демо‑прогон
`make demo` выполняет:
1) docker compose up
2) seed данных
3) demo pipeline (script → production → publish → metrics → ROMI)

## Просмотр артефактов
Артефакты лежат в `content_factory_mvp/data/assets/{content_id}`:
- subtitles.srt
- thumbnail.png
- metadata.json
- render_stub.mp4

## TODO
- Подключить реальные источники YouTube/GA/CRM
- Вынести в асинхронные воркеры Celery
