SHELL := /bin/bash

.PHONY: build test wasm frontend iso ci clean benchmark benchmark-quick benchmark-full report

build:
	cmake -S . -B build -G Ninja
	cmake --build build

test: build
	ctest --test-dir build
	pytest -q
	ruff check .
	pyright
	npm run test --prefix frontend -- --runInBand

wasm:
	./scripts/build_wasm.sh

frontend: wasm
	npm install --prefix frontend
	npm run build --prefix frontend

iso:
	./scripts/build_iso.sh

ci: build test frontend iso
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
