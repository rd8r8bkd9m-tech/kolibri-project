# Kolibri QA Acceptance

## 1. Canonical rule

Release gate для Kolibri определяется только через active shipping contour:

`web + services + core + backend/include/kolibri + WASM + apps`

Никакой release note, статус-апдейт или demo claim не считается честным, если release gate этого контура не зелёный или если результат gate не указан явно.

## 2. Bootstrap

Минимальный локальный bootstrap:

```bash
./infra/release_gate.sh bootstrap
```

Это должно установить:

- Python dependencies из `requirements.txt`
- frontend dependencies из `web/package-lock.json` / `web/package.json`

## 3. Release Gate

### 3.1 Native runtime

Команда:

```bash
./infra/release_gate.sh native
```

Состав:

- CMake configure/build для native targets;
- `python3 infra/check_ctest_inventory.py --build-dir build`;
- release-blocking CTest inventory из `infra/release_gate.sh`.

Release-blocking native tests определяются regex в `infra/release_gate.sh`. Если список меняется, этот документ и `docs/RELEASE_CHECKLIST.md` должны быть обновлены в том же PR.

### 3.2 Backend Python

Команда:

```bash
./infra/release_gate.sh backend
```

Targeted pytest suite хранится в `infra/release_gate.sh` и должна покрывать:

- auth;
- account/profile/preferences;
- common backend behavior;
- context window;
- e2e API;
- `.kpack`;
- persistence;
- rate limiting;
- realtime lookup;
- reasoning;
- swarm runtime;
- AI engine integration.

### 3.3 WASM

Команда:

```bash
./infra/release_gate.sh wasm
```

Acceptance:

- `kolibri.wasm` собирается через `infra/build_wasm.sh`;
- web public artifacts обновляются для browser/offline runtime path;
- WASM bridge остаётся совместим с `web/src/lib/kolibriBridge.ts`.

### 3.4 Frontend

Команда:

```bash
./infra/release_gate.sh frontend
```

Состав:

- `npm run test --prefix web`
- `npm run lint --prefix web`
- `npm run build --prefix web`

### 3.5 Combined gate

```bash
./infra/release_gate.sh all
```

или

```bash
make release-gate
```

## 4. Manual acceptance scenarios

Следующие сценарии относятся к shipping contour и должны быть воспроизводимы перед релизом:

1. Web shell открывается на desktop и mobile breakpoints без overlap и horizontal overflow.
2. `auth/status`, login/logout и profile/preferences roundtrip работают через backend.
3. Conversation CRUD и turns являются server source of truth.
4. Chat basic, follow-up, stream/stop и runtime preferences проходят без broken state.
5. Workspace позволяет выполнять swarm runtime status, ingest, refresh и `.kpack` import/export.
6. C runtime smoke и release-blocking native tests проходят через CTest.
7. WASM path собирается и поставляется во web assets; fallback path задокументирован.

## 5. Extended CI

Extended CI не расширяет официальный release scope. Он даёт дополнительные сигналы по более широкому историческому контуру.

| Job | Classification | Notes |
|---|---|---|
| `extended-security-sast` | advisory | security signal для services |
| `extended-fuzz-parser` | advisory | parser/libFuzzer smoke |
| `extended-benchmark-regression` | advisory | broader native performance signal |
| `extended-iso-package` | advisory | legacy/native packaging artifact |
| `extended-docker-smoke` | advisory | packaging/deploy smoke |
| `extended-run-all-smoke` | advisory | historical umbrella smoke |

Если extended job красный, это важно зафиксировать, но сам по себе он не должен использоваться как доказательство, что shipping contour не существует.

## 6. Acceptance alignment rule

Эта страница является каноническим текстовым описанием gate вместе с `docs/CANONICAL_REPO_CONTOUR.md`.

Следующие места обязаны ей соответствовать:

- `infra/release_gate.sh`
- `Makefile`
- `scripts/run_all.sh`, если используется;
- `.github/workflows/ci.yml`, если используется;
- `docs/DEPLOY_RUNBOOK.md`
- `docs/RELEASE_CHECKLIST.md`
- `AGENTS.md`
