# Kolibri: World Leader Execution Plan

Date: 2026-02-19
Owner: Kolibri core
Status: in_progress

## 1. Verified current state

Source documents reviewed:

1. `/Users/kolibri/kolibri-project/README.md`
2. `/Users/kolibri/kolibri-project/docs/plans/ROADMAP_SINGLE_SOURCE_OF_TRUTH.md`
3. `/Users/kolibri/kolibri-project/docs/plans/CALIBRI_AI_AUTOPILOT_TZ.md`
4. `/Users/kolibri/kolibri-project/docs/plans/ROADMAP_AGI.md`
5. `/Users/kolibri/kolibri-project/docs/archive/unconfirmed_reports/README.md`
6. `/Users/kolibri/kolibri-archiver-v85-compressc/docs/benchmark_world_standard_summary.md`
7. `/Users/kolibri/kolibri-archiver-v85-compressc/docs/benchmark_world_standard.csv`

Verified facts:

1. AI stack has substantial code in Python and C-core modules, but not all planned C modules exist yet (`vision.c`, `audio.c`, `backend/src/reasoning.c`, `knowledge_base.c` are absent).
2. Archiver has real benchmark harness and reproducible reports.
3. Current archiver status is not world-leading:
   1. Average ratio rank: 3.67/6
   2. Average compression speed rank: 5.17/6
4. There are historical reports in archive that are explicitly marked as unconfirmed and must not be used as proof until re-verified.

## 2. Product strategy for real leadership

Single algorithm cannot be top-1 on both ratio and speed for all corpora.
Leadership target is profile-based and measurable:

1. `speed` profile: top-1 or top-2 in compression speed on standard corpus.
2. `max` profile: top-1 or top-2 in compression ratio on standard corpus.
3. `balanced` profile: no catastrophic tradeoff, target top-3 on both axes.

This strategy preserves engineering honesty and gives a path to real market positioning.

## 3. Mandatory benchmark protocol

1. Corpus: `enwik8 + silesia` with fixed file list.
2. Repeats: minimum 3, median values.
3. Metrics:
   1. ratio
   2. compress MB/s
   3. decompress MB/s
   4. roundtrip integrity
4. Reports stored in repo under docs with before/after diff per change.

## 4. Execution phases

## Phase A: Stabilize baseline

1. Freeze current stable decompression compatibility.
2. Keep NEON and adaptive improvements that passed roundtrip.
3. Remove or gate experiments that regress benchmark aggregate.

## Phase B: Speed leadership track

1. Optimize `speed` profile pipeline and memory behavior.
2. Reduce branch misses in hot loops and keep SIMD-first paths.
3. Introduce corpus-aware fast-path selection.

Exit criteria:

1. `speed` profile improves aggregate compression MB/s versus current baseline.
2. No integrity regressions.

## Phase C: Ratio leadership track

1. Improve CM/RC context quality with controlled memory growth.
2. Add stronger post-model refinement only where net win is proven.
3. Tune BWT block policy dynamically by data class and size.

Exit criteria:

1. `max` profile improves aggregate ratio versus current baseline.
2. Decompression remains stable and bounded.

## Phase D: Product hardening

1. Add profile-level CLI and API contract documentation.
2. Add nightly benchmark regression checks.
3. Publish transparent leaderboard report with raw CSV artifacts.

## 5. AI product track in parallel

For chat product leadership, use strict milestones:

1. Latency reduction for `/api/v1/ai/chat` from tens of seconds to product SLA.
2. Streaming-first UX with stable TTFB and cancellation controls.
3. RAG quality with source-grounded answers and benchmarked eval set.
4. End-to-end release checklists on Ubuntu production environment.

## 6. Non-negotiable rules

1. No claims without reproducible benchmark artifacts.
2. No report leaves archive/unconfirmed until re-verified.
3. Every optimization must include before/after table and rollback path.

