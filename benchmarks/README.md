# Benchmarks

## World compression benchmark (Kolibri vs world)

Binary: `benchmarks/bench_kolibri_vs_world.c`

### Build (from repo root)

1) Build core library (needed for linking):

```bash
cmake -S . -B build -G Ninja && cmake --build build
```

2) Build and run the benchmark:

```bash
cd benchmarks
make build
LD_LIBRARY_PATH=../build ./bench_kolibri_vs_world
```

### Reproducible JSON output

Write a machine-readable report:

```bash
cd benchmarks
LD_LIBRARY_PATH=../build ./bench_kolibri_vs_world --quiet --json=results/world_results.json
```

Schema (v1):
- `tool`: always `"bench_kolibri_vs_world"`
- `schema_version`: `1`
- `timestamp_ms`: wall-clock timestamp in milliseconds
- `git_head`: `git rev-parse HEAD` (empty if unavailable)
- `summary`: `{ "kolibri_wins": int, "total_corpora": int }`
- `corpora[]`: per-corpus results
  - `name`: corpus display name
  - `size`: uncompressed size (bytes)
  - `winner_index`: index in `results[]` of best ratio among roundtrip-ok entries
  - `results[]`: per-archiver entry
    - `name`, `compressed_size`, `original_size`, `ratio`, `compress_ms`, `decompress_ms`, `roundtrip_ok`, `error`

Notes:
- Synthetic corpora are deterministic (same seed).
- External tools availability/versions affect results; for CI, prefer running via `benchmarks/run_all_benchmarks.sh` which stores JSON in `benchmarks/results/`.
