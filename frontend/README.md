# Kolibri Frontend

`frontend/src` является единственным shipping web shell проекта Kolibri.

Этот README описывает только активный product contour. Каталог `frontend/kolibri-web` не относится к ближайшему release scope и рассматривается как `integration-only`.

## Что входит в shipping frontend

- `src/App.tsx` — корневой product shell
- `src/api.ts` — canonical web client для backend surface
- `src/lib/kolibriBridge.ts` — WASM/offline bridge
- `public/kolibri.wasm` — browser runtime artifact, поставляемый из `scripts/build_wasm.sh`

## Продуктовая модель

Frontend не является самостоятельным отдельным продуктом. Он является UI-слоем для одного канонического user flow:

1. открыть chat shell;
2. пройти auth/profile/preferences roundtrip;
3. создать или продолжить диалог;
4. использовать streaming;
5. открыть workspace для swarm/kpack/ingest;
6. при необходимости переключиться на WASM/offline path.

## Runtime truth

- По умолчанию shell работает через backend gateway.
- WASM path включается как совместимый browser runtime, а не как отдельный API surface.
- Если локальный WASM-ответ слабый или отключён, product shell остаётся работоспособным через backend path.

## Команды

Установка зависимостей:

```bash
npm ci --prefix frontend
```

Локальная разработка:

```bash
npm run dev --prefix frontend -- --host 0.0.0.0 --port 3000
```

Shipping build:

```bash
./scripts/release_gate.sh wasm
npm run build --prefix frontend
```

Release gate для frontend:

```bash
npm run test --prefix frontend
npm run lint --prefix frontend
npm run build --prefix frontend
```

## WASM path

`scripts/build_wasm.sh` собирает `build/wasm/kolibri.wasm` и копирует:

- `frontend/public/kolibri.wasm`
- `frontend/public/kolibri.wasm.sha256`
- `frontend/public/kolibri.wasm.txt`

Shipping frontend считает WASM bridge частью того же продуктового контура, но не использует его как оправдание для расхождения между frontend и backend contract.

## Что считается официальным public surface

- chat shell и conversation UX
- auth/account/preferences flows
- streaming chat
- workspace actions вокруг swarm runtime и `.kpack`
- browser loading of `kolibri.wasm`

Неофициальные или вторичные UI-сценарии не должны описываться как часть shipping release, пока не попадают в тот же release gate.
