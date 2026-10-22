// User settings: defaults, limits and validation of values from the web API.
// Pure C++: no Arduino dependencies, compiled by the host tests as well.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"

enum class FontSize : uint8_t { Small = 0, Medium = 1, Large = 2 };
enum class Align : uint8_t { Left = 0, Center = 1, Right = 2 };

static constexpr uint8_t FONT_COUNT = 3;

struct DisplaySettings {
  FontSize font = FontSize::Large;
  Align align = Align::Center;
  uint8_t brightness = 60;  // percent, 0-100
  bool scroll = true;
  uint8_t speed = 5;        // 1-10, see settings::speedPixelsPerSecond()
  bool rotate180 = false;
  bool invert = false;
  bool twoLines = false;    // used only when the font fits two lines

  bool operator==(const DisplaySettings& o) const;
  bool operator!=(const DisplaySettings& o) const { return !(*this == o); }
};

// Custom messages saved from the web UI ("Custom saved messages").
struct Presets {
  uint8_t count = 0;
  char items[PRESET_MAX_COUNT][MESSAGE_MAX_BYTES + 1] = {};

  // Returns false if the list is full. Callers pass normalized text.
  bool add(const char* text);
  bool remove(uint8_t index);
  int find(const char* text) const;
};

namespace settings {

static constexpr uint8_t kBrightnessMax = 100;
static constexpr uint8_t kSpeedMin = 1;
static constexpr uint8_t kSpeedMax = 10;

// Scroll speed level (1-10) to pixels per second.
uint16_t speedPixelsPerSecond(uint8_t speed);
// Brightness percent (0-100) to the SSD1306 contrast register (0-255).
uint8_t contrastFromBrightness(uint8_t percent);

const char* fontName(FontSize f);  // "small", "medium", "large"
const char* alignName(Align a);    // "left", "center", "right"
bool parseFont(const char* s, FontSize* out);
bool parseAlign(const char* s, Align* out);
// Strict decimal: digits only, no sign or spaces, value <= maxValue.
bool parseUint(const char* s, uint32_t maxValue, uint32_t* out);
// "1"/"0"/"true"/"false".
bool parseBool(const char* s, bool* out);

// True for the field names accepted by POST /api/settings.
bool isKnownField(const char* key);
// Applies one form field to ds. Returns nullptr on success, otherwise a
// message suitable for the API error response; ds is unchanged on error.
const char* applyField(DisplaySettings* ds, const char* key, const char* value);

// Clamps values that came from storage into range. Returns how many fields
// had to be corrected.
uint8_t sanitize(DisplaySettings* ds);

}  // namespace settings
