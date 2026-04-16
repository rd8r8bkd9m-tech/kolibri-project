#!/usr/bin/env bash
set -euo pipefail

# Kolibri deployment helper for macOS hosts.
# Requires Docker Desktop or colima with nerdctl.

VERSION=""
PREFIX="/usr/local/kolibri"
RUNTIME="docker"
BACKEND_IMAGE=""
FRONTEND_IMAGE=""
SKIP_PULL=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)
            VERSION="$2"
            shift 2
            ;;
        --runtime)
            RUNTIME="$2"
            shift 2
            ;;
        --prefix)
            PREFIX="$2"
            shift 2
            ;;
        --backend-image)
            BACKEND_IMAGE="$2"
            shift 2
            ;;
        --frontend-image)
            FRONTEND_IMAGE="$2"
            shift 2
            ;;
        --skip-pull)
            SKIP_PULL=1
            shift
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

if [[ -z "$VERSION" ]]; then
    echo "Usage: $0 --version v1.2.0 [--runtime docker|nerdctl] [--prefix /usr/local/kolibri]" >&2
    exit 1
fi

mkdir -p "$PREFIX"/{config,logs,data}

if [[ ! -f "$PREFIX/config/bootstrap.ks" ]]; then
    cat >"$PREFIX/config/bootstrap.ks" <<'BOOTSTRAP'
начало:
    показать "Kolibri готов к работе"
конец.
BOOTSTRAP
fi

BACKEND_IMAGE="${BACKEND_IMAGE:-ghcr.io/kolibri/kolibri-backend:${VERSION}}"
FRONTEND_IMAGE="${FRONTEND_IMAGE:-ghcr.io/kolibri/kolibri-frontend:${VERSION}}"

run() {
    if [[ "$RUNTIME" == "nerdctl" ]]; then
        colima nerdctl "$@"
    else
        docker "$@"
    fi
}

echo "[INFO] Using container runtime: $RUNTIME"

for svc in kolibri-backend kolibri-frontend; do
    run stop "$svc" 2>/dev/null || true
    run rm "$svc" 2>/dev/null || true
done

if [[ $SKIP_PULL -eq 0 ]]; then
    run pull "$BACKEND_IMAGE"
    run pull "$FRONTEND_IMAGE"
fi

run run -d \
    --name kolibri-backend \
    --restart unless-stopped \
    -p 4050:4050 \
    -v "$PREFIX/data":/var/lib/kolibri \
    -v "$PREFIX/config/bootstrap.ks":/etc/kolibri/bootstrap.ks:ro \
    "$BACKEND_IMAGE" \
    --listen 4050 --bootstrap /etc/kolibri/bootstrap.ks

run run -d \
    --name kolibri-frontend \
    --restart unless-stopped \
    -p 8080:80 \
    "$FRONTEND_IMAGE"

echo "[INFO] Kolibri is available at http://localhost:8080"
