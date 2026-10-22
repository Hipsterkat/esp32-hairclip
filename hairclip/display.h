// SSD1306 128x32 output. A dedicated FreeRTOS task owns U8g2 and the I2C bus
// after begin(), so text keeps animating while the web server is busy. Other
// tasks only post new content through the functions below.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "settings.h"
#include "wifi_policy.h"

namespace display {

struct Info {
  bool detected;       // an SSD1306 acknowledged on the bus at boot
  bool busIdleHigh;    // SDA and SCL read HIGH before I2C started: pull-ups and OLED power present
  uint8_t address;     // 7-bit I2C address in use
  uint8_t busDevices;  // devices that acknowledged during the boot scan
  uint8_t lines;       // lines currently shown (1 or 2)
  bool scrolling;
  uint16_t textWidth;  // pixel width of the (first) line
  uint32_t frames;     // frames sent to the panel since boot
  uint32_t stackFree;  // render-task stack never used, in bytes
};

// Starts I2C on PIN_OLED_SDA/PIN_OLED_SCL, scans the bus, initializes the
// panel and starts the render task. Returns true if an SSD1306 answered.
bool begin(const char* message, const DisplaySettings& ds);

// Thread-safe setters; the render task picks the change up immediately.
void setContent(const char* message, const DisplaySettings& ds);
void setWifiState(ApState state);
// Shows up to two short lines (small font) for durationMs, then the message.
void showToast(const char* line1, const char* line2, uint32_t durationMs);

Info info();
// Addresses found by the boot-time I2C scan; returns how many were copied.
size_t busScan(uint8_t* out, size_t max);

}  // namespace display
