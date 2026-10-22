# Physical hardware test checklist

Items marked **PASS** (☑) or **PARTIAL** (◐) were observed on real hardware on
the date shown (see the test log at the end). **Every other item is NOT
HARDWARE TESTED.** Record your own results in the *Result* column; a ticked
box means a real measurement or observation, not an expectation. Stop at the
first failure.

The gates follow the bring-up order of the illustrated build guide (A–H).
Read [HARDWARE.md](../HARDWARE.md) first.

Useful tools: a fused multimeter, a current-limited adjustable bench supply, a
USB data cable, and a phone.

## Gate A–B: parts and charger, no power connected

| ✓ | Check | How | Pass condition | Result |
|---|---|---|---|---|
| ☐ | Board identity | Read the markings | ESP32-C3 SuperMini Plus; 5V/3V3/G/5/6 labels present | NOT HARDWARE TESTED |
| ☐ | Regulator and USB path | Identify the LDO marking; diode test from USB VBUS to the 5V pad | Regulator type recorded; blocking diode present or absent recorded | NOT HARDWARE TESTED |
| ☐ | OLED pin order | Read the module's silkscreen | VCC/GND/SCL/SDA order recorded | NOT HARDWARE TESTED |
| ☐ | OLED pull-ups | Resistance SDA→VCC and SCL→VCC on the unpowered module | About 2–10 kΩ each (if missing or higher, add 4.7 kΩ to 3V3 or use 100 kHz) | NOT HARDWARE TESTED |
| ☐ | OLED VCC from 3V3 | Continuity from OLED VCC to the SuperMini 3V3 pin, not 5V | Connected to 3V3 only | NOT HARDWARE TESTED |
| ☐ | Cell limits | Cell datasheet | Max charge current, cut-off and temperature range recorded | NOT HARDWARE TESTED |
| ☐ | R_PROG | Measure the resistance between TP4056 pin 2 and GND, all power removed | Matches the value chosen for the cell (10 kΩ ≈ 130 mA, 12 kΩ ≈ 100 mA) | NOT HARDWARE TESTED |
| ☐ | Switch | Continuity on the common pin in both positions | ON connects OUT+ to the 5V wire; OFF opens it | NOT HARDWARE TESTED |
| ☐ | No shorts | Resistance B+↔B−, OUT+↔OUT−, 3V3↔G, SDA↔SCL | No unexpected low resistance | NOT HARDWARE TESTED |
| ☐ | B− and OUT− separate | Continuity | Not linked outside the module | NOT HARDWARE TESTED |

## Gate C: flashing on USB power, cell unplugged

| ✓ | Check | How | Pass condition | Result |
|---|---|---|---|---|
| ☑ | Port appears | `scripts/setup-macos.sh --check` | `/dev/cu.usbmodem…` with `303A:1001` | PASS 2026-10-22: `/dev/cu.usbmodem1101`, `303A:1001`, chip ESP32-C3 (QFN32) rev v0.4, 4 MB embedded flash |
| ☑ | Upload | `scripts/flash.sh` | "Upload finished", no errors | PASS 2026-10-22: commit f7f6bcd, all 4 regions "Hash of data verified"; a second upload was confirmed with `esptool verify-flash` (digest matched) |
| ☑ | Boot report | `scripts/monitor.sh` | Banner printed, reset reason `power-on` or `usb` | PASS 2026-10-22: `status` reply complete, reset `usb`, 80 MHz, NVS ok, 170 KB heap free. The banner is cut off if no monitor is open at boot (by design: output is never blocking) |
| ☐ | Stable 3V3 | Meter on 3V3 during boot and Wi-Fi | 3.2–3.4 V, no resets | NOT HARDWARE TESTED |

## Gate D: display, I²C, button and Wi-Fi on USB power

| ✓ | Check | How | Pass condition | Result |
|---|---|---|---|---|
| ☑ | SSD1306 detected at 0x3C | `status` in the serial console | `i2c … lines idled HIGH … devices: 0x3C`; `oled detected at 0x3C` | PASS 2026-10-23, build b0eac5f: lines idled HIGH, only device 0x3C, `oled detected at 0x3C`. (Without the screen it correctly reported LOW and no devices.) |
| ◐ | Watchdog stays quiet | Use the clip normally for 30 min, then check *Last reset* | No `task-watchdog` or `interrupt-watchdog` resets | PARTIAL: no watchdog resets during about 15 min (2026-10-22) and about 20 min with about 6000 scroll frames (2026-10-23, build b0eac5f) |
| ☑ | Correct GPIO mapping | Same line | `SDA=GPIO5 SCL=GPIO6`; the display works with this wiring | PASS 2026-10-23, build b0eac5f |
| ☑ | Display initialization | Look at the panel | "Hello!" centered, crisp, not shifted or mirrored; Wi-Fi icon top-right | PASS 2026-10-23, build b0eac5f: saved message "salut" crisp, upright, centered; icon top-right |
| ☑ | Fonts and diacritics | Save `Ăă Ââ Îî Șș Țț` in each font size | Every accent and comma visible | PASS 2026-10-23, build b0eac5f: medium `ĂÂÎȘȚ ăâîșț`, large `Ăâî Șț`, small (two-line sample with ă ț Ș ș Ț): all marks complete |
| ☑ | Scrolling | Save a 60-character message | Smooth scroll, pause at the start, no tearing | PASS 2026-10-23, build b0eac5f: smooth and readable; speed 10 clearly faster; frame counter measured 38.4 frames/s with a 1.0–1.1 s pause every loop |
| ☑ | Two lines | Small font + Two lines + `Bună dimineața, Ștefan și Țărani` | Two centered lines, nothing clipped | PASS 2026-10-23, build b0eac5f |
| ☑ | Brightness / rotate / invert | Change each on the page | Visible change; rotation keeps text upright when the clip is flipped | PASS 2026-10-23, build b0eac5f: rotation 180 upside down, inverted colours, brightness 100 visibly brighter (set over the serial console; brightness 0 not tried) |
| ☑ | Wi-Fi AP visible | Phone Wi-Fi list | "Hairclip" listed within 10 s of boot | PASS 2026-10-22 (board only, USB power): seen by a Mac Wi-Fi scan on channel 1, WPA2-Personal |
| ◐ | Mobile browser connection | iPhone and Android: join, open http://192.168.4.1 | Page loads, status "Connected", preview matches the panel | PARTIAL 2026-10-22: one phone joined, loaded the page and saved (device showed 1 client); second platform and panel comparison not done yet |
| ☑ | Message saves | Type, Save | Toast "Saved"; the panel updates within 1 s | PASS: 2026-10-22 the page showed "Saved" and `status` had the new message; 2026-10-23, build b0eac5f the panel updated immediately for each new message (set over the serial console) |
| ◐ | Persists after reboot | Press RESET, then unplug and replug USB | The same message and settings return | PARTIAL: message and font survived a software restart, firmware updates and a USB unplug/replug (power-on reset, 2026-10-23, build b0eac5f); RESET button not tried yet |
| ☑ | BOOT turns Wi-Fi off | Hold BOOT 1.5 s with Wi-Fi on | "Wi-Fi OFF" toast, icon gone, network disappears | PASS 2026-10-22: `[wifi] access point off`, status off, driver memory released (toast/icon need the panel) |
| ☑ | BOOT turns Wi-Fi on | Hold BOOT 1.5 s with Wi-Fi off | "Wi-Fi ON 192.168.4.1" toast, network back | PASS 2026-10-22: `[wifi] access point "Hairclip" up`, network visible again in a Wi-Fi scan. PASS 2026-10-23, build b0eac5f: toast "Wi-Fi ON / 192.168.4.1" shown, then the message with the icon |
| ☑ | One toggle per hold | Hold BOOT for 5 s | Exactly one toggle | PASS 2026-10-22: a 4–5 s hold switched Wi-Fi on once and left it on |
| ☐ | Short press ignored | Tap BOOT | Nothing changes | NOT HARDWARE TESTED |
| ☐ | Download mode still works | Hold BOOT, tap RESET, release | Board enumerates in download mode; flashing works | NOT HARDWARE TESTED |
| ☑ | Automatic AP shutdown | Leave the clip alone for 3 min with no page open | Network gone after about 180 s; message still shown | PASS 2026-10-22: on at 179 s uptime, off by 191 s (180 s + 3 s grace); network gone from a Wi-Fi scan; free heap rose ~40 KB as the driver stopped |
| ◐ | Activity extends Wi-Fi | Type on the page now and then for 5 min | Wi-Fi stays on; countdown resets | PARTIAL 2026-10-22: using the page reset the countdown to 177 s; the 5-minute run not done yet |
| ☐ | Page "Wi-Fi off" button | Tap it, confirm | Notice shown, network drops after about 3 s | NOT HARDWARE TESTED |
| ☐ | Animation during requests | Save repeatedly while text scrolls | Scrolling continues without long freezes | NOT HARDWARE TESTED |

## Gate E: battery voltage range on a bench supply (cell removed)

Connect a current-limited supply (limit about 0.5 A) in place of the cell, at
the B+/B− terminals, with the switch ON and **no USB connected**.

| ✓ | Check | How | Pass condition | Result |
|---|---|---|---|---|
| ☐ | Full cell | 4.2 V, Wi-Fi on, page open | No resets; 3V3 within 3.0–3.6 V | NOT HARDWARE TESTED |
| ☐ | Nominal | 3.7 V, same | Same | NOT HARDWARE TESTED |
| ☐ | Low | Step down 0.05 V at a time to 3.3 V | Record the voltage where resets or a 3V3 below 3.0 V start | NOT HARDWARE TESTED |
| ☐ | Wi-Fi off | Repeat with Wi-Fi off | Record the lowest working voltage | NOT HARDWARE TESTED |
| ☐ | Brownout counter | After resets, check *Last reset* | Shows `brownout` with a count | NOT HARDWARE TESTED |
| ☐ | Current | Supply readout or ammeter | Record mA: Wi-Fi on, Wi-Fi off static, Wi-Fi off scrolling | NOT HARDWARE TESTED |

## Gate F: charging (TP4056 USB only, switch OFF)

| ✓ | Check | How | Pass condition | Result |
|---|---|---|---|---|
| ☐ | Charge current | Ammeter in series: B+ → meter → cell + | Within the cell's limit and the value expected for R_PROG | NOT HARDWARE TESTED |
| ☐ | Termination | Watch a full charge | Stops near 4.2 V; current falls below about 15 mA | NOT HARDWARE TESTED |
| ☐ | Temperature | Touch or thermometer | Cell and charger only mildly warm | NOT HARDWARE TESTED |
| ☐ | Never both USB ports | Procedure check | Charger USB only, SuperMini USB empty | NOT HARDWARE TESTED |

## Gate G: battery only, on the bench

| ✓ | Check | How | Pass condition | Result |
|---|---|---|---|---|
| ☐ | Battery-only startup | Charged cell, no USB, switch ON | Boots, message shown, Wi-Fi visible | NOT HARDWARE TESTED |
| ☐ | Persistence on battery | Save a message, switch OFF and ON | The message returns | NOT HARDWARE TESTED |
| ☐ | Switch OFF stops the load | Switch OFF | Display off; no current drawn by the clip | NOT HARDWARE TESTED |
| ☐ | Runtime | Full cell, Wi-Fi off, run until it stops | Record hours; compare with the estimate of 3–9 h | NOT HARDWARE TESTED |

## Gate H: finished enclosure

| ✓ | Check | How | Pass condition | Result |
|---|---|---|---|---|
| ☐ | Insulation | Meter between the barrette and every conductor | Open circuit | NOT HARDWARE TESTED |
| ☐ | Flex test | Open and close the clip 50 times, then repeat the insulation check | Still open circuit; no stress on the pouch or wires | NOT HARDWARE TESTED |
| ☐ | Controls reachable | Try BOOT, RESET and the switch | All operable | NOT HARDWARE TESTED |
| ☐ | Wi-Fi range | Phone at 1 m and 3 m | Page usable | NOT HARDWARE TESTED |
| ☐ | Heat | 30 min with Wi-Fi on, then 30 min off | No hot spots on the side touching the head | NOT HARDWARE TESTED |

## Test log

### 2026-10-22: board only, USB power, no screen, no battery

* Board: ESP32-C3 (QFN32) revision v0.4, 4 MB embedded flash (XMC), native
  USB-Serial/JTAG at `/dev/cu.usbmodem1101`.
* Firmware: commit f7f6bcd, then the I²C line-check fix (see below).
* Passed: port detection, upload (hash verified), boot report, 80 MHz clock,
  NVS, Wi-Fi AP on air (channel 1, WPA2), 3-minute auto-off, BOOT on and off,
  one toggle per hold, a phone saving a message, the message and font
  surviving a restart and a firmware update.
* Found and fixed: with nothing on GPIO5/6, the first version of the I²C
  line check could report "lines idled HIGH" after a software restart,
  because a floating line kept its charge. It now drains the lines with the
  internal pull-down before reading. Re-tested: LOW after a flash and after a
  restart.
* Note: a macOS Wi-Fi scan sometimes returns only part of the networks; repeat
  the scan before concluding that the access point is missing.
* Not tested yet: everything needing the OLED, the 3V3 rail measurement,
  the physical RESET button, short BOOT presses, the page's Wi-Fi-off button,
  and gates E–H (battery, charging, enclosure).

### 2026-10-23: OLED soldered, USB power, no battery

* Firmware: build b0eac5f (adds the serial `msg`/`set` commands used here).
* Passed: I²C lines HIGH and the SSD1306 detected at 0x3C; crisp, upright,
  centered text; Romanian marks complete in all three fonts; two-line layout;
  smooth scrolling (38.4 frames/s, 1.0–1.1 s pause per loop, faster at speed
  10); rotation 180, invert and brightness 100; Wi-Fi icon; BOOT toast
  "Wi-Fi ON / 192.168.4.1"; message persisted through a USB unplug/replug.
* About 6000 frames drawn without a hang or watchdog reset.
* Not tested yet: the 3V3 rail under load, the RESET button, short BOOT
  presses, the page's Wi-Fi-off button, animation while the web page saves,
  and gates E–H (battery, charging, enclosure).
