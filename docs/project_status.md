# Kolibri Project Status

## Status policy

This file is an honest delivery snapshot. It is not allowed to declare the product complete while acceptance gates are still open.

Primary source-of-truth documents:

- [PUBLIC_ARCHITECTURE.md](PUBLIC_ARCHITECTURE.md)
- [PRODUCT_SPEC_V2.md](PRODUCT_SPEC_V2.md)
- [API_REFERENCE.md](API_REFERENCE.md)
- [QA_ACCEPTANCE.md](QA_ACCEPTANCE.md)
- [plans/ROADMAP_TO_COMPETITIVE_AGI.md](plans/ROADMAP_TO_COMPETITIVE_AGI.md)
- [DEPLOY_RUNBOOK.md](DEPLOY_RUNBOOK.md)

## Current active execution plan

The active plan follows the master sequence from
`docs/plans/ROADMAP_TO_COMPETITIVE_AGI.md`:

- `Reasoner -> Chat -> Swarm`

Operationally, the current execution plan remains:

- frontend full completion first
- backend canonical runtime second
- 50-node swarm target
- numeric voting `0..9`
- morphology and semantics
- continuous learning and WASM/offline parity

## Current factual state

### Frontend

- V3 shell is the only active shell.
- Desktop and mobile run through the same product architecture.
- Server-backed account/profile/preferences are now hydrated at V3 shell startup, not only inside the settings drawer.
- Conversation metadata has a server API and frontend sync layer.
- V3 thread now has a backend turns API and server-hydration path for chat history.
- Ordinary chat in the frontend now defaults to one backend-first canonical runtime; semantic request routing and hidden `WASM-first` behavior are no longer used in the normal chat path.
- Workspace V3 now reads `swarm_topology` and `swarm_nodes` from backend status instead of the old hardcoded `10-node` text.

### Backend

- Auth router exists and now exposes public auth status for frontend boot.
- Account profile and account preferences endpoints exist.
- Conversation session metadata endpoints exist alongside persisted turns.
- Conversation history can now be read through a dedicated `/api/v1/ai/conversations/{id}/turns` endpoint backed by live runtime memory and SQLite persistence.
- Background learning now exists as a dedicated runtime manager with persisted source list, status file, recent run history, per-source health/backoff, manual run endpoint and autostart hook in FastAPI lifespan.
- The first explicit numeric-voting stage `0..9` is now implemented in `backend/src/inference.c` for formula-association selection and exposed by `apps/kolibri_infer_cli.c` as C-core telemetry.
- The first explicit morphology/topic-summary stage is now implemented in `backend/src/inference.c`: C-core exports `query_kind`, `canonical_topic`, `definition_entity` and `topic_token_count`.
- Ordinary chat projection queries (`explain`, `tell`, `knowledge`, `structure`, `importance`) now run through one unified `C-core-first` path in `backend/service/ai_engine.py` with a shared honest fallback instead of separate legacy branches.
- Ordinary chat responses now also expose full runtime digit-voting telemetry in `formula_data` (`runtime_digit_winner`, `runtime_digit_consensus`, `runtime_digit_votes`, `runtime_vote_origin`) instead of reserving `0..9` voting only for `c-core-formula`.
- Weather queries are no longer handled by an early special-command branch; they now flow through the canonical synthesis/runtime path and keep the same `web-augment-weather` / `weather-unavailable` contract.
- `dialog-context` and `dialog-fact-ack` no longer short-circuit directly in `chat()`; both now flow through the canonical synthesis path before result shaping.
- Read-only `profile-memory` and `conversation-memory` no longer short-circuit in the early special-command branch; they now flow through the canonical synthesis path, while memory write commands remain explicit.
- Read-only `identity`, `greeting`, `smalltalk-checkin`, `self-meta`, `kolibri-architecture`, `clarify-entity`, `abuse-deescalation` and `capabilities` now also flow through the canonical synthesis path instead of the early special-command branch.
- `math-eval` no longer short-circuits in the early special-command branch; ordinary math queries now run through the same canonical synthesis path before response shaping.
- Read-only `document-list` no longer short-circuits in the early special-command branch; trained-text listing now flows through the canonical `profile-memory` read path.
- Read-only `story-memory` and `retell-memory` no longer short-circuit in the early special-command branch; both now flow through canonical synthesis as learned-document reads.
- Read-only system inspection (`stats`, `pattern-lookup`, `formula-inspect`, `health`, `system metrics`) no longer short-circuits in the early special-command branch; these responses now flow through canonical synthesis as system inspection reads.
- Projection queries (`объясни ...`, `расскажи о ...`, `что ты знаешь о ...`, `как устроено ...`, `почему важно ...`) no longer short-circuit via the early direct `c_formula_query/web-reference` path; they now pass through canonical synthesis and preserve `c-core-formula` telemetry when C-core answers.
- Ordinary chat still requires further consolidation to fully remove all remaining route-specific debt outside that projection layer.

### Swarm and runtime

- Swarm runtime, demo ingest and `.kpack` APIs exist.
- Background web-learning sources, cycle status and recent run history are exposed through `/api/v1/swarm/runtime/learning/*`.
- `learning/status` now exposes source-aware `internet_runtime` details with separate `daemon_state`, `source_state` and counts for `eligible/backoff/failing/no-change` sources instead of relying only on a single coarse network flag.
- `learning/status` also exposes `recent_success_source_count`, so a failed raw network probe no longer mislabels the runtime as dead when recent source fetches actually succeeded and the manager is simply in backoff.
- Background learning no longer counts `formula_trainer` results with `failed to fetch` in `stderr` as successful source ingests; such runs now become source errors instead of fake `sources_succeeded=1` cycles with `saved_documents=0`.
- Backend deploy now syncs `background_learning_sources.json` and enables background learning on the remote server.
- `document-list` now prefers cleaner display labels for trained texts and falls back to summary/text when the stored title is obviously noisy.
- `document-list` and profile-memory now hide `auto-message` training fragments from the user-facing learned-text list; automatic long-message ingestion still exists for background learning, but no longer pollutes the visible taught-material list.
- 50-node production swarm is a target architecture, not yet the fully completed runtime.
- Swarm runtime status now exposes a real 50-node topology contract with role counts, validator quorum, consensus score, logical node health and `1 vs 10 vs 50` comparison targets.

## Acceptance snapshot for this iteration

- backend syntax: passed
- targeted canonical projection-runtime tests: passed
- targeted ordinary-chat runtime-voting tests: passed
- backend targeted background-learning tests: passed
- targeted C/Python numeric-voting tests: passed
- targeted C/Python morphology-summary tests: passed
- targeted `ai_engine` integration tests for projection unification: passed
- targeted `swarm_runtime_api` topology tests: passed
- `test_inference_web_formula`: passed
- frontend typecheck: passed
- frontend production build: passed
- full `tests/test_swarm_runtime_api.py`: passed

## Not yet allowed to claim

The project must **not** currently be described as:

- fully finished
- release complete
- production perfect
- a Grok/ChatGPT-level conversational system

Those claims require the acceptance gates in [QA_ACCEPTANCE.md](QA_ACCEPTANCE.md) to be green across the remaining phases.
