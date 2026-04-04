# Kolibri Numeric Voting Model

## Purpose

Numeric cognition is an internal decision layer. Digits `0..9` are not decoration; they are vote channels used before final answer synthesis.

## Digit roles

- `0` reject / null signal
- `1` fact anchor
- `2` structural relation
- `3` causal link
- `4` arithmetic and logical check
- `5` semantic similarity
- `6` memory recall
- `7` novelty and new evidence
- `8` action/tool suggestion
- `9` final synthesis pressure

## Decision stages

Each ordinary chat answer should accumulate votes for:

- topic confidence
- memory support
- tool necessity
- semantic consistency
- arithmetic/logical consistency
- output safety

## Release rule

An answer is rejected or downgraded when:

- topic support is too low
- conflict is too high
- output looks unrelated or unsafe
- no evidence exists and no valid tool path is available

## Current implemented stage

The first production stage is already wired into the C inference path:

- `inference.c` now accumulates explicit vote channels `0..9` during formula-association scoring
- `KolibriInferenceResult` now carries `numeric_vote`
- `kolibri_infer_cli` prints:
  - `DIGIT_WINNER`
  - `DIGIT_WINNER_SCORE`
  - `DIGIT_RUNNER_UP_SCORE`
  - `DIGIT_CONSENSUS`
  - `DIGIT_VOTES`
- Python orchestration parses these fields as telemetry only; the decision still remains in the C core

This document remains the source of truth for the next stages in `formula.c`, `roy.c`, morphology/semantics and full runtime orchestration.

Current factual runtime:

- `C-core formula` responses expose `c_digit_*` telemetry from `backend/src/inference.c`.
- Ordinary chat responses also expose `runtime_digit_winner`, `runtime_digit_consensus`, `runtime_digit_votes` and `runtime_vote_origin` in `formula_data`.
