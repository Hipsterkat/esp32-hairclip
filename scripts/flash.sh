#!/usr/bin/env bash
# Uploads the firmware over the ESP32-C3's own USB-C port. Never picks a port
# silently: it proposes the ESP32-C3 it detects and waits for confirmation.
#
#   scripts/flash.sh                          detect the board, confirm, flash
#   scripts/flash.sh --port /dev/cu.usbmodemXXXX
#   scripts/flash.sh --port PORT --yes        non-interactive (requires --port)
#   scripts/flash.sh --no-build               upload the existing build
#   scripts/flash.sh --monitor                open the serial monitor afterwards
set -euo pipefail
source "$(dirname "$0")/common.sh"

PORT=""
ASSUME_YES=0
BUILD=1
MONITOR=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --port) PORT="${2:-}"; shift 2 ;;
    --yes) ASSUME_YES=1; shift ;;
    --no-build) BUILD=0; shift ;;
    --monitor) MONITOR=1; shift ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) die "Unknown option: $1" ;;
  esac
done

require_macos
require_arduino_cli
if [[ $ASSUME_YES -eq 1 && -z "$PORT" ]]; then
  die "--yes needs an explicit --port: this script never chooses a port on its own."
fi
INTERACTIVE=1
[[ -t 0 ]] || INTERACTIVE=0
if [[ $INTERACTIVE -eq 0 && $ASSUME_YES -eq 0 ]]; then
  die "No terminal to confirm in. Use --port PORT --yes for a non-interactive upload."
fi

if [[ $BUILD -eq 1 ]]; then
  "$ROOT/scripts/build.sh"
fi
APP="$BUILD_DIR/hairclip.ino.bin"
[[ -f "$APP" ]] || die "No firmware in ${BUILD_DIR#"$ROOT"/}. Run scripts/build.sh first."

heading "Before you flash"
cat <<'EOF'
  1. Disconnect the LiPo battery (unplug it, or at least slide the switch OFF and
     confirm with a meter that no battery voltage reaches the board's 5V pad).
  2. Leave the TP4056 charger's USB-C port EMPTY.
  3. Connect the ESP32-C3 SuperMini's OWN USB-C port to this Mac with a data cable.
  4. Close any open serial monitor (Arduino IDE, screen, scripts/monitor.sh).
EOF

heading "Serial port"
if [[ -n "$PORT" ]]; then
  check_port "$PORT"
  id="$(list_serial_ports | awk -F'\t' -v p="$PORT" '$1 == p { print $2 }')"
  if [[ "$id" != "${ESPRESSIF_USB_VID}:${ESPRESSIF_USB_PID}" ]]; then
    warn "$PORT does not identify as an ESP32-C3 native USB port (USB id: ${id:-unknown})."
    warn "That is expected only for boards with a separate USB-UART chip."
    if [[ $ASSUME_YES -eq 0 ]]; then
      read -r -p "Flash $PORT anyway? Type 'yes': " answer
      [[ "$answer" == "yes" ]] || die "Cancelled."
    fi
  fi
else
  find_esp32c3_ports
  if [[ ${#CANDIDATES[@]} -eq 0 ]]; then
    warn "No ESP32-C3 found. Serial ports present:"
    list_serial_ports | awk -F'\t' '{ printf "    %s  %s\n", $1, $3 }' >&2
    cat >&2 <<'EOF'
  * Use the SuperMini's USB-C port (not the charger's) and a cable that carries data.
  * Still missing? Put the chip in download mode: hold BOOT, tap RESET, release BOOT
    (or hold BOOT while plugging in USB), then run this script again.
  * Then pass the port explicitly if needed: scripts/flash.sh --port /dev/cu.usbmodemXXXX
EOF
    exit 1
  elif [[ ${#CANDIDATES[@]} -gt 1 ]]; then
    warn "More than one ESP32-C3 is connected:"
    printf '    %s\n' "${CANDIDATES[@]}" >&2
    die "Choose one with --port."
  fi
  PORT="${CANDIDATES[0]}"
  info "Detected ESP32-C3 native USB (VID:PID ${ESPRESSIF_USB_VID}:${ESPRESSIF_USB_PID}) at $PORT"
fi

heading "Ready to flash"
info "  Port      $PORT"
info "  Board     $FQBN"
info "  Firmware  ${APP#"$ROOT"/} ($(wc -c <"$APP" | tr -d ' ') bytes)"
info "  SHA-256   $(shasum -a 256 "$APP" | awk '{print $1}')"
if [[ $ASSUME_YES -eq 0 ]]; then
  read -r -p "Type 'flash' to write this firmware to $PORT: " answer
  [[ "$answer" == "flash" ]] || die "Cancelled. Nothing was written."
fi

heading "Uploading"
if ! arduino-cli upload --fqbn "$FQBN" --port "$PORT" --input-dir "$BUILD_DIR" "$SKETCH_DIR"; then
  cat >&2 <<'EOF'

Upload failed. Try, in order:
  * Close every serial monitor and run the script again.
  * Download mode: hold BOOT, tap RESET, release BOOT. The port name may change:
    run scripts/flash.sh again so it re-detects the board.
  * Another USB cable or port (avoid unpowered hubs).
  * Lower speed: edit UploadSpeed=921600 to 115200 in scripts/common.sh.
See TROUBLESHOOTING.md, "Upload errors".
EOF
  exit 1
fi
ok "Upload finished. The board resets by itself; press RESET if the display stays dark."
info "Next: test on USB power before connecting the battery (docs/HARDWARE_TEST_CHECKLIST.md)."

if [[ $MONITOR -eq 1 ]]; then
  exec "$ROOT/scripts/monitor.sh" --port "$PORT"
fi
