# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
### Added
- **Partial Key Recovery 128-bit:** New C-core function `kolibri_recover_low64_with_known_high` for recovering unknown low 64-bit part when high 64-bit prefix is known.
- **Infeasible Guard:** Automatic rejection of search spaces larger than $2^{40}$ to prevent infeasible brute-force requests.
- **API Extension:** `/solve/hybrid` endpoint now supports `task: "partial_key_recovery_128"` with hex-formatted inputs and detailed JSON response.
- **Python Wrapper:** `KolibriAI.recover_low64_with_known_high()` method added to `kolibri_wrapper.py`.
- **Benchmarks & Tests:** New test suite `test_partial_key_recovery.py` covering small-window, not-found, and 32-bit stress scenarios.
- **Documentation:** Updated `docs/benchmarks/reverse_hash/README.md` and new `docs/hash_128/README.md` describing partial recovery architecture.
- Formalised public interface overview in `docs/public_interfaces.md`.
- Documented semantic versioning and changelog policy in `docs/developer_guide.md`.
- Multi-stage GitHub Actions pipeline with cosign signing, Docker packaging smoke tests, and release bundle assembly (`.github/workflows/ci.yml`).
- Deployment scripts for Linux/macOS/Windows with image override support (`scripts/deploy_*.sh`).
- Packaging, security, operations, and ops-briefing documentation (`docs/packaging_guide.md`, `docs/security_policy.md`, `docs/service_playbook.md`, `docs/ops_briefing.md`, etc.).
- Template release manifest for `v0.1.0` and guidance in `deploy/release-manifests/`.
