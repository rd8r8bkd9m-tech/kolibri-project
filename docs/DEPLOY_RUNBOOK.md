# Kolibri Deploy Runbook

## Targets

- home server: `ubuntu-home-wan`
- production: `kolibriai.ru`

## Standard frontend deploy

1. run TypeScript check
2. run production build
3. sync built assets to target host
4. verify root HTML references the new bundle
5. smoke test main UI surfaces

## Standard backend deploy

1. run targeted Python verification
2. sync backend code
3. restart service
4. verify:
   - `/api/health`
   - `/api/v1/auth/status`
   - `/api/v1/ai/chat`
   - `/api/v1/swarm/runtime/status`

## Rollback rule

Rollback immediately if:

- chat endpoint returns raw provider/runtime errors
- UI shell becomes unusable
- auth/profile/preferences endpoints break V3 settings flow
- swarm runtime stops responding after deploy

## Mandatory smoke after deploy

- open V3 shell
- open settings
- verify auth/profile/preferences load
- create chat
- send one message
- open workspace
