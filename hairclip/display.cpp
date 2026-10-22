#include "display.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include <string.h>

#include "config.h"
#include "generated_fonts.h"
#include "layout.h"
#include "text.h"

namespace display {

namespace {

// U8g2's SSD1306 128x32 "univision" driver sends the same essential setup as
// the reference 0.91" module code (multiplex 0x1F, COM pins 0x02, segment
// remap 0xA1, COM scan 0xC8, charge pump on). Pins are passed explicitly so
// Wire never falls back to the board default SCL=GPIO9 (the BOOT strap).
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA);

constexpr size_t kToastLen = 40;
constexpr uint32_t kBlinkMs = 250;
constexpr uint32_t kStackBytes = 6144;

// Small "radio on" icon for the top-right corner, XBM bit order.
constexpr uint8_t kIconW = 7;
constexpr uint8_t kIconH = 5;
const uint8_t kWifiIcon[kIconH] = {0x40, 0x50, 0x54, 0x55, 0x55};

struct Model {
  char message[MESSAGE_MAX_BYTES + 1];
  DisplaySettings settings;
  ApState wifi;
  char toast[2][kToastLen];
  uint32_t toastStartMs;
  uint32_t toastMs;  // 0 = no toast
  uint32_t version;
};

// Shared with other tasks, guarded by g_lock.
Model g_model;
Info g_info;
SemaphoreHandle_t g_lock = nullptr;
TaskHandle_t g_task = nullptr;

// Written once in begin(), read-only afterwards.
bool g_detected = false;
bool g_busIdleHigh = false;
uint8_t g_address = OLED_I2C_ADDRESS;
uint8_t g_found[16];
size_t g_foundCount = 0;

// Render-task state.
struct Scene {
  Model model;
  char text[MESSAGE_MAX_BYTES + 1];
  char lanes[2][MESSAGE_MAX_BYTES + 1];
  char toast[2][kToastLen];
  layout::Layout layout;
  uint32_t animStartMs;
};
Scene g_scene;
char g_measureBuf[MESSAGE_MAX_BYTES + 1];

const uint8_t* fontData(FontSize f) {
  switch (f) {
    case FontSize::Small: return HAIRCLIP_FONT_SMALL;
    case FontSize::Medium: return HAIRCLIP_FONT_MEDIUM;
    default: return HAIRCLIP_FONT_LARGE;
  }
}

bool hasGlyph(uint32_t cp, void*) {
  return cp <= 0xFFFF && u8g2_IsGlyph(u8g2.getU8g2(), static_cast<uint16_t>(cp)) != 0;
}

uint16_t measure(const char* s, size_t len, void*) {
  if (len > MESSAGE_MAX_BYTES) len = MESSAGE_MAX_BYTES;
  memcpy(g_measureBuf, s, len);
  g_measureBuf[len] = '\0';
  return u8g2.getUTF8Width(g_measureBuf);
}

void lock() { xSemaphoreTake(g_lock, portMAX_DELAY); }
void unlock() { xSemaphoreGive(g_lock); }

void notify() {
  if (g_task != nullptr) xTaskNotifyGive(g_task);
}

void copyText(char* dst, size_t cap, const char* src) {
  const size_t n = src ? text::utf8Truncate(src, strlen(src), cap - 1, cap - 1) : 0;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

void applyHardware(const DisplaySettings& ds, const DisplaySettings* before) {
  if (before == nullptr || before->brightness != ds.brightness) {
    u8g2.setContrast(settings::contrastFromBrightness(ds.brightness));
  }
  if (before == nullptr || before->invert != ds.invert) {
    u8g2.sendF("c", ds.invert ? 0xA7 : 0xA6);  // SSD1306 inverse / normal display
  }
  if (before == nullptr || before->rotate180 != ds.rotate180) {
    u8g2.setDisplayRotation(ds.rotate180 ? U8G2_R2 : U8G2_R0);
  }
}

void rebuild(Scene& sc) {
  const DisplaySettings& ds = sc.model.settings;
  u8g2.setFont(fontData(ds.font));
  int32_t firstBreak;
  const size_t len = text::buildDisplayText(sc.model.message, strlen(sc.model.message), hasGlyph,
                                            nullptr, sc.text, sizeof(sc.text), &firstBreak);
  layout::Options opt;
  opt.align = ds.align;
  opt.scroll = ds.scroll;
  opt.twoLines = ds.twoLines;
  opt.speedPxPerSec = settings::speedPixelsPerSecond(ds.speed);
  sc.layout = layout::compute(sc.text, len, firstBreak, kFontMetrics[static_cast<uint8_t>(ds.font)],
                              opt, measure, nullptr);
  for (uint8_t i = 0; i < sc.layout.laneCount; ++i) {
    const layout::Lane& lane = sc.layout.lanes[i];
    memcpy(sc.lanes[i], sc.text + lane.start, lane.length);
    sc.lanes[i][lane.length] = '\0';
  }
  // Toast lines are drawn in the small font; make them safe the same way.
  u8g2.setFont(HAIRCLIP_FONT_SMALL);
  for (uint8_t i = 0; i < 2; ++i) {
    int32_t ignored;
    text::buildDisplayText(sc.model.toast[i], strlen(sc.model.toast[i]), hasGlyph, nullptr,
                           sc.toast[i], sizeof(sc.toast[i]), &ignored);
  }
  u8g2.setFont(fontData(ds.font));
  sc.animStartMs = millis();

  lock();
  g_info.lines = sc.layout.laneCount;
  g_info.scrolling = sc.layout.lanes[0].scrolls;
  g_info.textWidth = sc.layout.lanes[0].width;
  unlock();
}

void drawWifiIcon() {
  u8g2.setDrawColor(0);
  u8g2.drawBox(OLED_WIDTH - kIconW - 1, 0, kIconW + 1, kIconH + 1);
  u8g2.setDrawColor(1);
  u8g2.drawXBM(OLED_WIDTH - kIconW, 0, kIconW, kIconH, kWifiIcon);
}

void drawFrame(Scene& sc, bool toastOn, bool iconOn, uint32_t offset) {
  u8g2.clearBuffer();
  if (toastOn) {
    u8g2.setFont(HAIRCLIP_FONT_SMALL);
    int16_t b1, b2;
    layout::twoLineBaselines(kFontMetrics[0], OLED_HEIGHT, &b1, &b2);
    const bool single = sc.toast[1][0] == '\0';
    for (uint8_t i = 0; i < (single ? 1 : 2); ++i) {
      const int16_t w = static_cast<int16_t>(u8g2.getUTF8Width(sc.toast[i]));
      const int16_t baseline = single ? layout::singleLineBaseline(kFontMetrics[0], OLED_HEIGHT) : (i == 0 ? b1 : b2);
      u8g2.drawUTF8((OLED_WIDTH - w) / 2, baseline, sc.toast[i]);
    }
    u8g2.setFont(fontData(sc.model.settings.font));
  } else {
    for (uint8_t i = 0; i < sc.layout.laneCount; ++i) {
      const layout::Lane& lane = sc.layout.lanes[i];
      if (!lane.scrolls) {
        u8g2.drawUTF8(lane.x, lane.baseline, sc.lanes[i]);
        continue;
      }
      // Ticker: draw the text and, behind a gap, its next copy. Negative x
      // wraps in u8g2_uint_t; U8g2 clips such glyphs correctly.
      const int32_t x = -static_cast<int32_t>(offset);
      u8g2.drawUTF8(static_cast<u8g2_uint_t>(x), lane.baseline, sc.lanes[i]);
      const int32_t x2 = x + lane.width + sc.layout.gap;
      if (x2 < OLED_WIDTH) u8g2.drawUTF8(static_cast<u8g2_uint_t>(x2), lane.baseline, sc.lanes[i]);
    }
  }
  if (iconOn) drawWifiIcon();
}

// Sends only the 8-pixel tile rows a scrolling lane covers.
void sendLaneRows(const Scene& sc, const layout::Lane& lane) {
  const FontMetrics& m = kFontMetrics[static_cast<uint8_t>(sc.model.settings.font)];
  int y0 = lane.baseline - m.maxAscent;
  int y1 = lane.baseline + m.maxDescent;
  if (y0 < 0) y0 = 0;
  if (y1 > OLED_HEIGHT) y1 = OLED_HEIGHT;
  if (y1 <= y0) return;
  int t0 = y0 / 8;
  int t1 = (y1 - 1) / 8;
  if (sc.model.settings.rotate180) {  // the buffer keeps the panel's own row order
    const int last = OLED_HEIGHT / 8 - 1;
    const int a = last - t1;
    t1 = last - t0;
    t0 = a;
  }
  u8g2.updateDisplayArea(0, static_cast<uint8_t>(t0), OLED_WIDTH / 8, static_cast<uint8_t>(t1 - t0 + 1));
}

void renderTask(void*) {
  Scene& sc = g_scene;
  uint32_t seen = 0;
  bool first = true;
  DisplaySettings applied;
  bool lastToast = false, lastIcon = false, sleeping = false;
  uint32_t lastOffset = UINT32_MAX;
  uint32_t frames = 0;
  esp_task_wdt_add(nullptr);  // the task watchdog resets the chip if this loop hangs

  for (;;) {
    esp_task_wdt_reset();
    bool full = false;
    lock();
    const bool changed = g_model.version != seen;
    if (changed) {
      sc.model = g_model;
      seen = g_model.version;
    }
    unlock();
    if (changed) {
      applyHardware(sc.model.settings, first ? nullptr : &applied);
      applied = sc.model.settings;
      first = false;
      rebuild(sc);
      full = true;
    }

    const uint32_t now = millis();
    const bool toastOn = sc.model.toastMs > 0 && now - sc.model.toastStartMs < sc.model.toastMs;
    const bool blinkOn = (now / kBlinkMs) % 2 == 0;
    const bool iconOn = sc.model.wifi == ApState::On || (sc.model.wifi == ApState::Stopping && blinkOn);

    const layout::Lane* scrolling = nullptr;
    uint32_t offset = 0;
    uint32_t elapsed = 0;
    if (!toastOn && sc.layout.laneCount == 1 && sc.layout.lanes[0].scrolls) {
      scrolling = &sc.layout.lanes[0];
      const uint32_t cycle = layout::cycleMs(*scrolling, sc.layout);
      elapsed = now - sc.animStartMs;
      if (elapsed >= cycle) {  // keep the reference recent: no millis() wrap issues
        sc.animStartMs += (elapsed / cycle) * cycle;
        elapsed %= cycle;
      }
      offset = layout::scrollOffset(*scrolling, sc.layout, elapsed);
    }

    // Nothing to show (empty message, radio off): put the panel to sleep.
    const bool blank = !toastOn && sc.model.wifi == ApState::Off && sc.text[0] == '\0';
    if (blank != sleeping) {
      u8g2.setPowerSave(blank ? 1 : 0);
      sleeping = blank;
      full = true;
    }

    if (!blank) {
      if (full || toastOn != lastToast || iconOn != lastIcon) {
        drawFrame(sc, toastOn, iconOn, offset);
        u8g2.sendBuffer();
        ++frames;
      } else if (scrolling != nullptr && offset != lastOffset) {
        drawFrame(sc, toastOn, iconOn, offset);
        sendLaneRows(sc, *scrolling);
        ++frames;
      }
    }
    lastToast = toastOn;
    lastIcon = iconOn;
    lastOffset = offset;

    lock();
    g_info.frames = frames;
    unlock();

    // Sleep until the picture would change, new content arrives, or it is
    // time to feed the watchdog.
    uint32_t wait = DISPLAY_MAX_SLEEP_MS;
    if (scrolling != nullptr) {
      const uint32_t step = layout::msUntilNextStep(*scrolling, sc.layout, elapsed);
      if (step < wait) wait = step < DISPLAY_MIN_FRAME_MS ? DISPLAY_MIN_FRAME_MS : step;
    }
    if (toastOn) {
      const uint32_t left = sc.model.toastMs - (now - sc.model.toastStartMs);
      if (left < wait) wait = left;
    }
    if (sc.model.wifi == ApState::Stopping) {
      const uint32_t left = kBlinkMs - now % kBlinkMs;
      if (left < wait) wait = left;
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));
  }
}

bool ack(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

}  // namespace

bool begin(const char* message, const DisplaySettings& ds) {
  g_lock = xSemaphoreCreateMutex();

  // An idle I2C bus is HIGH through the module's pull-up resistors (internal
  // pulls are off in INPUT mode). LOW means no pull-ups or no OLED power.
  pinMode(PIN_OLED_SDA, INPUT);
  pinMode(PIN_OLED_SCL, INPUT);
  g_busIdleHigh = digitalRead(PIN_OLED_SDA) == HIGH && digitalRead(PIN_OLED_SCL) == HIGH;

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL, OLED_I2C_CLOCK_HZ);
  for (uint8_t a = 0x08; a < 0x78; ++a) {
    if (ack(a) && g_foundCount < sizeof(g_found)) g_found[g_foundCount++] = a;
  }
  if (ack(OLED_I2C_ADDRESS)) {
    g_detected = true;
    g_address = OLED_I2C_ADDRESS;
  } else if (ack(OLED_I2C_ADDRESS_ALT)) {
    g_detected = true;
    g_address = OLED_I2C_ADDRESS_ALT;
  }

  u8g2.setI2CAddress(g_address * 2);
  u8g2.setBusClock(OLED_I2C_CLOCK_HZ);
  u8g2.begin();  // init sequence, clear, display on
  u8g2.setFontMode(1);
  u8g2.setFontPosBaseline();

  copyText(g_model.message, sizeof(g_model.message), message);
  g_model.settings = ds;
  g_model.wifi = ApState::Off;
  g_model.version = 1;
  g_info.detected = g_detected;
  g_info.busIdleHigh = g_busIdleHigh;
  g_info.address = g_address;
  g_info.busDevices = static_cast<uint8_t>(g_foundCount);

  xTaskCreate(renderTask, "display", kStackBytes, nullptr, 2, &g_task);
  return g_detected;
}

void setContent(const char* message, const DisplaySettings& ds) {
  lock();
  copyText(g_model.message, sizeof(g_model.message), message);
  g_model.settings = ds;
  ++g_model.version;
  unlock();
  notify();
}

void setWifiState(ApState state) {
  lock();
  if (g_model.wifi != state) {
    g_model.wifi = state;
    ++g_model.version;
  }
  unlock();
  notify();
}

void showToast(const char* line1, const char* line2, uint32_t durationMs) {
  lock();
  copyText(g_model.toast[0], kToastLen, line1);
  copyText(g_model.toast[1], kToastLen, line2);
  g_model.toastStartMs = millis();
  g_model.toastMs = durationMs;
  ++g_model.version;
  unlock();
  notify();
}

Info info() {
  lock();
  Info i = g_info;
  unlock();
  i.stackFree = g_task != nullptr ? uxTaskGetStackHighWaterMark(g_task) : 0;
  return i;
}

size_t busScan(uint8_t* out, size_t max) {
  const size_t n = g_foundCount < max ? g_foundCount : max;
  memcpy(out, g_found, n);
  return n;
}

}  // namespace display
