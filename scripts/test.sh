#!/usr/bin/env bash
# Runs every check that does not need hardware:
#   host C++ unit tests (with AddressSanitizer/UBSan), Python tests (fonts,
#   generated assets, static checks, C++/JS preview parity), and with
#   --firmware a full ESP32-C3 compile.
#
#   scripts/test.sh             unit + static tests
#   scripts/test.sh --firmware  also compile the firmware (scripts/build.sh)
set -euo pipefail
source "$(dirname "$0")/common.sh"

FIRMWARE=0
for arg in "$@"; do
  case "$arg" in
    --firmware) FIRMWARE=1 ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) die "Unknown option: $arg" ;;
  esac
done

CXX="${CXX:-$(command -v clang++ || command -v g++ || true)}"
[[ -n "$CXX" ]] || die "No C++ compiler (install the Xcode Command Line Tools: xcode-select --install)."

heading "Host unit tests"
mkdir -p "$ROOT/build/host"
SANITIZE="-fsanitize=address,undefined -fno-sanitize-recover=all"
[[ "${NO_SANITIZERS:-0}" == "1" ]] && SANITIZE=""
# shellcheck disable=SC2086
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Werror $SANITIZE \
  -I "$SKETCH_DIR" \
  "$ROOT"/tests/host/main.cpp "$ROOT"/tests/host/test_*.cpp \
  "$SKETCH_DIR"/text.cpp "$SKETCH_DIR"/settings.cpp "$SKETCH_DIR"/storage.cpp "$SKETCH_DIR"/layout.cpp \
  "$SKETCH_DIR"/button.cpp "$SKETCH_DIR"/wifi_policy.cpp "$SKETCH_DIR"/json_writer.cpp \
  -o "$ROOT/build/host/unit_tests"
"$ROOT/build/host/unit_tests"
ok "C++ unit tests passed"

heading "Python tests"
command -v node >/dev/null 2>&1 || warn "node not found: the browser-preview parity tests will be skipped"
(cd "$ROOT/tests/python" && python3 -m unittest discover -v 2>&1 | tail -n +1) | grep -Ev "^\s*$" | tail -60
(cd "$ROOT/tests/python" && python3 -m unittest discover >/dev/null 2>&1) || die "Python tests failed (output above)"
ok "Python tests passed"

if [[ $FIRMWARE -eq 1 ]]; then
  "$ROOT/scripts/build.sh"
fi
heading "Done"
ok "All software checks passed. Hardware behaviour is NOT covered: see docs/HARDWARE_TEST_CHECKLIST.md"
