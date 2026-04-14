# Kolibri Release Checklist

## Before release

- `./scripts/release_gate.sh all` зелёный
- `README.md` обновлён
- `docs/PRODUCT_SPEC_V2.md` обновлён
- `docs/API_REFERENCE.md` обновлён
- `docs/PUBLIC_ARCHITECTURE.md` обновлён
- `docs/public_interfaces.md` обновлён
- `docs/QA_ACCEPTANCE.md` обновлён
- `docs/DEPLOY_RUNBOOK.md` обновлён
- `docs/INTEGRATION_SURFACES.md` обновлён
- `docs/release_notes.md` обновлён

## Evidence pack

- `build/wasm/kolibri.wasm`
- `build/wasm/kolibri.wasm.sha256`
- `frontend/dist`
- release docs из списка выше

## After deploy

- shell открывается
- auth status отвечает
- profile/preferences загружаются
- conversation create/select работает
- sync chat работает
- streaming chat работает
- workspace открывается
- swarm runtime status отвечает
- `.kpack` import/export smoke пройден
