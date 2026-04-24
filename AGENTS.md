# AGENTS.md

Rules for AI coding agents working in this repository.

## 1. Current product contour

The active shipping contour is:

```text
web -> services -> core -> WASM -> apps
```

Default work must stay inside these paths unless the task explicitly names another contour:

- `web/`
- `services/`
- `core/`
- `backend/include/kolibri/`
- `infra/`
- `apps/`
- `docs/`

## 2. Do not treat historical folders as shipping scope

Do not modify these by default:

- `frontend/`
- `mobile/kolibri-app`
- `cloud-storage`
- `content_factory_*`
- `swarm`
- `sdk/python`
- `kernel`
- `web-app`

They are integration-only or historical unless the task explicitly says otherwise.

## 3. Canonical commands

Bootstrap:

```bash
./infra/release_gate.sh bootstrap
```

Full gate:

```bash
./infra/release_gate.sh all
```

Make alias:

```bash
make release-gate
```

Frontend commands use `web/`:

```bash
npm run test --prefix web
npm run lint --prefix web
npm run build --prefix web
```

Backend service runs from `services.main`:

```bash
python3 -m uvicorn services.main:app --host 0.0.0.0 --port 8001
```

## 4. Source of truth docs

Read these before broad changes:

- `docs/CANONICAL_REPO_CONTOUR.md`
- `README.md`
- `docs/PRODUCT_SPEC_V2.md`
- `docs/PUBLIC_ARCHITECTURE.md`
- `docs/API_REFERENCE.md`
- `docs/QA_ACCEPTANCE.md`
- `docs/public_interfaces.md`

## 5. Engineering rules

- Keep changes small and scoped.
- Prefer fixing active shipping paths over adding new duplicated surfaces.
- Do not advertise C HTTP runtime as the shipping gateway until it passes the same contract as `services/`.
- Do not make release claims unless the release gate was run and the result is stated.
- Do not hide failing tests. State failures explicitly in PR summaries.
- Avoid broad rewrites of C core unless the task is specifically about core architecture or UB/memory safety.

## 6. Status labels

Use these terms consistently:

- `shipping`: active release contour and release gate.
- `parity-target`: official runtime path that must match shipping behavior before promotion.
- `integration-only`: secondary consumer of public interfaces.
- `experimental`: unstable surface that can change without compatibility guarantees.
