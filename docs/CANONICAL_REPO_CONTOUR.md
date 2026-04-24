# Kolibri Canonical Repo Contour

This document fixes the active product contour for the current Kolibri release work. It exists to prevent agents, contributors and release notes from treating every historical directory as equal shipping scope.

## 1. Active shipping contour

The current canonical product contour is:

```text
web -> services -> core -> WASM -> apps
```

### `web/`

Current shipping web shell.

Responsibilities:

- chat-first UI;
- browser/runtime shell;
- WASM bridge integration;
- product smoke, lint and build for the frontend release gate.

Primary entrypoints:

- `web/src/App.tsx`
- `web/src/lib/kolibriBridge.ts`
- `web/package.json`

### `services/`

Current shipping FastAPI gateway.

Responsibilities:

- health endpoints;
- auth/account/profile/preferences;
- chat and streaming chat;
- conversations;
- swarm runtime;
- `.kpack` import/export;
- operator-facing compatibility routes when explicitly enabled.

Primary entrypoints:

- `services/main.py`
- `services/ai_chat.py`
- `services/ai_engine.py`
- `services/swarm_runtime_api.py`

### `core/`

Native C23 core.

Responsibilities:

- formula/runtime logic;
- inference;
- logical memory;
- knowledge primitives;
- compression;
- WASM bridge implementation;
- native HTTP parity target.

Primary entrypoints:

- `core/wasm_bridge.c`
- `core/kolibri_http_server.c`
- `core/formula.c`
- `core/inference.c`
- `core/knowledge.c`
- `core/logical_memory.c`

### `backend/include/kolibri/`

Stable public C header surface for the current product contour.

Consumer code must use documented public headers from this tree, not arbitrary internal headers from `core/`.

### `infra/`

Release and build automation.

Primary entrypoints:

- `infra/release_gate.sh`
- `infra/build_wasm.sh`
- `infra/tests/`

### `apps/`

Product-side native utilities.

Shipping-significant utilities are documented in `docs/public_interfaces.md`. Other binaries can exist, but are not automatically part of the stable public surface.

## 2. Parity target

`core/kolibri_http_server.c` is an official runtime path, but it is not the current shipping gateway until it passes the same product contract and release gate as `services/`.

Status: `parity-target`.

## 3. Integration-only and historical contours

The following directories may stay in the repository, but they are not part of the nearest shipping scope unless a future document explicitly promotes them:

- `frontend/`
- `mobile/kolibri-app`
- `cloud-storage`
- `content_factory_*`
- `swarm`
- `sdk/python`
- `kernel`
- `web-app`

Agents must not expand release scope by editing these directories unless the task explicitly asks for it.

## 4. Release-gate command

Canonical gate:

```bash
./infra/release_gate.sh all
```

Make alias:

```bash
make release-gate
```

The gate is authoritative over claims. If the gate is red or not run, release notes must say so.

## 5. Naming rule

Use the current paths in new docs, prompts and PRs:

- use `web/`, not `frontend/src`, for the active web shell;
- use `services/`, not `backend/service`, for the active FastAPI gateway;
- use `infra/release_gate.sh`, not `scripts/release_gate.sh`, for the active gate;
- use `core/kolibri_http_server.c`, not `backend/src/kolibri_http_server.c`, for the native HTTP parity target.

## 6. Agent rule

Before implementing features, agents must first classify the requested change:

- `shipping`: touches `web/`, `services/`, `core/`, `backend/include/kolibri/`, `apps/`, `infra/`;
- `parity-target`: specifically improves `core/kolibri_http_server.c` toward the services contract;
- `integration-only`: touches secondary contours;
- `experimental`: introduces new surfaces not covered by this document.

When in doubt, prefer smaller changes inside the active shipping contour and update this document only with explicit intent.
