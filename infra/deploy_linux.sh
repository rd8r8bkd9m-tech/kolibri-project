#!/usr/bin/env bash
set -euo pipefail

# Kolibri deployment helper for Linux environments.
# Usage: ./scripts/deploy_linux.sh --version v1.0.0 --prefix /opt/kolibri

VERSION=""
PREFIX="/opt/kolibri"
WITH_FRONTEND=1
WITH_BACKEND=1
BACKEND_IMAGE=""
FRONTEND_IMAGE=""
SKIP_PULL=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)
            VERSION="$2"
            shift 2
            ;;
        --prefix)
            PREFIX="$2"
            shift 2
            ;;
        --no-frontend)
            WITH_FRONTEND=0
            shift
            ;;
        --no-backend)
            WITH_BACKEND=0
            shift
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
    echo "Specify --version matching release tag (e.g. v1.2.0)" >&2
    exit 1
fi

mkdir -p "$PREFIX"/{bin,etc,var/lib/kolibri,logs}

if [[ ! -f "$PREFIX/etc/bootstrap.ks" ]]; then
    cat >"$PREFIX/etc/bootstrap.ks" <<'BOOTSTRAP'
начало:
    показать "Kolibri готов к работе"
конец.
BOOTSTRAP
fi

backend_image_default="ghcr.io/kolibri/kolibri-backend:${VERSION}"
frontend_image_default="ghcr.io/kolibri/kolibri-frontend:${VERSION}"

BACKEND_IMAGE="${BACKEND_IMAGE:-$backend_image_default}"
FRONTEND_IMAGE="${FRONTEND_IMAGE:-$frontend_image_default}"

echo "[INFO] Installing Kolibri ${VERSION} under $PREFIX"

if [[ $WITH_BACKEND -eq 1 ]]; then
    echo "[INFO] Deploying backend container"
    if [[ $SKIP_PULL -eq 0 ]]; then
        docker pull "$BACKEND_IMAGE"
    fi
    docker stop kolibri-backend 2>/dev/null || true
    docker rm kolibri-backend 2>/dev/null || true
    docker run -d \
        --name kolibri-backend \
        --restart unless-stopped \
        -p 4050:4050 \
        -v "$PREFIX"/var/lib/kolibri:/var/lib/kolibri \
        -v "$PREFIX"/logs:/var/log/kolibri \
        "$BACKEND_IMAGE" \
        --listen 4050 --bootstrap "$PREFIX/etc/bootstrap.ks"
fi

if [[ $WITH_FRONTEND -eq 1 ]]; then
    echo "[INFO] Deploying frontend container"
    if [[ $SKIP_PULL -eq 0 ]]; then
        docker pull "$FRONTEND_IMAGE"
    fi
    docker stop kolibri-frontend 2>/dev/null || true
    docker rm kolibri-frontend 2>/dev/null || true
    docker run -d \
        --name kolibri-frontend \
        --restart unless-stopped \
        -p 8080:80 \
        "$FRONTEND_IMAGE"
fi

echo "[INFO] Deployment complete. Backend port: 4050, Frontend port: 8080"
