# Development notes

## Repository layout

```
hairclip/                Arduino sketch (folder name must match hairclip.ino)
  hairclip.ino           setup()/loop(), BOOT button sampling, serial console
  config.h               pins, Wi-Fi defaults, timeouts, limits
  display.*              U8g2 + render task (device only)
  wifi_manager.*         access point start/stop (device only)
  web_ui.*               HTTP routes and JSON handlers (device only)
  storage.*              NVS persistence; NvsStore adapter under #ifdef ARDUINO
  text.*                 UTF-8, message normalization, displayable set, escaping
  settings.*             defaults, limits, strict parsing
  layout.*               line placement, two-line split, scroll timeline
  button.*, wifi_policy.*, json_writer.*
  generated_fonts.h      GENERATED: font choice + metrics
  generated_web.h        GENERATED: gzipped control page + CSP
  secrets.example.h      template for the git-ignored secrets.h
web/                     control page sources (index.html, app.js, preview.js)
tools/                   u8g2font.py, generate_assets.py, dev_server.py
tests/host/              C++ unit tests and the parity dump tool
tests/python/, tests/js/ font, asset, static and parity tests
scripts/                 setup-macos, build, flash, monitor, test
docs/                    API, hardware checklist, screenshots
```

The sketch lives in its own folder so Arduino tooling never compiles the
tests or tools. All files ending in `.cpp`/`.ino` in `hairclip/` are compiled.

## Pinned toolchain

| Component | Version |
|---|---|
| arduino-cli | 1.5.1 (other 1.x versions should work) |
| Arduino-ESP32 core (`esp32:esp32`) | 3.3.12 |
| U8g2 | 2.36.19 |
| Board | ESP32C3 Dev Module, FQBN in `scripts/common.sh` |

`scripts/setup-macos.sh` installs exactly these versions. `scripts/build.sh`
warns if different ones are installed. The CI workflow installs the same
versions.

## Pure modules and tests

`text`, `settings`, `layout`, `button`, `wifi_policy`, `json_writer` and the
portable part of `storage` include no Arduino headers. `scripts/test.sh`
compiles them on the host with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion
-Werror` and AddressSanitizer/UBSan, together with `tests/host/test_*.cpp`.
Keep hardware access in `display`, `wifi_manager`, `web_ui` and `hairclip.ino`
so the logic stays testable.

`tests/python/test_static.py` enforces some of these rules: no long `delay()`,
no `while (!Serial)`, GPIO9 only read, explicit I²C pins, no Arduino headers
in pure modules, no printed password, no hard-coded serial ports, both tasks
on the task watchdog, no unbounded waits in the render task, and no manual
`delete`/`malloc` (use static buffers or `std::unique_ptr`).

The task watchdog (`WATCHDOG_TIMEOUT_MS`, 30 s) watches the Arduino loop and
the display task. Code on either task must never block for that long; the
render task wakes at least every `DISPLAY_MAX_SLEEP_MS` to feed it.

## Generated headers

`generated_fonts.h` and `generated_web.h` are committed so the firmware builds
without Python. Regenerate after changing `web/`, the `CHARMAP` table in
`hairclip/text.cpp`, or the font list in `tools/generate_assets.py`:

```bash
python3 tools/generate_assets.py
```

`scripts/build.sh`, the tests and CI run `--check` and fail if the headers are
stale. The check compares a SHA-256 of the uncompressed page rather than the
gzip bytes, because gzip output can differ between zlib builds.

The browser preview (`web/preview.js`) re-implements U8g2's glyph decoder, the
text normalization and the layout rules. `tests/python/test_parity.py` runs
the same inputs through the C++ code (`tests/host/parity_dump.cpp`), the
JavaScript and the Python font reader, and requires identical results. If you
change `text.cpp` or `layout.cpp`, change `preview.js` the same way.

## Working on the web page without hardware

```bash
python3 tools/dev_server.py
```

This serves the exact embedded page at http://127.0.0.1:8080 with its CSP,
against an in-memory mock of the API. Message normalization uses the
firmware's own C++ code. It is a mock: timing, flash writes and Wi-Fi are
simulated.

## Continuous integration

`.github/workflows/ci.yml` runs on pushes and pull requests. It installs the
pinned toolchain, runs `scripts/test.sh`, compiles the firmware with
`scripts/build.sh`, and uploads the images with `SHA256SUMS` and
`FLASH_LAYOUT.txt` as an artifact. CI never needs hardware; a green run says
nothing about how the clip behaves. Hardware results belong in
[HARDWARE_TEST_CHECKLIST.md](HARDWARE_TEST_CHECKLIST.md).

## Releases

1. Bump `HAIRCLIP_FIRMWARE_VERSION` in `hairclip/config.h`.
2. Run `scripts/test.sh --firmware`.
3. Record the hardware checklist results for this version.
4. Tag (`git tag v1.0.1`) and attach `build/firmware/hairclip.ino.merged.bin`
   (flash at offset 0x0), the separate images, `SHA256SUMS` and
   `FLASH_LAYOUT.txt`.

Recovery without Arduino tools: after entering download mode, flash
`hairclip.ino.merged.bin` at `0x0` with esptool v5
(`esptool --chip esp32c3 write-flash 0x0 hairclip.ino.merged.bin`; esptool v4
spells it `esptool.py … write_flash`).

## Commit messages

Use the imperative and say why, for example `Clamp scroll speed from storage
to 1-10`. Keep hardware-test results in the checklist, not in commit messages.
