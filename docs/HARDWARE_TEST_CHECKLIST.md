# Physical hardware test checklist

**Every item below is NOT HARDWARE TESTED.** The firmware has been compiled and
unit-tested on a computer only. Record your own results in the *Result*
column; a ticked box means a real measurement or observation, not an
expectation. Stop at the first failure.

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
| ☐ | Cell limits | Cell datasheet | Max charge current, cut-off and temperature range recorded | NOT HARDWARE TESTED |
| ☐ | R_PROG | Measure the resistance between TP4056 pin 2 and GND, all power removed | Matches the value chosen for the cell (10 kΩ ≈ 130 mA, 12 kΩ ≈ 100 mA) | NOT HARDWARE TESTED |
| ☐ | Switch | Continuity on the common pin in both positions | ON connects OUT+ to the 5V wire; OFF opens it | NOT HARDWARE TESTED |
| ☐ | No shorts | Resistance B+↔B−, OUT+↔OUT−, 3V3↔G, SDA↔SCL | No unexpected low resistance | NOT HARDWARE TESTED |
| ☐ | B− and OUT− separate | Continuity | Not linked outside the module | NOT HARDWARE TESTED |

## Gate C: flashing on USB power, cell unplugged

| ✓ | Check | How | Pass condition | Result |
|---|---|---|---|---|
| ☐ | Port appears | `scripts/setup-macos.sh --check` | `/dev/cu.usbmodem…` with `303A:1001` | NOT HARDWARE TESTED |
| ☐ | Upload | `scripts/flash.sh` | "Upload finished", no errors | NOT HARDWARE TESTED |
| ☐ | Boot report | `scripts/monitor.sh` | Banner printed, reset reason `power-on` or `usb` | NOT HARDWARE TESTED |
| ☐ | Stable 3V3 | Meter on 3V3 during boot and Wi-Fi | 3.2–3.4 V, no resets | NOT HARDWARE TESTED |

## Gate D: display, I²C, button and Wi-Fi on USB power

| ✓ | Check | How | Pass condition | Result |
|---|---|---|---|---|
| ☐ | SSD1306 detected at 0x3C | `status` in the serial console | `i2c … devices: 0x3C`; `oled detected at 0x3C` | NOT HARDWARE TESTED |
| ☐ | Correct GPIO mapping | Same line | `SDA=GPIO5 SCL=GPIO6`; the display works with this wiring | NOT HARDWARE TESTED |
| ☐ | Display initialization | Look at the panel | "Hello!" centered, crisp, not shifted or mirrored; Wi-Fi icon top-right | NOT HARDWARE TESTED |
| ☐ | Fonts and diacritics | Save `Ăă Ââ Îî Șș Țț` in each font size | Every accent and comma visible | NOT HARDWARE TESTED |
| ☐ | Scrolling | Save a 60-character message | Smooth scroll, pause at the start, no tearing | NOT HARDWARE TESTED |
| ☐ | Two lines | Small font + Two lines + `Bună dimineața, Ștefan și Țărani` | Two centered lines, nothing clipped | NOT HARDWARE TESTED |
| ☐ | Brightness / rotate / invert | Change each on the page | Visible change; rotation keeps text upright when the clip is flipped | NOT HARDWARE TESTED |
| ☐ | Wi-Fi AP visible | Phone Wi-Fi list | "Hairclip" listed within 10 s of boot | NOT HARDWARE TESTED |
| ☐ | Mobile browser connection | iPhone and Android: join, open http://192.168.4.1 | Page loads, status "Connected", preview matches the panel | NOT HARDWARE TESTED |
| ☐ | Message saves | Type, Save | Toast "Saved"; the panel updates within 1 s | NOT HARDWARE TESTED |
| ☐ | Persists after reboot | Press RESET, then unplug and replug USB | The same message and settings return | NOT HARDWARE TESTED |
| ☐ | BOOT turns Wi-Fi off | Hold BOOT 1.5 s with Wi-Fi on | "Wi-Fi OFF" toast, icon gone, network disappears | NOT HARDWARE TESTED |
| ☐ | BOOT turns Wi-Fi on | Hold BOOT 1.5 s with Wi-Fi off | "Wi-Fi ON 192.168.4.1" toast, network back | NOT HARDWARE TESTED |
| ☐ | One toggle per hold | Hold BOOT for 5 s | Exactly one toggle | NOT HARDWARE TESTED |
| ☐ | Short press ignored | Tap BOOT | Nothing changes | NOT HARDWARE TESTED |
| ☐ | Download mode still works | Hold BOOT, tap RESET, release | Board enumerates in download mode; flashing works | NOT HARDWARE TESTED |
| ☐ | Automatic AP shutdown | Leave the clip alone for 3 min with no page open | Network gone after about 180 s; message still shown | NOT HARDWARE TESTED |
| ☐ | Activity extends Wi-Fi | Type on the page now and then for 5 min | Wi-Fi stays on; countdown resets | NOT HARDWARE TESTED |
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
