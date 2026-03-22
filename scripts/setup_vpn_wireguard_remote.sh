#!/usr/bin/env bash
set -euo pipefail

REMOTE_HOST="${KOLIBRI_REMOTE_HOST:-ubuntu-home-wan}"
REMOTE_REPO="${KOLIBRI_REMOTE_HOME_REPO:-/home/ladik/kolibri-project}"
WG_INTERFACE="${WG_INTERFACE_NAME:-kolibri}"

echo "[vpn-remote] running prepare on ${REMOTE_HOST} (sudo password may be requested)"
ssh -t "$REMOTE_HOST" "cd '$REMOTE_REPO' && sudo ./scripts/setup_vpn_wireguard.sh --prepare --interface '$WG_INTERFACE'"

echo "[vpn-remote] done"
