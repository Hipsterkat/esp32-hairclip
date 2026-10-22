# Shared settings and helpers for the scripts in this folder (sourced, not run).
# shellcheck shell=bash

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKETCH_DIR="$ROOT/hairclip"
BUILD_DIR="$ROOT/build/firmware"

# Toolchain versions this project is built and tested with.
ESP32_CORE_VERSION="3.3.12"
U8G2_VERSION="2.36.19"
BOARD_MANAGER_URL="https://espressif.github.io/arduino-esp32/package_esp32_index.json"

# "ESP32C3 Dev Module" with native USB CDC on boot (Serial = USB port), 80 MHz
# CPU, 4 MB flash, default partitions. DIO flash mode works on every
# ESP32-C3 SuperMini clone; QIO is marginally faster but not universal.
FQBN="esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=80,FlashMode=dio,FlashFreq=80,FlashSize=4M,PartitionScheme=default,UploadSpeed=921600,DebugLevel=none,EraseFlash=none"

# Espressif's built-in USB Serial/JTAG controller (ESP32-C3 native USB).
ESPRESSIF_USB_VID="303A"
ESPRESSIF_USB_PID="1001"

if [[ -t 1 ]]; then
  BOLD=$'\e[1m'; RED=$'\e[31m'; GREEN=$'\e[32m'; YELLOW=$'\e[33m'; RESET=$'\e[0m'
else
  BOLD=""; RED=""; GREEN=""; YELLOW=""; RESET=""
fi

info() { printf '%s\n' "$*"; }
ok() { printf '%s✔%s %s\n' "$GREEN" "$RESET" "$*"; }
warn() { printf '%s!%s %s\n' "$YELLOW" "$RESET" "$*" >&2; }
die() { printf '%s✘ %s%s\n' "$RED" "$*" "$RESET" >&2; exit 1; }
heading() { printf '\n%s== %s ==%s\n' "$BOLD" "$*" "$RESET"; }

require_macos() {
  [[ "$(uname -s)" == "Darwin" ]] || die "These scripts are written for macOS (found $(uname -s))."
}

require_arduino_cli() {
  command -v arduino-cli >/dev/null 2>&1 || die "arduino-cli not found. Run scripts/setup-macos.sh first."
}

# Prints "port<TAB>vid:pid<TAB>description" for every USB serial port that
# arduino-cli can see. Uses python3 (preinstalled on macOS) to read JSON.
list_serial_ports() {
  arduino-cli board list --format json 2>/dev/null | python3 -c '
import json, sys
try:
    data = json.load(sys.stdin)
except ValueError:
    sys.exit(0)
ports = data.get("detected_ports", []) if isinstance(data, dict) else data
for entry in ports or []:
    port = entry.get("port", {})
    addr = port.get("address", "")
    if not addr.startswith("/dev/"):
        continue
    props = port.get("properties", {}) or {}
    vid = (props.get("vid") or "").upper().replace("0X", "")
    pid = (props.get("pid") or "").upper().replace("0X", "")
    label = port.get("label", "") or port.get("protocol_label", "")
    print(f"{addr}\t{vid}:{pid}\t{label}")
'
}

# Ports that belong to an ESP32-C3's native USB Serial/JTAG controller.
list_esp32c3_ports() {
  list_serial_ports | awk -F'\t' -v id="${ESPRESSIF_USB_VID}:${ESPRESSIF_USB_PID}" '$2 == id { print $1 }'
}

# Fills the global array CANDIDATES with list_esp32c3_ports (bash 3.2 has no mapfile).
find_esp32c3_ports() {
  CANDIDATES=()
  local line
  while IFS= read -r line; do
    [[ -n "$line" ]] && CANDIDATES+=("$line")
  done < <(list_esp32c3_ports)
  return 0
}

# Validates a user-supplied port: must be an existing /dev/cu.* character device.
check_port() {
  local port="$1"
  [[ "$port" == /dev/cu.* ]] || die "Use the /dev/cu.* device on macOS (got '$port'). /dev/tty.* devices wait for carrier detect."
  [[ -c "$port" ]] || die "Port $port does not exist. Is the board plugged in with a data-capable USB cable?"
}
