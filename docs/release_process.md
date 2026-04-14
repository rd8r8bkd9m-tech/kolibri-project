# Kolibri Release Process

## 1. Goal

Релиз Kolibri должен доказывать один shipping contour, а не весь исторический репозиторий.

Official release evidence pack состоит из:

- `README.md`
- `docs/PRODUCT_SPEC_V2.md`
- `docs/API_REFERENCE.md`
- `docs/PUBLIC_ARCHITECTURE.md`
- `docs/public_interfaces.md`
- `docs/QA_ACCEPTANCE.md`
- `docs/DEPLOY_RUNBOOK.md`
- `docs/RELEASE_CHECKLIST.md`
- `docs/INTEGRATION_SURFACES.md`
- `docs/release_notes.md`
- `build/wasm/kolibri.wasm`
- `frontend/dist`

## 2. Release gate

Релиз не начинается, пока не зелёны:

```bash
./scripts/release_gate.sh all
```

или

```bash
make release-gate
```

## 3. Extended CI

`make extended-ci` и extended jobs в GitHub Actions относятся к более широкому quality contour. Они дают дополнительные сигналы, но не подменяют release gate.

## 4. CI evidence

GitHub Actions делит CI на два слоя:

- `release-gate-*` jobs
- `extended-*` jobs

`release-bundle` job собирает release evidence только после зелёных `release-gate-*`.

## 5. Local release flow

1. Выполнить bootstrap:

```bash
./scripts/release_gate.sh bootstrap
```

2. Выполнить release gate:

```bash
./scripts/release_gate.sh all
```

3. Проверить документы из evidence pack.
4. Проверить production build frontend и актуальность `kolibri.wasm`.
5. Подготовить или скачать CI release evidence bundle.
6. Выполнить deploy по `docs/DEPLOY_RUNBOOK.md`.

## 6. Artifact policy

- `kolibri.wasm` и `frontend/dist` являются release artifacts.
- ISO, docker smoke, fuzz, broader benchmark и прочие дополнительные материалы относятся к extended contour.
- Наличие extended artifacts не должно использоваться как замена зелёному release gate.
