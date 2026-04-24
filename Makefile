SHELL := /bin/bash
NINJA_BIN := $(or $(shell command -v ninja 2>/dev/null),$(shell command -v ninja-build 2>/dev/null))
CMAKE_GENERATOR_ARGS := $(if $(NINJA_BIN),-G Ninja -DCMAKE_MAKE_PROGRAM=$(NINJA_BIN),)

.PHONY: build cli test wasm web frontend iso ci clean benchmark benchmark-quick benchmark-full report \
	release-gate-bootstrap release-gate-backend release-gate-native release-gate-wasm \
	release-gate-frontend release-gate extended-ci

PYTEST_RELEASE_GATE = \
	infra/tests/test_auth.py \
	infra/tests/test_backend_service.py \
	infra/tests/test_common.py \
	infra/tests/test_context_window.py \
	infra/tests/test_e2e_api.py \
	infra/tests/test_kpack.py \
	infra/tests/test_persistence.py \
	infra/tests/test_rate_limiter.py \
	infra/tests/test_realtime_lookup.py \
	infra/tests/test_reasoning.py \
	infra/tests/test_swarm_runtime_api.py \
	infra/tests/test_ai_engine_integration.py

build:
	@if [ -z "$(NINJA_BIN)" ] && [ -f build/CMakeCache.txt ] && grep -q '^CMAKE_GENERATOR:INTERNAL=Ninja$$' build/CMakeCache.txt; then \
		echo "[kolibri] removing stale Ninja cache from build/"; \
		rm -rf build/CMakeCache.txt build/CMakeFiles; \
	fi
	cmake -S . -B build $(CMAKE_GENERATOR_ARGS) -DCMAKE_BUILD_TYPE=Release -DKOLIBRI_ENABLE_TESTS=ON
	cmake --build build

cli:
	@if [ -z "$(NINJA_BIN)" ] && [ -f build/CMakeCache.txt ] && grep -q '^CMAKE_GENERATOR:INTERNAL=Ninja$$' build/CMakeCache.txt; then \
		echo "[kolibri] removing stale Ninja cache from build/"; \
		rm -rf build/CMakeCache.txt build/CMakeFiles; \
	fi
	cmake -S . -B build $(CMAKE_GENERATOR_ARGS) -DCMAKE_BUILD_TYPE=Release -DKOLIBRI_ENABLE_TESTS=ON
	cmake --build build --target kolibri_cli

release-gate-bootstrap:
	./infra/release_gate.sh bootstrap

release-gate-backend:
	./infra/release_gate.sh backend

release-gate-native:
	./infra/release_gate.sh native

release-gate-wasm:
	./infra/release_gate.sh wasm

release-gate-frontend: release-gate-wasm
	./infra/release_gate.sh frontend

release-gate: release-gate-native release-gate-backend release-gate-frontend

test: release-gate

wasm:
	./infra/build_wasm.sh

web: release-gate-wasm
	npm install --prefix web
	npm run build --prefix web

# Backward-compatible alias. The active web shell is web/.
frontend: web

iso:
	./scripts/build_iso.sh

extended-ci: build release-gate
	python3 -m pytest infra/tests/ -q --tb=short
	ruff check .
	pyright
	ctest --test-dir build --output-on-failure

ci: extended-ci iso
	./scripts/policy_validate.py

clean:
	rm -rf build web/dist web/node_modules frontend/dist frontend/node_modules
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
