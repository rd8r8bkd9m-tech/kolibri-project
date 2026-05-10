# Kolibri AI - Qwen Integration Guide

## Overview
Kolibri is a hybrid AI platform with C-core, swarm architecture, and spectral analysis capabilities.

## Key Components

### 1. 128-bit Hash Lab (`POST /hash_lab/128`)
Two modes for toy/demo reversible hash inversion:

**BRUTE_FORCE_MODE** (mode=0):
- Partial brute-force with known high 64-bit prefix
- Searches low 64-bit window in parallel (OpenMP)
- Guard: rejects search space > 2^40

**INVERSION_MODE** (mode=1):
- Analytical inversion via Feistel network inverse
- Instant recovery for `feistel128_demo` hash
- Verification by recomputation

### 2. Reverse Hash Solver (`POST /solve/hybrid`)
- 32-bit bruteforce with C/OpenMP parallelization
- Policies: `first_found_fast`, `lowest_key_in_range`
- Hamming distance verification

### 3. Spectral Analysis
- FFT/IFFT with NEON optimization (Apple Silicon)
- Dominant period detection
- GF(2) linear/quadratic solvers
- Shor-inspired hidden period finding

## Environment
- Python 3.14
- OpenMP via Homebrew `libomp`
- FastAPI + Uvicorn backend
- CMake build system

## API Keys
- `QWEN_API_KEY`: Authentication for Qwen tooling
- `QWEN_CODE`: Code generation context

## Development
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
uvicorn backend.src.api_kolibri:app --reload
```
