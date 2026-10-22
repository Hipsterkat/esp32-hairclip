# Hardware and safety review

This review covers the power path, charger, GPIO choices and wearable risks of
the hairclip as specified in the build schematic and the illustrated build
guide. It is a desk review against datasheets. **Nothing here has been measured
on the physical clip: every item is NOT HARDWARE TESTED.** Firmware cannot
protect against an electrical fault; only correct hardware can.

> **Do not wear the clip, and do not leave it switched on unattended, until
> every gate in [docs/HARDWARE_TEST_CHECKLIST.md](docs/HARDWARE_TEST_CHECKLIST.md)
> has passed on your own parts.**

## Summary of findings

| # | Finding | Severity | Action |
|---|---|---|---|
| 1 | A 1-cell LiPo (3.0–4.2 V) feeds the board's **5V** pin. The on-board LDO cannot boost, so below roughly 3.5 V under Wi-Fi load the 3.3 V rail can sag and the chip may reset. | High | Measure (gate E). Consider a buck-boost stage (§2). |
| 2 | TP4056 with R_PROG = 10 kΩ charges at ≈120–130 mA ≈ 0.6–0.65 C for a 200 mAh cell. Many small cells allow only 0.5 C. | High | Read the cell datasheet. Use ≈12 kΩ (≈100 mA) if 0.5 C is the limit. |
| 3 | No power path: with the switch ON while charging, the load shares the charge current, so the charger may never terminate, and the TP4056 has no safety timer. | High | Charge only with the switch OFF. |
| 4 | Computer USB on the SuperMini **and** battery switch ON can back-feed 5 V into the battery path if the board has no blocking diode (unverified). | High | Never combine them: battery disconnected or switch OFF (verified with a meter) before plugging in the board's USB. |
| 5 | Exposed B+/B− pads and wiring sit on a metal barrette; a short dumps the cell's full current. | High | Insulate everything; add a carrier, strain relief and optionally a fuse. |
| 6 | Heat from the LDO, radio and charger next to the scalp. | Medium | Never charge while worn; check temperatures (gates F–H). |
| 7 | GPIO9 (BOOT) is a strapping pin. | Low | Firmware only reads it after boot. Don't add circuitry to it. |
| 8 | There is no battery-voltage sensing. | Info | No battery percentage or low-voltage shutdown is possible without new parts. |

## 1. LiPo into the 5V pin: voltage headroom

Power path from the schematic:
`cell → protection (DW01 + 8205A) → TP4056 OUT+ → SS12D00 switch → SuperMini 5V pad → ME6211 LDO (claimed, unverified) → 3V3 rail → ESP32-C3 + OLED`.

The pad labelled 5V receives the cell voltage, **not 5 V**. The LDO only
regulates down; its output follows the input minus the dropout.

What the parts need (sources at the end):

* **ESP32-C3:** 3.0–3.6 V recommended, and a supply able to deliver at least
  0.5 A. Wi-Fi transmit peaks reach 335 mA at full power (802.11b, 21 dBm) and
  receiving draws about 84 mA.
* **ME6211 LDO:** dropout about 100 mV typical (200 mV max) at 100 mA, and up
  to 400 mV max at 200 mA. No 300 mA figure is published, so expect more.
* **Wiring, switch and protection MOSFETs:** a few tens of mΩ each, so tens of
  millivolts at peak current. Any series diode on the 5V pad would add
  0.3–0.45 V; whether the SuperMini Plus has one is **unknown**.

Illustrative headroom (estimates, not measurements):

| Cell voltage | Wi-Fi transmitting (~200–300 mA) | Wi-Fi off (~20–40 mA) |
|---|---|---|
| 4.2 V (full) | ≈3.3 V regulated | ≈3.3 V regulated |
| 3.7 V (nominal) | ≈3.2–3.3 V, still in range | ≈3.3 V |
| 3.5 V | ≈3.0–3.1 V, marginal: resets possible | ≈3.3 V minus a small dropout |
| 3.3 V and below | below 3.0 V: brownout resets likely | ≈3.2 V or less, close to the limit |

Most of a LiPo's capacity sits between 4.2 V and about 3.6 V, so the setup can
work for much of a charge, especially with the radio off. It cannot be
**guaranteed** across the full discharge range. The firmware reduces the risk
without removing it:

* Wi-Fi transmit power is lowered to 8.5 dBm (`AP_TX_POWER_QDBM`), which lowers
  the peak current. Espressif does not publish the exact peak at 8.5 dBm.
* The radio turns off after 3 minutes without use.
* The brownout detector stays enabled (core default). Never disable it to
  "fix" resets.
* Brownout resets are counted in flash and shown on the web page ("Last
  reset"), which helps diagnose a weak supply.

## 2. Current draw and runtime: estimates only

Chip figures are Espressif datasheet values (3.3 V, 25 °C). Everything else is
an estimate that must be replaced by measurement (gates E and G).

| Part | Wi-Fi off | Wi-Fi access point on |
|---|---|---|
| ESP32-C3 at 80 MHz | 13–22 mA (modem-sleep, CPU idle/running) | about 84 mA receiving, plus transmit bursts |
| OLED (depends on lit pixels and brightness) | ~3–15 mA (estimate) | same |
| Board LEDs, LDO, losses | ~1–5 mA (estimate; a power LED may be always on) | same |
| **Total (estimate)** | **≈17–40 mA** | **≈90–130 mA** |

A 200 mAh cell gives perhaps 120–160 mAh before the voltage headroom runs out
(see §1). That suggests roughly **3–9 hours with Wi-Fi off** and **1–1.7 hours
with Wi-Fi on**. These are planning numbers, not results. Measure the current
in series with the battery (gate G) before you trust any runtime.

Improvements that need **new components**:

* A 3.3 V **buck-boost** regulator (e.g. a TPS63xxx-class module) instead of
  the LDO path, so the rail stays at 3.3 V down to the cell's cut-off.
* **Battery voltage sensing**: a resistor divider (high-value, ideally
  switched) into an ADC pin such as GPIO0, GPIO1, GPIO3 or GPIO4, or an I²C fuel
  gauge on the existing bus. Only with one of these could firmware show a
  battery level or shut down at low voltage.
* A **power-path** charger, or an ideal-diode/P-MOSFET load-sharing circuit, so
  the clip can run while charging without confusing the charger.
* Removing an always-on power LED saves a few mA.

## 3. Charger programming resistor

The TP4056 sets its constant charge current with R_PROG between PROG (pin 2)
and GND: I ≈ 1200 V / R_PROG.

| R_PROG | Charge current | For a 200 mAh cell |
|---|---|---|
| 1.2 kΩ (as shipped) | ≈1000 mA | 5 C: **never use this** |
| 10 kΩ (schematic) | ≈120 mA by formula, 130 mA per the TP4056 table | 0.6–0.65 C |
| 12 kΩ | ≈100 mA | 0.5 C |

Pick R_PROG from the **cell maker's datasheet** (maximum charge current and
temperature range), then measure the real current with an ammeter in series
with the cell (gate F). Allow for resistor tolerance, and remember that a
nearly full cell draws less current. The TP4056 limits its own die
temperature; that does **not** protect the cell from heat.

## 4. No load sharing or power path

The TP4056 module charges whatever sits on B+, and OUT+ is the same node. If
the clip is switched on while charging, the ESP32 draws from the same current.
The charger then sees a current that never falls to its termination level
(≈C/10, about 12–13 mA), so it may keep the cell at 4.2 V indefinitely. The
TP4056 has no charge timer. **Charge only with the slide switch OFF.**

## 5. Simultaneous USB and battery

The SuperMini's USB-C VBUS may be tied directly to the 5V pad, or through a
diode; this is **not verified** for your board. If it is tied directly, plugging
the board into a computer while the battery switch is ON drives about 5 V into
TP4056 OUT+/B+, which is the cell, bypassing the charger's current control.
The DW01 overcharge cut-off (≈4.3 V) is a last-resort protection, not a
charger.

Use one source at a time:

| Mode | Battery / switch | USB in use | Notes |
|---|---|---|---|
| Flash / debug | cell **unplugged**, switch OFF | SuperMini USB only | Charger USB empty |
| Charge | cell connected, switch **OFF** | TP4056 USB only | Off the body, supervised, on a non-flammable surface |
| Wear | switch ON | none | Cover the USB ports |
| Store | switch OFF, unplug for long storage | none | |
| **Never** | switch ON + SuperMini USB, or both USB ports at once | | Possible back-feeding |

A diode test between the SuperMini's USB VBUS and its 5V pad tells you whether
a blocking diode exists. Even if it does, keep the table above.

## 6. Short circuits and protection

* Battery negative goes **only** to B−; the system ground is OUT−. Linking B−
  to OUT− bypasses the DW01/8205A protection.
* The DW01 typically trips on over-current (about 3 A with an 8205A, far more
  than a 200 mAh cell should ever deliver), on over-discharge (≈2.4 V) and on
  over-charge (≈4.3 V). The cell's own protection board, if fitted, adds a
  second layer. Neither prevents thin wires from heating before it trips.
* Consider a small resettable fuse or PTC in series with B+ (sized above the
  peak load of about 0.4 A and below the cell's rating).
* Bare pads, solder joints and wire ends must not be able to touch each other
  or the barrette.

## 7. Metal hairclip: insulation and mechanics

* The barrette is a conductor. Bond a **non-conductive carrier** (printed
  plastic, FR-4 blank) to it; no pad, joint or USB shell may touch metal, even
  when the clip flexes.
* Cover board undersides and joints (Kapton tape, heat-shrink on wires; no heat
  gun near the cell).
* Seat the pouch cell in a smooth cradle **without pressure**. Opening the
  spring must not bend or squeeze it; no screws, sharp edges, solder points or
  hot glue against the pouch.
* Strain-relieve wires; keep them out of the hinge.
* Keep the antenna end of the SuperMini away from the metal barrette and the
  cell where possible, and check Wi-Fi range with the finished enclosure.
* Keep BOOT, RESET and the slide switch reachable; make OFF easy to find.

## 8. Heat

The LDO dissipates (V_cell − 3.3 V) × I, for example about 0.2 W at 4.2 V and
200 mA during Wi-Fi bursts. The radio adds to that, and charging dissipates up
to about (5 V − V_cell) × I in the TP4056. None of this is large, but it sits
against the scalp inside a closed shell. Measure surface temperatures during
Wi-Fi use and during a full charge (gates F and H). Stop using a pack that
swells, smells, leaks or gets hot.

## 9. Charging rules

* Never charge while wearing the clip. Remove it and switch it OFF first.
* Charge only through the TP4056's USB-C port, on a stable, non-flammable
  surface, while someone can watch it.
* Some TP4056 USB-C boards lack the 5.1 kΩ CC resistors, so USB-C-to-C
  chargers give no power. Use a USB-A-to-C cable rather than modifying the
  board.
* Don't charge below 0 °C or above the cell's limit (see its datasheet).

## 10. GPIO review (ESP32-C3)

| GPIO | Use | Review |
|---|---|---|
| 5 | OLED SDA | General-purpose pin (also pad JTAG MTDI, unused). Passed explicitly to `Wire.begin()`. |
| 6 | OLED SCL | General-purpose pin (also pad JTAG MTCK, unused). |
| 9 | BOOT button (input) | **Strapping pin**: LOW at reset selects download mode. The button pulls it LOW only while pressed; the firmware configures INPUT_PULLUP and only reads it after boot. |
| 8 | not used | Strapping pin and on-board LED/RGB on many SuperMini boards; left untouched. |
| 2 | not used | Strapping pin. |
| 12–17 | not used | SPI flash: never use. |
| 18, 19 | not used | Native USB D−/D+: used for flashing and the serial console. |

The "ESP32C3 Dev Module" board profile defaults `Wire` to SDA=8 and SCL=9,
which would put I²C on the BOOT strap. The firmware always passes GPIO5 and
GPIO6 explicitly, to both `Wire.begin()` and the U8g2 constructor.

Pressing BOOT while the board resets (or powers up) enters the ROM download
mode by design; that is the recovery path for flashing. The firmware ignores a
button that is already held when it starts, and acts only on a press that
begins after boot and lasts at least 1.5 s.

**I²C pull-ups and the 3.3 V limit.** SDA and SCL need pull-up resistors.
0.91″ modules normally carry them (4.7–10 kΩ), connected to the module's own
VCC; the ESP32-C3's internal pulls (about 45 kΩ) are too weak on their own.
Two rules follow:

* The module's VCC must be the **3V3** pin. On the 5V/battery pad its
  pull-ups would put up to 4.2 V on GPIO5/6, above the ESP32-C3's 3.6 V
  maximum, which can damage the chip.
* At 400 kHz the pull-ups should be about 4.7 kΩ. With 10 kΩ, long wires or
  flaky text, set `OLED_I2C_CLOCK_HZ` to 100000, or add 4.7 kΩ resistors from
  SDA and SCL to 3V3.

At boot the firmware reads both lines before starting I²C. If they do not idle
HIGH, `status` reports "lines idled LOW (check OLED power and pull-ups)" and
the API reports `busIdleHigh: false`.

**Watchdogs.** The interrupt watchdog (300 ms, core default) catches stuck
interrupts. The task watchdog, set to 30 s, watches both the Arduino loop and
the display task, and resets the chip if either hangs. That limit sits well
above the longest legitimate block (a slow HTTP client). After such a reset,
the page shows *Last reset: task-watchdog*. Watchdogs recover from software
hangs only; they do nothing for power problems.

An ESP32 plugin review checked this firmware against the ESP32-C3 rules
(strapping pins 2/8/9, flash pins 12–17, USB pins 18/19, I²C pull-ups, 3.3 V
GPIO limit, watchdogs, memory handling). Its pin-map validator reported no
errors and one expected warning: GPIO9 must be HIGH at reset for a normal
boot.

## 11. What the firmware does not do

* It does **not** measure the battery: no percentage, no low-voltage cut-off.
* It cannot stop charging, prevent back-feeding or limit current.
* It does not detect over-temperature.

## 12. Verification before wearing

Work through [docs/HARDWARE_TEST_CHECKLIST.md](docs/HARDWARE_TEST_CHECKLIST.md).
It follows the bring-up gates A–H of the illustrated build guide: identify the
parts, verify R_PROG, flash and test on USB with the cell removed, sweep the
battery voltage range on a current-limited bench supply under Wi-Fi load,
measure a full charge, then repeat the checks on battery and in the finished
enclosure.

## Sources

* Espressif, *ESP32-C3 Series Datasheet* v2.4: recommended supply 3.0–3.6 V
  and ≥ 0.5 A (Table 5-2); RF current (Table 5-7); modem-sleep and low-power
  current (Tables 5-8, 5-9).
* Nanjing Micro One, *ME6211 series datasheet*: dropout 100 mV typ @ 100 mA,
  400 mV max @ 200 mA, 500 mA max output.
* NanJing Top Power, *TP4056 datasheet*: R_PROG programming, C/10 termination,
  4.2 V regulation, no charge timer.
* User-supplied build schematic (8 pages) and *ESP32-C3 Hairclip Illustrated
  Build Guide* (22 Oct 2026), including its power gate, USB-mode table and
  bring-up gates.
