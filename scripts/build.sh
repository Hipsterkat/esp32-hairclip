#!/usr/bin/env bash
# Compiles the firmware with the pinned board options and writes the images,
# their SHA-256 checksums and the flash layout to build/firmware/.
#
#   scripts/build.sh            build (warnings in this project's code fail the build)
#   scripts/build.sh --clean    delete build/ first
set -euo pipefail
source "$(dirname "$0")/common.sh"

for arg in "$@"; do
  case "$arg" in
    --clean) rm -rf "$ROOT/build" ;;
    -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
    *) die "Unknown option: $arg" ;;
  esac
done

require_arduino_cli

heading "Toolchain"
core="$(arduino-cli core list 2>/dev/null | awk '$1 == "esp32:esp32" { print $2 }')"
u8g2="$(arduino-cli lib list 2>/dev/null | awk '$1 == "U8g2" { print $2 }')"
[[ -n "$core" ]] || die "The esp32:esp32 core is not installed. Run scripts/setup-macos.sh."
[[ -n "$u8g2" ]] || die "The U8g2 library is not installed. Run scripts/setup-macos.sh."
[[ "$core" == "$ESP32_CORE_VERSION" ]] && ok "esp32:esp32 $core" || warn "esp32:esp32 $core (tested with $ESP32_CORE_VERSION)"
[[ "$u8g2" == "$U8G2_VERSION" ]] && ok "U8g2 $u8g2" || warn "U8g2 $u8g2 (tested with $U8G2_VERSION)"

heading "Generated web/font headers"
u8g2_dir="$(arduino-cli lib list --format json | python3 -c '
import json, sys
data = json.load(sys.stdin)
for lib in (data.get("installed_libraries") if isinstance(data, dict) else data) or []:
    l = lib.get("library", {})
    if l.get("name") == "U8g2":
        print(l.get("install_dir", ""))
')"
export U8G2_FONTS_C="$u8g2_dir/src/clib/u8g2_fonts.c"
python3 "$ROOT/tools/generate_assets.py" --check \
  || die "Regenerate with: python3 tools/generate_assets.py   (then rebuild)"

heading "Compiling"
# Firmware build ID shown by `status` and /api/status: the git commit, marked
# -dirty when the sketch folder has uncommitted changes.
BUILD_ID="$(git -C "$ROOT" rev-parse --short=7 HEAD 2>/dev/null || echo unknown)"
if [[ "$BUILD_ID" != "unknown" && -n "$(git -C "$ROOT" status --porcelain -- hairclip 2>/dev/null)" ]]; then
  BUILD_ID="${BUILD_ID}-dirty"
fi
info "FQBN: $FQBN"
info "Build ID: $BUILD_ID"
mkdir -p "$BUILD_DIR"
LOG="$ROOT/build/compile.log"
if ! arduino-cli compile --fqbn "$FQBN" --warnings all --output-dir "$BUILD_DIR" \
    --build-property "compiler.cpp.extra_flags=-DHAIRCLIP_BUILD_ID=$BUILD_ID" "$SKETCH_DIR" >"$LOG" 2>&1; then
  tail -40 "$LOG" >&2
  echo >&2
  warn "Build failed. Full log: $LOG"
  warn "Common causes: missing core/library (run scripts/setup-macos.sh), an edit with a"
  warn "syntax error, or web/ changed without running tools/generate_assets.py."
  exit 1
fi
own_warnings="$(grep -E "hairclip/[A-Za-z_]+\.(cpp|h|ino):[0-9]+:[0-9]+: warning" "$LOG" || true)"
if [[ -n "$own_warnings" ]]; then
  echo "$own_warnings" >&2
  die "Compiler warnings in project code (see above). Full log: $LOG"
fi
grep -E "^(Sketch uses|Global variables)" "$LOG" | sed 's/^/  /'
ok "Compiled without warnings in project code"

heading "Images"
cd "$BUILD_DIR"
shasum -a 256 hairclip.ino.bootloader.bin hairclip.ino.partitions.bin hairclip.ino.bin hairclip.ino.merged.bin > SHA256SUMS
cat > FLASH_LAYOUT.txt <<EOF
ESP32-C3 flash layout for this build (FQBN $FQBN)
  0x0      hairclip.ino.bootloader.bin
  0x8000   hairclip.ino.partitions.bin
  0xe000   boot_app0.bin (from the esp32 core)
  0x10000  hairclip.ino.bin
or the complete 4 MB image at 0x0:
  0x0      hairclip.ino.merged.bin
EOF
sed 's/^/  /' SHA256SUMS
ok "Firmware ready in ${BUILD_DIR#"$ROOT"/} (flash with scripts/flash.sh)"
