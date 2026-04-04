# Kolibri Product Spec V2

## Product promise

Kolibri is a chat-first product. The ordinary user experience is one application with one main flow:

`open chats -> ask -> get an answer -> refine -> attach/teach/pack only when needed`

Everything else is secondary:

- `Workspace` for swarm, packs, teach, quality
- `Settings` for account, profile, theme and runtime preferences
- `Voice` and `Imagine` as composer actions, not separate products

## Primary surfaces

### Desktop

- left sidebar with chat history
- central thread viewport
- secondary drawers for workspace and settings

### Mobile

- `Chats`
- `Ask`

No other primary tabs are allowed.

## Required product flows

### Chat

- create/select/search/rename/pin/delete chat
- send/stream/stop
- edit and resend
- copy message
- retry on failure

### Composer

- attachment preview before send
- image/text analysis
- teach flow
- voice flow
- imagine flow
- `.kpack` import/export

### Account and settings

- auth status
- login/logout
- server-backed profile
- server-backed preferences
- theme/persona/memory/model preference persistence

### Workspace

- swarm status
- refresh and background learning state
- quality history
- kpack import/export
- teach ingest flows

## UX rules

- no overlap between header, thread viewport and composer
- no dead buttons
- no hidden broken states
- no hard refresh recovery
- no mixed light/dark theme state
- no layout collapse on keyboard/safe-area changes

## Source of truth

- frontend renders product state
- backend owns semantic routing and runtime decisions
- user-facing chat uses canonical `/api/v1/ai/chat` and `/api/v1/ai/chat/stream`
- server is source of truth for account/profile/preferences and conversation metadata
