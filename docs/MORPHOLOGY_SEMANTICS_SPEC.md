# Kolibri Morphology and Semantics Spec

## Goal

Morphology and semantics are required runtime layers, not optional Python workarounds.

## Morphology

Required capabilities:

- tokenization
- normalization
- lemmatization
- RU inflection handling
- base EN normalization

## Semantics

Required capabilities:

- entity extraction
- topic extraction
- relation extraction
- follow-up continuity
- semantic memory linking
- contradiction detection

## Product impact

These layers exist to improve:

- topic retention across 5+ turns
- follow-up quality for `а почему`, `а пример`, `что ещё`, `сравни`
- city/entity switching
- self-consistency
- reduction of route-specific patches

## Runtime placement

- input normalization in backend orchestrator
- semantic signals exported into digit-voting stage
- selective support from C-core formula/inference layers

## Current implemented stage

The first C-core stage is now live in `backend/src/inference.c`:

- canonicalization of domain/topic tokens during query topic extraction
- canonical entity extraction for definition-like requests
- explicit `query_kind`
- explicit `canonical_topic`
- explicit `definition_entity`
- export of this summary through `KolibriInferenceResult` and `kolibri_infer_cli`

Python currently consumes these fields only as telemetry. The semantic decision remains in the C path.
