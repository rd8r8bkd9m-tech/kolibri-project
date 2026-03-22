#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  sudo ./scripts/setup_vpn_wireguard.sh --prepare [--interface kolibri]
  sudo ./scripts/setup_vpn_wireguard.sh --config /path/to/client.conf [--interface kolibri] [--start]

Backward compatible:
  sudo ./scripts/setup_vpn_wireguard.sh /path/to/client.conf

Modes:
  --prepare        Install WireGuard and create a template config (without keys).
  --config PATH    Install ready config to /etc/wireguard/<interface>.conf
  --start          Start and enable wg-quick@<interface> after config install.
EOF
}

if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
  echo "Run as root: sudo $0 ..." >&2
  exit 1
fi

MODE=""
SRC_CONF=""
TARGET_NAME="${WG_INTERFACE_NAME:-kolibri}"
START_AFTER_INSTALL=0

if [[ $# -eq 1 && "${1:-}" != "--prepare" && "${1:-}" != "--config" ]]; then
  MODE="config"
  SRC_CONF="$1"
  START_AFTER_INSTALL=1
else
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --prepare)
        MODE="prepare"
        shift
        ;;
      --config)
        MODE="config"
        SRC_CONF="${2:-}"
        shift 2
        ;;
      --interface)
        TARGET_NAME="${2:-}"
        shift 2
        ;;
      --start)
        START_AFTER_INSTALL=1
        shift
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        echo "Unknown argument: $1" >&2
        usage >&2
        exit 1
        ;;
    esac
  done
fi

if [[ -z "$MODE" ]]; then
  usage >&2
  exit 1
fi

TARGET_CONF="/etc/wireguard/${TARGET_NAME}.conf"
TEMPLATE_CONF="/etc/wireguard/${TARGET_NAME}.conf.template"

install_wireguard() {
  if command -v wg >/dev/null 2>&1 && command -v wg-quick >/dev/null 2>&1; then
    echo "[vpn] wireguard already installed"
    return
  fi
  if command -v apt-get >/dev/null 2>&1; then
    echo "[vpn] installing wireguard packages via apt"
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -y
    apt-get install -y wireguard wireguard-tools
  else
    echo "[vpn] apt-get not found; install WireGuard manually and rerun" >&2
    exit 1
  fi
}

prepare_template() {
  mkdir -p /etc/wireguard
  chmod 700 /etc/wireguard
  if [[ ! -f "$TEMPLATE_CONF" ]]; then
    cat > "$TEMPLATE_CONF" <<'EOF'
[Interface]
PrivateKey = <PASTE_CLIENT_PRIVATE_KEY_HERE>
Address = 10.8.0.2/24
DNS = 1.1.1.1, 8.8.8.8

[Peer]
PublicKey = <PASTE_SERVER_PUBLIC_KEY_HERE>
PresharedKey = <OPTIONAL_PRESHARED_KEY_OR_REMOVE_LINE>
Endpoint = vpn.example.com:51820
AllowedIPs = 0.0.0.0/0, ::/0
PersistentKeepalive = 25
EOF
    chmod 600 "$TEMPLATE_CONF"
    echo "[vpn] created template: $TEMPLATE_CONF"
  else
    echo "[vpn] template already exists: $TEMPLATE_CONF"
  fi
}

install_config() {
  if [[ -z "$SRC_CONF" ]]; then
    echo "Missing --config /path/to/client.conf" >&2
    exit 1
  fi
  if [[ ! -f "$SRC_CONF" ]]; then
    echo "Config file not found: $SRC_CONF" >&2
    exit 1
  fi
  install -m 600 "$SRC_CONF" "$TARGET_CONF"
  echo "[vpn] installed config: $TARGET_CONF"
}

validate_non_placeholder_config() {
  if grep -Eq '<PASTE_|<OPTIONAL_' "$TARGET_CONF"; then
    echo "[vpn] config still contains placeholders: $TARGET_CONF" >&2
    echo "[vpn] fill real keys first, then run with --start" >&2
    exit 1
  fi
}

start_interface() {
  systemctl daemon-reload
  systemctl enable --now "wg-quick@${TARGET_NAME}"
  echo "[vpn] interface '${TARGET_NAME}' is up"
  wg show "${TARGET_NAME}" || true
  ip -brief address show "${TARGET_NAME}" || true
}

case "$MODE" in
  prepare)
    install_wireguard
    prepare_template
    echo "[vpn] prepare complete (no keys configured, tunnel not started)"
    ;;
  config)
    install_wireguard
    install_config
    if [[ "$START_AFTER_INSTALL" -eq 1 ]]; then
      validate_non_placeholder_config
      start_interface
    else
      echo "[vpn] config installed; start later with:"
      echo "      sudo systemctl enable --now wg-quick@${TARGET_NAME}"
    fi
    ;;
esac
