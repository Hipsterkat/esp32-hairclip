# Troubleshooting

Find the failing layer before changing anything. Disconnect power before
moving wires, work on the bench outside the enclosure, and change one thing at
a time. The serial console helps with most problems:

```bash
scripts/monitor.sh
```

Type `status` and press Enter. The report shows the I²C devices found, the OLED
address, the Wi-Fi state, the BOOT button level, the last reset reason and the
current settings. The Wi-Fi password is never printed.

## Blank OLED

1. **Power first.** With USB connected, measure 3.3 V between the OLED's VCC
   and GND pins. Many 0.91″ modules order the pins GND-VCC-SCL-SDA, some
   VCC-GND-SCL-SDA: follow the **labels** on your module.
2. **Wiring.** SDA → GPIO5 and SCL → GPIO6 (not GPIO8/9, the Arduino
   defaults). Check continuity end to end, and for solder bridges between
   neighbouring pads.
3. **Does it answer?** Run `status`. The `i2c` line lists the addresses that
   answered:
   * `0x3C` (or `0x3D`): the display is on the bus. If it still shows nothing,
     check `brightness` (0% is very dim) and `invert`, and press RESET.
   * `none`: no device answered. That points to wiring, power, swapped
     SDA/SCL, or a module without pull-up resistors (most have them).
4. **Wrong controller?** Some cheap "SSD1306" modules use an SH1106 or a
   different resolution. A picture shifted by a couple of pixels, or garbage in
   one half, suggests that. This firmware supports the SSD1306 128×32 only.
5. **Empty message with Wi-Fi off:** the panel is deliberately put to sleep
   when there is nothing to show. Hold BOOT for 1.5 s to bring Wi-Fi back
   (the icon appears), then save a message.

## No serial port

* Use the **SuperMini's** USB-C port, not the TP4056 charger's.
* Use a cable that carries data. Many USB-C cables are charge-only; try one
  that works with another device's data transfer.
* Connect directly rather than through an unpowered hub.
* List the ports. An ESP32-C3 appears as `/dev/cu.usbmodem…` with USB ID
  `303A:1001`:

```bash
scripts/setup-macos.sh --check
```

* Still missing: put the chip in **download mode**. Hold BOOT, tap RESET,
  release BOOT, or hold BOOT while plugging in. The ROM bootloader always
  enumerates over USB, even when the running firmware has crashed.
* *System Information → USB* should list a "USB JTAG/serial debug unit".
  The ESP32-C3's native USB needs no driver on macOS.

## Upload errors

| Message | What to do |
|---|---|
| `Failed to connect` / `No serial data received` | Close every serial monitor (Arduino IDE, `screen`, `scripts/monitor.sh`). Enter download mode (BOOT + RESET) and run `scripts/flash.sh` again. |
| Port disappears or changes name during upload | Normal when the chip switches into the bootloader; rerun `scripts/flash.sh` to re-detect it. |
| `A fatal error occurred: Invalid head of packet` | Lower the speed: change `UploadSpeed=921600` to `115200` in `scripts/common.sh`, then try another cable or port. |
| `Wrong chip` / "This chip is ESP32, not ESP32-C3" | You are flashing a different board; check the port. |
| `Permission denied` on the port | Another program holds the port; close it, or unplug and replug the board. |
| Upload succeeds but nothing runs | Press RESET. If the serial console shows boot loops, see *Resets* below. |

## Wi-Fi network not visible

* The icon (four small bars) in the OLED's top-right corner means the access
  point is on. No icon means Wi-Fi is off: hold BOOT for 1.5 s.
* Wi-Fi turns itself off 3 minutes after the last use. That is intended.
* Move the clip away from metal and the battery. A metal barrette right under
  the antenna can shorten the range a lot.
* Some SuperMini boards have poorly tuned antennas. The firmware already uses
  low transmit power (8.5 dBm), which often helps. If you change
  `AP_TX_POWER_QDBM`, test the range again.
* `status` on the serial console shows the Wi-Fi state, and whether starting
  the access point failed.

## Page won't load

* Type **http://192.168.4.1** in full, without `https`.
* **iPhone:** make sure the phone is still on *Hairclip*. iOS may switch back to
  a known network that has internet.
* **Android:** when asked "no internet, stay connected?", choose *Stay
  connected*. If the page still fails, disable mobile data for a moment;
  otherwise Android may send the request over mobile data.
* The page says "Can't reach the hairclip": the access point turned off
  (timeout or BOOT). Hold BOOT for 1.5 s, rejoin the network and reload.
* An old copy of the page after a firmware update: reload the page.

## Message not saved or lost after restart

* The page reports the result. "Shown on the display, but saving to flash
  failed" means the flash write failed: the message stays visible until the
  next restart. Try again; if it repeats, run `factory-reset` in the serial
  console.
* Messages over 120 characters or 256 bytes are refused, never cut silently.
* After a restart, `status` shows what was loaded. The boot report also says
  whether stored settings had to be repaired.

## BOOT button does nothing

* Hold for a full **1.5 s**. Short presses are ignored on purpose.
* After a press is detected, release the button before the next toggle.
* A button held while the board starts is ignored until it has been released
  once. Holding BOOT during RESET enters download mode instead (the display
  stays dark, which is normal).
* `status` prints `boot btn GPIO9 pressed/released` so you can check the
  wiring of a replacement button.

## Resets when running on the battery

Typical signs are the display flickering or restarting during Wi-Fi use, and
"brownout" in *Last reset* on the page or in `status`.

* This is the voltage-headroom limit in [HARDWARE.md §1](HARDWARE.md#1-lipo-into-the-5v-pin-voltage-headroom):
  the cell (or wiring, or a series diode) cannot keep the 3.3 V rail up
  during Wi-Fi bursts.
* Charge the cell, keep Wi-Fi use short, and measure the 3V3 rail under load
  on a current-limited bench supply (checklist gate E).
* Do **not** disable the brownout detector. Fix the power path instead (for
  example with a buck-boost regulator).

## Charging

| Symptom | Likely cause |
|---|---|
| No charging at all with a USB-C-to-C charger | The TP4056 module lacks CC resistors: use a USB-A-to-C cable |
| Charging never finishes | The clip is switched ON while charging (the load keeps the current up): switch OFF |
| Very hot charger or cell | Stop. Check R_PROG and the cell's charge rating ([HARDWARE.md §3](HARDWARE.md#3-charger-programming-resistor)) |
| LED colours unclear | They differ between module makers; measure the current instead |

## Build problems

| Message | What to do |
|---|---|
| `generated headers are out of date` | Run `python3 tools/generate_assets.py` after editing `web/` or the char map |
| `U8g2lib.h: No such file` | Run `scripts/setup-macos.sh` (installs U8g2 2.36.19) |
| `esp32:esp32 … (tested with 3.3.12)` warning | Another core version is installed; the build may work, but 3.3.12 is the tested one |
| `Compiler warnings in project code` | Fix the warning shown; the build is deliberately strict |

## Start over

Erase all saved settings and presets, then restart: type `factory-reset` in the
serial console. To erase the whole flash as well, re-flash with
`EraseFlash=all` in the FQBN in `scripts/common.sh` (and change it back
afterwards).
