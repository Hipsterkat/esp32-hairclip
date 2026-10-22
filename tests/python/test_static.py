"""Static checks on the firmware and scripts: blocking calls, pin safety,
platform assumptions and secrets."""

from __future__ import annotations

import re
import unittest

from common import FIRMWARE, ROOT

PURE_MODULES = ["text", "settings", "layout", "button", "wifi_policy", "json_writer"]


def source(name: str) -> str:
    return (FIRMWARE / name).read_text(encoding="utf-8")


def strip_comments(code: str) -> str:
    code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
    return re.sub(r"//[^\n]*", "", code)


def firmware_code() -> dict[str, str]:
    return {p.name: strip_comments(p.read_text(encoding="utf-8"))
            for p in sorted(FIRMWARE.iterdir()) if p.suffix in (".cpp", ".h", ".ino") and not p.name.startswith("generated_")}


class BlockingCalls(unittest.TestCase):
    def test_no_wait_for_serial(self):
        for name, code in firmware_code().items():
            self.assertNotRegex(code, r"while\s*\(\s*!\s*Serial\s*\)", name)

    def test_delays_are_short_yields(self):
        allowed = {"hairclip.ino"}
        for name, code in firmware_code().items():
            for m in re.finditer(r"\bdelay\s*\((.*)\);", code):
                self.assertIn(name, allowed, f"delay() in {name}")
                arg = m.group(1)
                self.assertRegex(arg, r"^wifi::state\(\) == ApState::Off \? \d+ : \d+$", arg)
                for n in map(int, re.findall(r"\d+", arg)):
                    self.assertLessEqual(n, 20)
            self.assertNotIn("delayMicroseconds", code, name)

    def test_render_task_sleeps_on_notifications(self):
        code = strip_comments(source("display.cpp"))
        self.assertIn("ulTaskNotifyTake", code)
        self.assertNotRegex(code, r"\bdelay\s*\(")
        self.assertNotRegex(code, r"while\s*\(\s*(true|1)\s*\)")

    def test_pure_modules_have_no_arduino_dependency(self):
        for mod in PURE_MODULES:
            for ext in (".h", ".cpp"):
                code = source(mod + ext)
                self.assertNotRegex(code, r"#include\s*<(Arduino|WiFi|Wire|WebServer|Preferences|U8g2lib)\.h>", mod + ext)
        storage = source("storage.cpp")
        portable = storage[: storage.index("#ifdef ARDUINO\n\nbool NvsStore")]
        self.assertNotIn("Preferences", strip_comments(portable))


class Reliability(unittest.TestCase):
    def test_task_watchdog_watches_loop_and_display(self):
        ino = firmware_code()["hairclip.ino"]
        self.assertIn("esp_task_wdt_reconfigure(&cfg)", ino)
        self.assertIn("enableLoopWDT()", ino)
        display = firmware_code()["display.cpp"]
        self.assertIn("esp_task_wdt_add(nullptr)", display)
        self.assertIn("esp_task_wdt_reset()", display)
        # The render task must wake up to feed the watchdog: no unbounded wait.
        self.assertNotRegex(display, r"ulTaskNotifyTake\([^;]*portMAX_DELAY")

    def test_memory_is_raii_managed(self):
        for name, code in firmware_code().items():
            code = re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', code)  # ignore text inside string literals
            self.assertNotRegex(code, r"\bdelete\b", f"{name}: use static buffers or std::unique_ptr")
            self.assertNotRegex(code, r"\b(malloc|calloc|realloc|free)\s*\(", name)

    def test_i2c_idle_level_is_checked_before_wire_starts(self):
        display = strip_comments(source("display.cpp"))
        self.assertLess(display.index("g_busIdleHigh = digitalRead(PIN_OLED_SDA)"),
                        display.index("Wire.begin(PIN_OLED_SDA"))


class PinSafety(unittest.TestCase):
    def test_i2c_uses_explicit_oled_pins(self):
        display = strip_comments(source("display.cpp"))
        self.assertIn("Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL", display)
        self.assertIn("U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA", display)

    def test_boot_pin_is_only_read(self):
        for name, code in firmware_code().items():
            self.assertNotRegex(code, r"pinMode\s*\(\s*PIN_BOOT_BUTTON\s*,\s*OUTPUT", name)
            self.assertNotRegex(code, r"digitalWrite\s*\(\s*PIN_BOOT_BUTTON", name)
        self.assertIn("pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP)", firmware_code()["hairclip.ino"])

    def test_no_other_gpio_is_driven(self):
        for name, code in firmware_code().items():
            self.assertNotRegex(code, r"digitalWrite|analogWrite|ledcWrite|rgbLedWrite", name)


class Platform(unittest.TestCase):
    def test_wifi_settings_do_not_write_flash(self):
        self.assertIn("WiFi.persistent(false)", source("wifi_manager.cpp"))

    def test_cpu_clock_and_tx_power(self):
        ino = source("hairclip.ino")
        self.assertIn("setCpuFrequencyMhz(CPU_FREQ_MHZ)", ino)
        self.assertIn("setTxPower", source("wifi_manager.cpp"))

    def test_password_is_never_printed(self):
        for name, code in firmware_code().items():
            if name in ("config.h", "secrets.example.h"):
                continue
            for line in code.splitlines():
                if "HAIRCLIP_AP_PASSWORD" in line:
                    self.assertIn("softAP(", line, f"{name}: {line.strip()}")

    def test_scripts_do_not_hardcode_serial_ports(self):
        for script in (ROOT / "scripts").glob("*.sh"):
            text = script.read_text()
            self.assertNotIn("/dev/ttyACM", text, script.name)
            self.assertNotIn("/dev/ttyUSB", text, script.name)
            for m in re.finditer(r"/dev/cu\.usbmodem[0-9A-Za-z]*", text):
                self.assertTrue(m.group(0).endswith("XXXX") or m.group(0) == "/dev/cu.usbmodem",
                                f"{script.name}: {m.group(0)}")

    def test_flash_script_requires_confirmation(self):
        text = (ROOT / "scripts/flash.sh").read_text()
        self.assertIn("Type 'flash'", text)
        self.assertIn("--yes needs an explicit --port", text)

    def test_secrets_file_is_ignored(self):
        ignore = (ROOT / ".gitignore").read_text()
        self.assertIn("hairclip/secrets.h", ignore)
        self.assertFalse((FIRMWARE / "secrets.h").exists() and "secrets.h" not in ignore)


if __name__ == "__main__":
    unittest.main()
