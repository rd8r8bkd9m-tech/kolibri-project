#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: ./scripts/release_gate.sh <target>

Targets:
  bootstrap   Install Python and frontend dependencies for the active contour
  native      Configure/build native targets and run the native release gate
  backend     Run the backend pytest release gate
  wasm        Build kolibri.wasm and refresh frontend/public artifacts
  frontend    Run frontend smoke contracts, typecheck, and production build
  all         Run native + backend + wasm + frontend release-gate steps
USAGE
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${KOLIBRI_BUILD_DIR:-$repo_root/build}"
target="${1:-all}"

pytest_release_gate=(
    tests/test_auth.py
    tests/test_backend_service.py
    tests/test_common.py
    tests/test_context_window.py
    tests/test_e2e_api.py
    tests/test_kpack.py
    tests/test_persistence.py
    tests/test_rate_limiter.py
    tests/test_realtime_lookup.py
    tests/test_reasoning.py
    tests/test_swarm_runtime_api.py
    tests/test_ai_engine_integration.py
)

ctest_release_gate_regex='test_kolibri_http_server_api|test_kolibri_http_stream_api|test_kolibri_http_phase1_benchmark'

log_step() {
    printf '[release-gate] %s\n' "$1"
}

resolve_python_for_backend() {
    if [[ -n "${KOLIBRI_PYTHON:-}" ]]; then
        printf '%s\n' "$KOLIBRI_PYTHON"
        return
    fi

    local candidates=(
        "$repo_root/.venv/bin/python"
        "$repo_root/.venv312/bin/python"
    )
    local candidate
    for candidate in "${candidates[@]}"; do
        if [[ -x "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return
        fi
    done

    command -v python3
}

reset_stale_cmake_cache() {
    local desired_generator="${1:-}"
    local cache_file="$build_dir/CMakeCache.txt"
    if [[ ! -f "$cache_file" ]]; then
        return
    fi

    local cached_generator=""
    cached_generator="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$cache_file" | head -n 1)"
    if [[ -z "$cached_generator" ]]; then
        return
    fi

    if [[ -n "$desired_generator" && "$cached_generator" != "$desired_generator" ]]; then
        log_step "resetting stale CMake cache: cached generator '$cached_generator' != '$desired_generator'"
        rm -rf "$build_dir/CMakeCache.txt" "$build_dir/CMakeFiles"
        return
    fi

    if [[ -z "$desired_generator" && "$cached_generator" == "Ninja" ]] \
        && ! command -v ninja >/dev/null 2>&1 \
        && ! command -v ninja-build >/dev/null 2>&1; then
        log_step "resetting stale Ninja cache because ninja is not available locally"
        rm -rf "$build_dir/CMakeCache.txt" "$build_dir/CMakeFiles"
    fi
}

configure_cmake() {
    local cmake_args=(
        -S "$repo_root"
        -B "$build_dir"
        -DCMAKE_BUILD_TYPE=Release
        -DKOLIBRI_ENABLE_TESTS=ON
    )
    local desired_generator=""

    if [[ -n "${KOLIBRI_CMAKE_GENERATOR:-}" ]]; then
        desired_generator="$KOLIBRI_CMAKE_GENERATOR"
        cmake_args+=(-G "$KOLIBRI_CMAKE_GENERATOR")
        log_step "using CMake generator from KOLIBRI_CMAKE_GENERATOR=$KOLIBRI_CMAKE_GENERATOR"
    elif command -v ninja >/dev/null 2>&1; then
        desired_generator="Ninja"
        cmake_args+=(-G Ninja)
        log_step "using Ninja generator"
    elif command -v ninja-build >/dev/null 2>&1; then
        desired_generator="Ninja"
        cmake_args+=(-G Ninja "-DCMAKE_MAKE_PROGRAM=$(command -v ninja-build)")
        log_step "using Ninja generator via ninja-build"
    else
        log_step "ninja not found, falling back to CMake default generator"
    fi

    reset_stale_cmake_cache "$desired_generator"
    cmake "${cmake_args[@]}"
}

run_bootstrap() {
    log_step "installing Python dependencies for the active contour"
    python3 -m pip install --upgrade pip
    python3 -m pip install -r "$repo_root/requirements.txt"

    log_step "installing frontend dependencies"
    npm ci --prefix "$repo_root/frontend"
}

run_native() {
    log_step "configuring CMake build in $build_dir"
    configure_cmake

    log_step "building native targets"
    cmake --build "$build_dir"

    log_step "checking CTest inventory"
    python3 "$repo_root/scripts/check_ctest_inventory.py" --build-dir "$build_dir"

    log_step "running native release-gate tests"
    ctest --test-dir "$build_dir" -R "$ctest_release_gate_regex" --output-on-failure
}

run_backend() {
    log_step "running backend release-gate pytest suite"
    local python_bin
    python_bin="$(resolve_python_for_backend)"
    log_step "using Python interpreter: $python_bin"
    (
        cd "$repo_root"
        "$python_bin" -m pytest -q "${pytest_release_gate[@]}" --tb=short
    )
}

run_wasm() {
    log_step "building kolibri.wasm"
    "$repo_root/scripts/build_wasm.sh"
}

run_frontend() {
    log_step "running frontend smoke contracts"
    (cd "$repo_root" && npm run test --prefix frontend)

    log_step "running frontend typecheck"
    (cd "$repo_root" && npm run lint --prefix frontend)

    log_step "running frontend production build"
    (cd "$repo_root" && npm run build --prefix frontend)
}

case "$target" in
    bootstrap)
        run_bootstrap
        ;;
    native)
        run_native
        ;;
    backend)
        run_backend
        ;;
    wasm)
        run_wasm
        ;;
    frontend)
        run_frontend
        ;;
    all)
        run_native
        run_backend
        run_wasm
        run_frontend
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        echo "Unknown target: $target" >&2
        usage >&2
        exit 1
        ;;
esac
