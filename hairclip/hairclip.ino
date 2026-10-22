// ESP32-C3 OLED hairclip firmware.
//
// Boot: show the saved message on the 128x32 OLED, start the "Hairclip"
// access point and serve the control page at http://192.168.4.1. The radio
// turns itself off after 3 minutes without use; hold BOOT for 1.5 s to toggle
// it. See README.md (build/flash) and HARDWARE.md (power and safety).

#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>

#include <atomic>

#include "button.h"
#include "config.h"
#include "display.h"
#include "settings.h"
#include "storage.h"
#include "text.h"
#include "web_ui.h"
#include "wifi_manager.h"

static NvsStore g_nvs;
static SettingsStore g_store(g_nvs);
static AppState g_state;
static LoadReport g_loadReport;

// The BOOT button is sampled every 10 ms from an esp_timer callback, so a
// press is never missed while loop() is busy with an HTTP request. The
// callback only records the event; loop() acts on it.
static LongPressButton g_button(BUTTON_DEBOUNCE_MS, BUTTON_LONG_PRESS_MS);
static std::atomic<bool> g_longPress(false);
static esp_timer_handle_t g_buttonTimer = nullptr;

static void sampleButton(void*) {
  if (g_button.update(digitalRead(PIN_BOOT_BUTTON) == LOW, millis())) g_longPress = true;
}

// The core starts the task watchdog with a 5 s timeout and nothing
// subscribed. Lengthen it, then watch the Arduino loop task (fed by the core
// after every loop()); the display task subscribes itself.
static void startWatchdog() {
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = WATCHDOG_TIMEOUT_MS;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&cfg) == ESP_OK) enableLoopWDT();
}

static const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt-watchdog";
    case ESP_RST_TASK_WDT: return "task-watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    default: return "unknown";
  }
}

static const char* apStateLabel(ApState s) {
  switch (s) {
    case ApState::On: return "on";
    case ApState::Stopping: return "stopping";
    default: return "off";
  }
}

// ---------------------------------------------------------------------------
// Serial console (115200 baud, newline-terminated commands). Never prints the
// Wi-Fi password.
// ---------------------------------------------------------------------------

static void printDisplaySettings() {
  const DisplaySettings& ds = g_state.display;
  Serial.printf("display    font=%s align=%s brightness=%u%% scroll=%u speed=%u rotation=%u invert=%u lines=%u\n",
                settings::fontName(ds.font), settings::alignName(ds.align), ds.brightness, ds.scroll, ds.speed,
                ds.rotate180 ? 180 : 0, ds.invert, ds.twoLines ? 2 : 1);
}

static void printStatus() {
  const uint32_t now = millis();
  const display::Info d = display::info();
  uint8_t found[16];
  const size_t n = display::busScan(found, sizeof(found));
  Serial.printf("firmware   %s (build %s)\n", HAIRCLIP_FIRMWARE_VERSION, HAIRCLIP_BUILD);
  Serial.printf("chip       %s rev %u, %lu MHz, reset: %s, brownouts: %u\n", ESP.getChipModel(),
                ESP.getChipRevision(), static_cast<unsigned long>(getCpuFrequencyMhz()),
                g_state.resetReason, g_state.brownouts);
  Serial.printf("uptime     %lu s, heap %lu free (min %lu)\n",
                static_cast<unsigned long>(esp_timer_get_time() / 1000000),
                static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()));
  Serial.printf("i2c        SDA=GPIO%u SCL=GPIO%u, lines idled %s at boot, devices:", PIN_OLED_SDA, PIN_OLED_SCL,
                d.busIdleHigh ? "HIGH (pull-ups ok)" : "LOW (check OLED power and pull-ups)");
  for (size_t i = 0; i < n; ++i) Serial.printf(" 0x%02X", found[i]);
  Serial.printf("%s\n", n == 0 ? " none" : "");
  Serial.printf("oled       %s at 0x%02X, %u line(s), %s, %lu frames\n", d.detected ? "detected" : "NOT DETECTED",
                d.address, d.lines, d.scrolling ? "scrolling" : "static", static_cast<unsigned long>(d.frames));
  Serial.printf("wifi       %s, SSID \"%s\", http://%u.%u.%u.%u, %u client(s), auto-off in %lu s\n",
                apStateLabel(wifi::state()), HAIRCLIP_AP_SSID, AP_IP[0], AP_IP[1], AP_IP[2], AP_IP[3],
                wifi::clientCount(), static_cast<unsigned long>(wifi::autoOffInMs(now) / 1000));
  Serial.printf("boot btn   GPIO%u %s\n", PIN_BOOT_BUTTON, digitalRead(PIN_BOOT_BUTTON) == LOW ? "pressed" : "released");
  Serial.printf("message    \"%s\"\n", g_state.message);
  printDisplaySettings();
}

static void printHelp() {
  Serial.println("commands: status | msg <text> | set <field> <value> | wifi on | wifi off | restart |");
  Serial.println("          factory-reset | help    (set fields: font align brightness scroll speed rotation invert lines)");
}

// msg <text>: show and save a message, with the same rules as the web page.
static void consoleMessage(const char* input) {
  static char norm[MESSAGE_MAX_BYTES + 1];
  const text::NormalizeResult r =
      text::normalizeMessage(input, strlen(input), norm, sizeof(norm), MESSAGE_MAX_CHARS, MESSAGE_MAX_BYTES);
  if (r.status != text::NormalizeStatus::Ok) {
    Serial.println(r.status == text::NormalizeStatus::TooLong
                       ? "error: message too long (max 120 characters, 256 bytes)"
                       : "error: message is not valid UTF-8");
    return;
  }
  bool saved = true;
  if (strcmp(norm, g_state.message) != 0) {
    saved = g_store.saveMessage(norm);
    memcpy(g_state.message, norm, r.bytes + 1);
    display::setContent(g_state.message, g_state.display);
  }
  Serial.printf("message    \"%s\"%s\n", g_state.message, saved ? "" : "  (NOT saved to flash)");
}

// set <field> <value>: change and save one display setting (web API names).
static void consoleSet(const char* args) {
  const char* space = strchr(args, ' ');
  char field[16];
  if (space == nullptr || static_cast<size_t>(space - args) >= sizeof(field)) {
    Serial.println("usage: set <font|align|brightness|scroll|speed|rotation|invert|lines> <value>");
    return;
  }
  memcpy(field, args, static_cast<size_t>(space - args));
  field[space - args] = '\0';
  if (!settings::isKnownField(field)) {
    Serial.printf("error: unknown setting '%s'\n", field);
    return;
  }
  DisplaySettings next = g_state.display;
  if (const char* err = settings::applyField(&next, field, space + 1)) {
    Serial.printf("error: %s\n", err);
    return;
  }
  g_state.display = next;
  display::setContent(g_state.message, g_state.display);
  bool saved = true;
  if (g_state.storedDisplay != g_state.display) {
    saved = g_store.saveDisplay(g_state.storedDisplay, g_state.display);
    if (saved) g_state.storedDisplay = g_state.display;
  }
  printDisplaySettings();
  if (!saved) Serial.println("(NOT saved to flash)");
}

static void runCommand(const char* cmd) {
  const uint32_t now = millis();
  if (strcmp(cmd, "status") == 0) {
    printStatus();
  } else if (strncmp(cmd, "msg ", 4) == 0) {
    consoleMessage(cmd + 4);
  } else if (strcmp(cmd, "msg") == 0) {
    consoleMessage("");
  } else if (strncmp(cmd, "set ", 4) == 0) {
    consoleSet(cmd + 4);
  } else if (strcmp(cmd, "wifi on") == 0) {
    wifi::turnOn(now);
  } else if (strcmp(cmd, "wifi off") == 0) {
    wifi::turnOff(now, StopReason::Console);
  } else if (strcmp(cmd, "restart") == 0) {
    Serial.println("restarting");
    Serial.flush();
    ESP.restart();
  } else if (strcmp(cmd, "factory-reset") == 0) {
    const bool ok = g_store.factoryReset();
    Serial.printf("settings %s; restarting\n", ok ? "erased" : "could NOT be erased");
    Serial.flush();
    ESP.restart();
  } else {
    printHelp();
  }
}

static void pollConsole() {
  static char line[MESSAGE_MAX_BYTES + 16];  // room for "msg " plus a full message
  static size_t len = 0;
  static bool overflow = false;
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (overflow) {
        Serial.println("error: line too long");
      } else if (len > 0) {
        line[len] = '\0';
        runCommand(line);
      }
      len = 0;
      overflow = false;
    } else if (len < sizeof(line) - 1) {
      line[len++] = static_cast<char>(c);
    } else {
      overflow = true;  // never cut a line (it could split a UTF-8 character)
    }
  }
}

// ---------------------------------------------------------------------------

void setup() {
  setCpuFrequencyMhz(CPU_FREQ_MHZ);
  Serial.begin(SERIAL_BAUD);
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);  // never stall when no computer is listening
#endif
  startWatchdog();
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

  const esp_reset_reason_t reason = esp_reset_reason();
  g_state.resetReason = resetReasonName(reason);
  const bool nvsOk = g_nvs.begin();
  g_loadReport = g_store.load(g_state.message, &g_state.display, &g_state.presets);
  g_state.storedDisplay = g_state.display;
  if (reason == ESP_RST_BROWNOUT) g_store.recordBrownout();
  g_state.brownouts = g_store.brownoutCount();

  display::begin(g_state.message, g_state.display);

  esp_timer_create_args_t timerArgs = {};
  timerArgs.callback = sampleButton;
  timerArgs.name = "boot-button";
  esp_timer_create(&timerArgs, &g_buttonTimer);
  esp_timer_start_periodic(g_buttonTimer, 10 * 1000);

  web::begin(g_state, g_store);
  wifi::begin(millis());

  Serial.println();
  Serial.println("=== ESP32-C3 OLED hairclip ===");
  Serial.printf("settings   nvs %s, %s%s, %u repaired, %u preset(s) dropped\n", nvsOk ? "ok" : "UNAVAILABLE",
                g_loadReport.fresh ? "first boot" : "loaded", g_loadReport.messageTruncated ? ", message truncated" : "",
                g_loadReport.fieldsRepaired, g_loadReport.presetsDropped);
  printStatus();
  printHelp();
}

void loop() {
  const uint32_t now = millis();
  if (g_longPress.exchange(false)) wifi::toggle(now);
  wifi::loop(now);
  pollConsole();
  // Yield to FreeRTOS (the CPU idles); poll the web server more often while
  // the access point is up.
  delay(wifi::state() == ApState::Off ? 20 : 2);
}
