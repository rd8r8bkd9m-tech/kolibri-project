# Kolibri Swarm 50 Architecture

## Goal

Swarm is not a decorative benchmark. The production target is a 50-node background runtime.

## Node roles

- `10 anchor nodes`
  - baseline formula memory
  - integrity checks
  - conservative reference answers
- `30 learner nodes`
  - continuous ingest propagation
  - domain expansion
  - candidate formula generation
- `10 validator nodes`
  - disagreement detection
  - quorum checks
  - consensus and score reporting

## Runtime behavior

Any successful ingest must:

1. update live formula memory
2. record provenance
3. enqueue swarm refresh
4. propagate to learner nodes
5. emit consensus and delta metrics

## Product-visible telemetry

- `1 vs 10 vs 50`
- consensus score
- disagreement count
- last learning propagation time
- domain growth delta
- refresh delta and import totals

Current runtime contract:

- `/api/v1/swarm/runtime/status` exposes:
  - `swarm_topology.target_node_count = 50`
  - `anchor_node_count = 10`
  - `learner_node_count = 30`
  - `validator_node_count = 10`
  - `validator_quorum = 6`
  - `swarm_nodes[]` with logical role/health/state per node

## Deployment expectations

- supervised processes
- restart policy
- health endpoint
- status snapshot files
- log visibility on home server and production
