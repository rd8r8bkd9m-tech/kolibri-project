SHELL := /bin/bash
NINJA_BIN := $(or $(shell command -v ninja 2>/dev/null),$(shell command -v ninja-build 2>/dev/null))
CMAKE_GENERATOR_ARGS := $(if $(NINJA_BIN),-G Ninja -DCMAKE_MAKE_PROGRAM=$(NINJA_BIN),)

.PHONY: build test wasm frontend iso ci clean benchmark benchmark-quick benchmark-full report \
	release-gate-bootstrap release-gate-backend release-gate-native release-gate-wasm \
	release-gate-frontend release-gate extended-ci

PYTEST_RELEASE_GATE = \
	tests/test_auth.py \
	tests/test_backend_service.py \
	tests/test_common.py \
	tests/test_context_window.py \
	tests/test_e2e_api.py \
	tests/test_kpack.py \
	tests/test_persistence.py \
	tests/test_rate_limiter.py \
	tests/test_realtime_lookup.py \
	tests/test_reasoning.py \
	tests/test_swarm_runtime_api.py \
	tests/test_ai_engine_integration.py

build:
	@if [ -z "$(NINJA_BIN)" ] && [ -f build/CMakeCache.txt ] && grep -q '^CMAKE_GENERATOR:INTERNAL=Ninja$$' build/CMakeCache.txt; then \
		echo "[kolibri] removing stale Ninja cache from build/"; \
		rm -rf build/CMakeCache.txt build/CMakeFiles; \
	fi
	cmake -S . -B build $(CMAKE_GENERATOR_ARGS) -DCMAKE_BUILD_TYPE=Release -DKOLIBRI_ENABLE_TESTS=ON
	cmake --build build

release-gate-bootstrap:
	./scripts/release_gate.sh bootstrap

release-gate-backend:
	./scripts/release_gate.sh backend

release-gate-native:
	./scripts/release_gate.sh native

release-gate-wasm:
	./scripts/release_gate.sh wasm

release-gate-frontend: release-gate-wasm
	./scripts/release_gate.sh frontend

release-gate: release-gate-native release-gate-backend release-gate-frontend

test: release-gate

wasm:
	./scripts/build_wasm.sh

frontend: release-gate-wasm
	npm install --prefix frontend
	npm run build --prefix frontend

iso:
	./scripts/build_iso.sh

extended-ci: build release-gate
	python3 -m pytest tests/ -q --tb=short
	ruff check .
	pyright
	ctest --test-dir build --output-on-failure

ci: extended-ci iso
	./scripts/policy_validate.py

clean:
	rm -rf build frontend/dist frontend/node_modules
	$(MAKE) -C benchmarks clean

# Benchmark targets
benchmark:
	$(MAKE) -C benchmarks benchmark

benchmark-quick:
	$(MAKE) -C benchmarks benchmark-quick

benchmark-full:
	$(MAKE) -C benchmarks benchmark-full

report:
	$(MAKE) -C benchmarks report
