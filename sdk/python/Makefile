# Kolibri Python SDK Makefile
#
# Usage:
#   make build    - Build the C extension
#   make test     - Run tests
#   make bench    - Run benchmarks
#   make clean    - Clean build artifacts
#   make install  - Install the package

.PHONY: all build install test bench clean help

PYTHON ?= python3

all: build

# Build the C extension in place
build:
	$(PYTHON) setup.py build_ext --inplace

# Install the package (also builds extension)
install:
	pip install -e .

# Run tests
test: build
	$(PYTHON) test_correctness.py

# Run tests with pytest (more verbose)
pytest: build
	$(PYTHON) -m pytest test_correctness.py -v

# Run benchmarks
bench: build
	$(PYTHON) benchmark.py

# Clean build artifacts
clean:
	rm -rf build/
	rm -rf dist/
	rm -rf *.egg-info/
	rm -rf kolibri/*.egg-info/
	rm -rf kolibri/__pycache__/
	rm -rf __pycache__/
	rm -rf .pytest_cache/
	rm -f kolibri/_kolibri*.so
	rm -f kolibri/_kolibri*.pyd
	rm -f kolibri/_kolibri*.dll

# Build portable version (without -march=native)
portable:
	KOLIBRI_PORTABLE=1 $(PYTHON) setup.py build_ext --inplace

# Show help
help:
	@echo "Kolibri Python SDK"
	@echo ""
	@echo "Available targets:"
	@echo "  build     - Build the C extension"
	@echo "  install   - Install the package with pip"
	@echo "  test      - Run correctness tests"
	@echo "  pytest    - Run tests with pytest (more verbose)"
	@echo "  bench     - Run performance benchmarks"
	@echo "  clean     - Remove build artifacts"
	@echo "  portable  - Build without CPU-specific optimizations"
	@echo "  help      - Show this message"
