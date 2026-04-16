#!/usr/bin/env bash
set -euo pipefail

# Run Kolibri OS ISO in QEMU with sane defaults.

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
ISO_PATH="$BUILD_DIR/kolibri.iso"

usage() {
  cat <<EOF
Usage: $0 [--rebuild] [--mem MB] [--no-gui] [--serial LOGFILE]

Options:
  --rebuild        Build (or rebuild) the ISO before running.
  --mem MB         Guest RAM in megabytes (default: 128).
  --no-gui         Use curses (text) display instead of a GUI window.
  --serial FILE    Tee serial (COM1) output to FILE in addition to stdout.

Examples:
  $0 --rebuild
  $0 --mem 256 --serial run_autonomy.log
EOF
}

REBUILD=0
MEM=128
NO_GUI=0
SERIAL_LOG=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --rebuild) REBUILD=1; shift ;;
    --mem) MEM="$2"; shift 2 ;;
    --no-gui) NO_GUI=1; shift ;;
    --serial) SERIAL_LOG="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ $REBUILD -eq 1 || ! -f "$ISO_PATH" ]]; then
  "$ROOT_DIR/scripts/build_iso.sh"
fi

if [[ ! -f "$ISO_PATH" ]]; then
  echo "[ERROR] ISO not found: $ISO_PATH" >&2
  exit 1
fi

# Choose accelerator if available (host-dependent) by querying QEMU.
ACCEL=()
if command -v qemu-system-i386 >/dev/null 2>&1; then
  if qemu-system-i386 -accel help 2>/dev/null | grep -q '^hvf'; then
    ACCEL=("-accel" "hvf")
  elif qemu-system-i386 -accel help 2>/dev/null | grep -q '^kvm'; then
    ACCEL=("-accel" "kvm")
  elif qemu-system-i386 -accel help 2>/dev/null | grep -q '^whpx'; then
    ACCEL=("-accel" "whpx")
  else
    ACCEL=()
  fi
fi

DISPLAY_OPTS=("-vga" "std")
if [[ $NO_GUI -eq 1 ]]; then
  if [[ -n "$SERIAL_LOG" ]]; then
    # Headless when serial is redirected to a file
    DISPLAY_OPTS=("-display" "none")
  else
    DISPLAY_OPTS=("-display" "curses")
  fi
fi

SERIAL_OPTS=("-serial" "stdio")
if [[ -n "$SERIAL_LOG" ]]; then
  mkdir -p "$(dirname "$SERIAL_LOG")"
  SERIAL_OPTS=("-serial" "file:$SERIAL_LOG")
  echo "[INFO] Serial redirected to: $SERIAL_LOG"
fi

cmd=(qemu-system-i386)
if [[ ${#ACCEL[@]:-0} -gt 0 ]]; then
  cmd+=("${ACCEL[@]}")
fi
cmd+=(
  -m "$MEM"
  -cdrom "$ISO_PATH"
  -boot d
  -smp 1
  -rtc base=utc
  -no-reboot -no-shutdown
)
cmd+=("${DISPLAY_OPTS[@]}")
cmd+=("${SERIAL_OPTS[@]}")
exec "${cmd[@]}"
