# Kolibri QA Acceptance

## 1. Canonical rule

Release gate для Kolibri определяется только через active shipping contour:

`frontend/src + backend/service + backend/src + WASM + apps`

Никакой release note, статус-апдейт или demo claim не считается честным, если release gate этого контура не зелёный.

## 2. Bootstrap

Минимальный локальный bootstrap:

```bash
./scripts/release_gate.sh bootstrap
```

Это должно установить:

- Python dependencies из `requirements.txt`
- frontend dependencies из `frontend/package-lock.json`

## 3. Release Gate

### 3.1 Native runtime

Команда:

```bash
./scripts/release_gate.sh native
```

Состав:

- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DKOLIBRI_ENABLE_TESTS=ON`
- `cmake --build build`
- `python3 scripts/check_ctest_inventory.py --build-dir build`
- `ctest --test-dir build -R 'test_kolibri_http_server_api|test_kolibri_http_stream_api|test_kolibri_http_phase1_benchmark' --output-on-failure`

Release-blocking native tests:

- `test_kolibri_http_server_api`
- `test_kolibri_http_stream_api`
- `test_kolibri_http_phase1_benchmark`

### 3.2 Backend Python

Команда:

```bash
./scripts/release_gate.sh backend
```

Targeted pytest suite:

- `tests/test_auth.py`
- `tests/test_backend_service.py`
- `tests/test_common.py`
- `tests/test_context_window.py`
- `tests/test_e2e_api.py`
- `tests/test_kpack.py`
- `tests/test_persistence.py`
- `tests/test_rate_limiter.py`
- `tests/test_realtime_lookup.py`
- `tests/test_reasoning.py`
- `tests/test_swarm_runtime_api.py`
- `tests/test_ai_engine_integration.py`

### 3.3 WASM

Команда:

```bash
./scripts/release_gate.sh wasm
```

Acceptance:

- `build/wasm/kolibri.wasm` существует
- `frontend/public/kolibri.wasm` обновлён
- `frontend/public/kolibri.wasm.sha256` и `frontend/public/kolibri.wasm.txt` обновлены

### 3.4 Frontend

Команда:

```bash
./scripts/release_gate.sh frontend
```

Состав:

- `npm run test --prefix frontend`
- `npm run lint --prefix frontend`
- `npm run build --prefix frontend`

### 3.5 Combined gate

```bash
./scripts/release_gate.sh all
```

или

```bash
make release-gate
```

## 4. Manual acceptance scenarios

Следующие сценарии относятся к shipping contour и должны быть воспроизводимы перед релизом:

1. Frontend shell открывается на desktop и mobile breakpoints без overlap и horizontal overflow.
2. `auth/status`, login/logout и profile/preferences roundtrip работают через backend.
3. Conversation CRUD и turns являются server source of truth.
4. Chat basic, follow-up, stream/stop и runtime preferences проходят без broken state.
5. Workspace позволяет выполнять swarm runtime status, ingest, refresh и `.kpack` import/export.
6. C runtime smoke и phase-1 benchmark проходят через CTest.
7. WASM path собирается и поставляется во frontend assets; fallback path задокументирован.

## 5. Extended CI

Extended CI не расширяет официальный release scope. Он даёт дополнительные сигналы по более широкому историческому контуру.

| Job | Classification | Notes |
|---|---|---|
| `extended-security-sast` | advisory | security signal для backend/service |
| `extended-fuzz-parser` | advisory | parser/libFuzzer smoke |
| `extended-benchmark-regression` | advisory | broader native performance signal |
| `extended-iso-package` | advisory | legacy/native packaging artifact |
| `extended-docker-smoke` | advisory | packaging/deploy smoke |
| `extended-run-all-smoke` | advisory | historical umbrella smoke |

Если extended job красный, это важно зафиксировать, но сам по себе он не должен использоваться как доказательство, что shipping contour "не существует".

## 6. Acceptance alignment rule

Эта страница является каноническим текстовым описанием gate.

Следующие места обязаны ей соответствовать:

- `scripts/release_gate.sh`
- `Makefile`
- `scripts/run_all.sh`
- `.github/workflows/ci.yml`
- `docs/DEPLOY_RUNBOOK.md`
- `docs/RELEASE_CHECKLIST.md`
