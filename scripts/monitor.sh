#!/usr/bin/env bash
# Opens the firmware's serial console (USB CDC, 115200 baud). Ctrl-C quits.
# Type "status" + Enter for a diagnostic report, "help" for all commands.
#
#   scripts/monitor.sh                 use the single connected ESP32-C3
#   scripts/monitor.sh --port PORT
set -euo pipefail
source "$(dirname "$0")/common.sh"

PORT=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --port) PORT="${2:-}"; shift 2 ;;
    -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
    *) die "Unknown option: $1" ;;
  esac
done

require_arduino_cli
if [[ -z "$PORT" ]]; then
  find_esp32c3_ports
  case ${#CANDIDATES[@]} in
    0) die "No ESP32-C3 found. Connect the board's own USB-C port, or pass --port /dev/cu.usbmodemXXXX." ;;
    1) PORT="${CANDIDATES[0]}" ;;
    *) printf '    %s\n' "${CANDIDATES[@]}" >&2; die "Several ESP32-C3 boards found. Choose one with --port." ;;
  esac
fi
check_port "$PORT"
info "Serial monitor on $PORT (Ctrl-C to quit; type 'status' and press Enter)"
exec arduino-cli monitor --port "$PORT" --config baudrate="$SERIAL_BAUD"
