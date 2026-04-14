# Kolibri Release Notes

## Draft release: 0.2.0 shipping contour stabilization

Эти release notes считаются черновиком до тех пор, пока release gate не зелёный и evidence pack не собран.

## Что входит в релиз

- один shipping web shell из `frontend/src`
- один shipping backend gateway из `backend/service`
- native core из `backend/src` и `backend/include/kolibri`
- browser/offline artifact `kolibri.wasm`
- product-side CLI utilities из `apps/`
- единый release gate и release evidence bundle

## Что изменено в этом цикле

- документация сведена к одному source of truth для shipping-контура;
- release gate вынесен в `scripts/release_gate.sh`;
- `Makefile`, `scripts/run_all.sh` и GitHub Actions разделены на `Release Gate` и `Extended CI`;
- public docs больше не рекламируют secondary contours как часть ближайшего shipping scope;
- FastAPI явно закреплён как текущая shipping truth, а C HTTP runtime помечен как `parity-target`.

## Что не входит в релизный scope

- `frontend/kolibri-web`
- `mobile/kolibri-app`
- `cloud-storage`
- `content_factory_*`
- `swarm`
- `sdk/python`
- `kernel`
- `web-app`

Эти контуры остаются в репозитории как `integration-only` или `experimental`.

## Release evidence required

Релизные заявления допустимы только при наличии:

- зелёного `release-gate`
- актуального `docs/QA_ACCEPTANCE.md`
- актуального `docs/DEPLOY_RUNBOOK.md`
- собранного `kolibri.wasm`
- собранного `frontend/dist`
