#!/usr/bin/env bash
# One-time setup on macOS: checks the machine, installs the ESP32 Arduino core
# and U8g2 at the versions this project is tested with, and verifies the board
# options the build uses.
#
#   scripts/setup-macos.sh            check, then install what is missing
#   scripts/setup-macos.sh --check    report only, change nothing
#   scripts/setup-macos.sh --yes      do not ask before "brew install arduino-cli"
set -euo pipefail
source "$(dirname "$0")/common.sh"

CHECK_ONLY=0
ASSUME_YES=0
for arg in "$@"; do
  case "$arg" in
    --check) CHECK_ONLY=1 ;;
    --yes) ASSUME_YES=1 ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) die "Unknown option: $arg" ;;
  esac
done

heading "Computer"
require_macos
ARCH="$(uname -m)"
info "macOS $(sw_vers -productVersion) on $ARCH"
if [[ "$ARCH" == "arm64" ]]; then
  ok "Apple Silicon"
else
  warn "Not Apple Silicon ($ARCH). The scripts should still work, but are tested on arm64."
fi
command -v python3 >/dev/null 2>&1 || die "python3 is missing. Install the Xcode Command Line Tools: xcode-select --install"
ok "python3 $(python3 -c 'import platform; print(platform.python_version())')"

heading "Homebrew and Arduino CLI"
if command -v brew >/dev/null 2>&1; then
  ok "Homebrew $(brew --version | head -1 | awk '{print $2}')"
else
  warn "Homebrew is not installed. It is only needed to install arduino-cli."
  info "  Install it from https://brew.sh (review the script before running it), or"
  info "  download arduino-cli directly: https://arduino.github.io/arduino-cli/latest/installation/"
fi

if ! command -v arduino-cli >/dev/null 2>&1; then
  [[ $CHECK_ONLY -eq 1 ]] && die "arduino-cli is not installed."
  command -v brew >/dev/null 2>&1 || die "Install Homebrew or arduino-cli first (see above), then rerun this script."
  if [[ $ASSUME_YES -eq 0 ]]; then
    read -r -p "Install arduino-cli with Homebrew now? [y/N] " answer
    [[ "$answer" =~ ^[Yy]$ ]] || die "arduino-cli is required. Install it with: brew install arduino-cli"
  fi
  brew install arduino-cli
fi
ok "arduino-cli $(arduino-cli version | awk '{print $3}')"

heading "ESP32 board package"
if ! arduino-cli config get board_manager.additional_urls 2>/dev/null | grep -qF "$BOARD_MANAGER_URL"; then
  [[ $CHECK_ONLY -eq 1 ]] && die "The Espressif board manager URL is not configured."
  arduino-cli config init >/dev/null 2>&1 || true  # creates the config file if there is none
  arduino-cli config add board_manager.additional_urls "$BOARD_MANAGER_URL"
fi
ok "Board manager URL: $BOARD_MANAGER_URL"

installed_core="$(arduino-cli core list --format json 2>/dev/null | python3 -c '
import json, sys
data = json.load(sys.stdin)
for p in (data.get("platforms") if isinstance(data, dict) else data) or []:
    if p.get("id") == "esp32:esp32":
        print(p.get("installed_version") or p.get("installed") or "")
' || true)"
if [[ "$installed_core" == "$ESP32_CORE_VERSION" ]]; then
  ok "esp32:esp32 $installed_core"
else
  [[ $CHECK_ONLY -eq 1 ]] && die "esp32:esp32 $ESP32_CORE_VERSION is required (installed: ${installed_core:-none})."
  info "Installing esp32:esp32@$ESP32_CORE_VERSION (several hundred MB, takes a few minutes)..."
  arduino-cli core update-index
  arduino-cli core install "esp32:esp32@$ESP32_CORE_VERSION"
  ok "esp32:esp32 $ESP32_CORE_VERSION installed"
fi

heading "Libraries"
installed_u8g2="$(arduino-cli lib list --format json 2>/dev/null | python3 -c '
import json, sys
data = json.load(sys.stdin)
for lib in (data.get("installed_libraries") if isinstance(data, dict) else data) or []:
    l = lib.get("library", {})
    if l.get("name") == "U8g2":
        print(l.get("version", ""))
' || true)"
if [[ "$installed_u8g2" == "$U8G2_VERSION" ]]; then
  ok "U8g2 $installed_u8g2"
else
  [[ $CHECK_ONLY -eq 1 ]] && die "U8g2 $U8G2_VERSION is required (installed: ${installed_u8g2:-none})."
  arduino-cli lib update-index
  arduino-cli lib install "U8g2@$U8G2_VERSION"
  ok "U8g2 $U8G2_VERSION installed"
fi
info "WiFi, WebServer, Preferences and Wire are part of the ESP32 core."

heading "Board options"
details="$(arduino-cli board details -b esp32:esp32:esp32c3 2>/dev/null)" || die "The ESP32C3 Dev Module board is not available."
for option in CDCOnBoot=cdc CPUFreq=80 FlashMode=dio FlashSize=4M PartitionScheme=default; do
  grep -q "$option" <<<"$details" || die "Board option $option is not offered by this core version."
done
ok "ESP32C3 Dev Module supports every option in the build:"
info "  $FQBN"

heading "USB serial ports"
ports="$(list_serial_ports)"
if [[ -z "$ports" ]]; then
  info "No serial ports found."
else
  while IFS=$'\t' read -r addr id label; do
    tag=""
    [[ "$id" == "${ESPRESSIF_USB_VID}:${ESPRESSIF_USB_PID}" ]] && tag="  <- ESP32-C3 native USB"
    printf '  %-40s %-10s %s%s\n' "$addr" "${id/#:/-}" "$label" "$tag"
  done <<<"$ports"
fi
if [[ -z "$(list_esp32c3_ports)" ]]; then
  info "No ESP32-C3 connected (that is fine for building). To flash, plug the board's own USB-C"
  info "port into this Mac with a data cable, battery disconnected. See README.md, 'Flashing'."
fi

heading "Done"
ok "Setup complete. Next: scripts/build.sh"
