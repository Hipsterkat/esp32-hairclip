// Hairclip firmware configuration.
//
// Everything a builder is expected to change lives in this file. Wi-Fi
// credentials can also be overridden without editing tracked files: copy
// secrets.example.h to secrets.h (git-ignored) in this folder.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define HAIRCLIP_FIRMWARE_VERSION "1.0.0"

// ---------------------------------------------------------------------------
// Pins: ESP32-C3 SuperMini Plus wiring from the build schematic.
//
// The "ESP32C3 Dev Module" profile defaults Wire to SDA=8 / SCL=9, and GPIO9
// is the BOOT strapping pin, so the OLED pins are always passed explicitly.
// GPIO9 is only ever *read* (INPUT_PULLUP) after boot; the strap value is
// latched by the ROM at reset, before this firmware runs.
// ---------------------------------------------------------------------------
static constexpr uint8_t PIN_OLED_SDA = 5;
static constexpr uint8_t PIN_OLED_SCL = 6;
static constexpr uint8_t PIN_BOOT_BUTTON = 9;  // onboard BOOT button, active LOW

// ---------------------------------------------------------------------------
// OLED: 0.91" SSD1306, 128x32, I2C.
// ---------------------------------------------------------------------------
static constexpr uint8_t OLED_I2C_ADDRESS = 0x3C;      // 7-bit address from the spec
static constexpr uint8_t OLED_I2C_ADDRESS_ALT = 0x3D;  // some modules ship strapped to 0x3D
static constexpr uint32_t OLED_I2C_CLOCK_HZ = 400000;
static constexpr uint16_t OLED_WIDTH = 128;
static constexpr uint16_t OLED_HEIGHT = 32;
// Upper bound on the frame rate while scrolling (a full 128x32 frame takes
// roughly 15 ms on a 400 kHz bus).
static constexpr uint32_t DISPLAY_MIN_FRAME_MS = 25;
// Text shown when nothing has been saved yet.
#define HAIRCLIP_DEFAULT_MESSAGE "Hello!"

// ---------------------------------------------------------------------------
// Wi-Fi access point.
// ---------------------------------------------------------------------------
#if defined(__has_include)
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#endif

#ifndef HAIRCLIP_AP_SSID
#define HAIRCLIP_AP_SSID "Hairclip"
#endif
#ifndef HAIRCLIP_AP_PASSWORD
// Published default from the project spec. Anyone who has read the README
// knows it: change it (8-63 characters) before wearing the clip in public.
#define HAIRCLIP_AP_PASSWORD "clip12345"
#endif

static_assert(sizeof(HAIRCLIP_AP_SSID) >= 2 && sizeof(HAIRCLIP_AP_SSID) <= 33,
              "HAIRCLIP_AP_SSID must be 1-32 bytes");
static_assert(sizeof(HAIRCLIP_AP_PASSWORD) >= 9 && sizeof(HAIRCLIP_AP_PASSWORD) <= 64,
              "HAIRCLIP_AP_PASSWORD must be 8-63 characters (WPA2)");

static constexpr uint8_t AP_IP[4] = {192, 168, 4, 1};
static constexpr uint8_t AP_NETMASK[4] = {255, 255, 255, 0};
static constexpr uint8_t AP_CHANNEL = 1;
static constexpr uint8_t AP_MAX_CLIENTS = 3;
// Radio power in units of 0.25 dBm (34 = 8.5 dBm). Lower power reduces the
// current spikes the battery path has to deliver and is a common fix for
// ESP32-C3 SuperMini boards with poorly matched antennas. Valid: 8..84.
static constexpr int8_t AP_TX_POWER_QDBM = 34;
// Turn the access point off after this long without user activity.
static constexpr uint32_t WIFI_IDLE_TIMEOUT_MS = 180000;
// When the AP is switched off, keep serving for this long so an open page
// can show "Wi-Fi is turning off" before the connection drops.
static constexpr uint32_t WIFI_STOP_GRACE_MS = 3000;

// ---------------------------------------------------------------------------
// BOOT button.
// ---------------------------------------------------------------------------
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
static constexpr uint32_t BUTTON_LONG_PRESS_MS = 1500;

// ---------------------------------------------------------------------------
// Power. Wi-Fi needs at least 80 MHz on the ESP32-C3.
// ---------------------------------------------------------------------------
static constexpr uint32_t CPU_FREQ_MHZ = 80;

// ---------------------------------------------------------------------------
// Text limits. Characters are Unicode code points after normalization.
// ---------------------------------------------------------------------------
static constexpr size_t MESSAGE_MAX_CHARS = 120;
static constexpr size_t MESSAGE_MAX_BYTES = 256;
static constexpr size_t PRESET_MAX_COUNT = 6;

static constexpr uint32_t SERIAL_BAUD = 115200;
