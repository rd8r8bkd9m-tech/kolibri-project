# Calibri AI — Отчёт по Фазе 1

Дата: 2026-02-18

## Что сделано
1. Фронтенд продеплоен на `kolibriai.ru` с русским интерфейсом.
2. Исправлен мобильный UX: поле ввода чата отображается над нижней навигацией.
3. Фронтенд переведён на реальные AI-эндпоинты backend:
   - streaming: `/api/v1/ai/chat/stream`
   - fallback: `/api/v1/ai/chat`
4. Добавлен унифицированный скрипт деплоя фронтенда:
   - `scripts/deploy_kolibriai.sh`
5. Добавлен скрипт деплоя backend на Ubuntu:
   - `scripts/deploy_backend_ubuntu.sh`
6. Добавлен smoke-тест прода:
   - `scripts/smoke_kolibriai.py`

## Текущее состояние прода
1. Фронтенд обслуживается из `/var/www/kolibri`.
2. Nginx проксирует `/api/*` на backend.
3. backend-чаты отвечают через реальный движок Kolibri AI.
4. Единый автопилотный релиз выполняется скриптом:
   - `scripts/autopilot_release.sh`

## Риски
1. На сервере есть 2 backend-инстанса из разных путей (`/srv/kolibri/repo` и `/home/ladik/kolibri-project`), что может давать дрейф версий.
2. Нужна полная консолидация в один production-инстанс (Фаза 2).
3. Полная задержка ответа `/api/v1/ai/chat` пока высокая (десятки секунд).

## Следующие шаги (Фаза 2)
1. Консолидация backend в один сервис + единый process manager.
2. Авто-smoke после каждого деплоя.
3. Метрики качества: latency p50/p95, success rate, длина ответа.
4. Оптимизация latency для sync-chat endpoint.
