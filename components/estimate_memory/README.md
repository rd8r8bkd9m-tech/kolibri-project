# Kolibri Estimate Memory

Local-first C23/SQLite foundation for personal estimate memory and relay sync.

This component is intentionally **not an estimator algorithm**. The Kolibri agent and the user decide the composition of an estimate. This module gives the application durable offline memory, exact minor-unit price storage, workflow-derived trust, and a privacy-bounded sync outbox.

## What it implements

- automatic SQLite schema creation;
- WAL mode and transactional writes;
- personal/workspace price observations;
- prices stored as integer minor units, never binary float;
- versioned trust policy;
- workflow signals:
  - `agent_draft`;
  - `saved`;
  - `user_edited`;
  - `exported`;
  - `sent_to_client`;
  - `accepted_by_client`;
  - `contract_created`;
  - `act_created`;
  - `completed`;
- `sent_to_client` promotes a price in personal memory;
- selection of the strongest recent personal price by workspace/domain/region/unit;
- usage count and last-used timestamp;
- append-only sync outbox with idempotent event IDs and local sequence;
- retry/sent state and server cursor;
- anonymised `price.observation.v1` payload generated inside SQLite;
- relay payload excludes estimate ID, actor ID, source reference and original free text;
- close/reopen persistence test.

## Build and test

```bash
cmake -S components/estimate_memory -B build-estimate-memory -G Ninja
cmake --build build-estimate-memory
ctest --test-dir build-estimate-memory --output-on-failure
```

Expected result:

```text
1/1 Test #1: test_estimate_memory ... Passed
```

## Trust policy

The initial policy is stored in the database as `local-trust-v1`, not hidden in agent prompts. It is deliberately versioned so future releases can migrate and recalculate trust without destroying history.

The values are initial defaults and are covered by tests. They are not a claim that one weighting policy is universally optimal. Relay aggregation will use its own versioned server policy and preserve the original local signal.

## Privacy boundary

`kem_enqueue_price_for_relay` accepts a pseudonymous contributor ID and emits only the normalized data needed for aggregation. Customer identities, exact addresses, estimate IDs, document text and user free text remain local/private unless a separate explicit consent flow is implemented.

## Next integration slices

1. Compile this component as part of the existing C23 core.
2. Add a stable FFI/WASM boundary.
3. Use the same SQLite schema in web/PWA and native desktop builds.
4. Connect the outbox to the Kolibri relay API.
5. Render local-vs-relay suggestions in assistant-ui tool UI.
6. Add encrypted backup/export/import and month-offline sync tests.
