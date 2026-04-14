# Kolibri Integration Surfaces

Этот документ описывает контуры, которые остаются в репозитории, но не входят в ближайший shipping release.

## Status rule

Все перечисленные ниже контуры имеют статус `integration-only` или `experimental`, если не сказано иное.

## Catalog

| Contour | Status | Main interface to shipping contour | Notes |
|---|---|---|---|
| `frontend/kolibri-web` | integration-only | auth/account/preferences and related backend contracts | отдельный web contour, не часть shipping shell |
| `mobile/kolibri-app` | integration-only | auth/chat/account API | мобильный клиент может потреблять shipping backend, но не входит в release gate |
| `cloud-storage` | integration-only | storage/files and ingest-adjacent flows | рассматривается как внешний сервисный контур |
| `content_factory_mvp` | integration-only | knowledge ingest / generated content handoff | не входит в официальный product narrative |
| `content_factory_mvp2` | integration-only | knowledge ingest / generated content handoff | то же правило |
| `swarm` | integration-only | swarm-related data and benchmark surfaces | не равен shipping swarm runtime API |
| `sdk/python` | integration-only | HTTP/C interfaces | SDK может использовать shipping interfaces, но не задаёт релизный scope |
| `kernel` | experimental | none required | исторический/отдельный contour, не релизный |
| `web-app` | experimental | none required | отдельный web contour вне текущего scope |

## Rule of use

Эти контуры допустимо развивать, пока соблюдаются два правила:

1. они не переопределяют canonical product docs;
2. они не объявляются частью shipping release, пока не попадают в тот же release gate.
