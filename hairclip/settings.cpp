#include "settings.h"

#include <string.h>

bool DisplaySettings::operator==(const DisplaySettings& o) const {
  return font == o.font && align == o.align && brightness == o.brightness && scroll == o.scroll &&
         speed == o.speed && rotate180 == o.rotate180 && invert == o.invert &&
         twoLines == o.twoLines;
}

bool Presets::add(const char* text) {
  if (count >= PRESET_MAX_COUNT) return false;
  const size_t n = strlen(text);
  if (n > MESSAGE_MAX_BYTES) return false;
  memcpy(items[count], text, n + 1);
  ++count;
  return true;
}

bool Presets::remove(uint8_t index) {
  if (index >= count) return false;
  for (uint8_t i = index; i + 1 < count; ++i) memcpy(items[i], items[i + 1], sizeof(items[i]));
  --count;
  items[count][0] = '\0';
  return true;
}

int Presets::find(const char* text) const {
  for (uint8_t i = 0; i < count; ++i) {
    if (strcmp(items[i], text) == 0) return i;
  }
  return -1;
}

namespace settings {

namespace {
const uint16_t kSpeedTable[kSpeedMax] = {8, 12, 16, 20, 25, 30, 40, 50, 60, 75};
const char* const kFontNames[FONT_COUNT] = {"small", "medium", "large"};
const char* const kAlignNames[3] = {"left", "center", "right"};
}  // namespace

uint16_t speedPixelsPerSecond(uint8_t speed) {
  if (speed < kSpeedMin) speed = kSpeedMin;
  if (speed > kSpeedMax) speed = kSpeedMax;
  return kSpeedTable[speed - 1];
}

uint8_t contrastFromBrightness(uint8_t percent) {
  if (percent > kBrightnessMax) percent = kBrightnessMax;
  return static_cast<uint8_t>((percent * 255u + 50u) / 100u);
}

const char* fontName(FontSize f) {
  const uint8_t i = static_cast<uint8_t>(f);
  return i < FONT_COUNT ? kFontNames[i] : "large";
}

const char* alignName(Align a) {
  const uint8_t i = static_cast<uint8_t>(a);
  return i < 3 ? kAlignNames[i] : "center";
}

bool parseFont(const char* s, FontSize* out) {
  for (uint8_t i = 0; i < FONT_COUNT; ++i) {
    if (strcmp(s, kFontNames[i]) == 0) {
      *out = static_cast<FontSize>(i);
      return true;
    }
  }
  return false;
}

bool parseAlign(const char* s, Align* out) {
  for (uint8_t i = 0; i < 3; ++i) {
    if (strcmp(s, kAlignNames[i]) == 0) {
      *out = static_cast<Align>(i);
      return true;
    }
  }
  return false;
}

bool parseUint(const char* s, uint32_t maxValue, uint32_t* out) {
  if (s == nullptr || *s == '\0') return false;
  uint64_t v = 0;
  size_t digits = 0;
  for (; *s != '\0'; ++s) {
    if (*s < '0' || *s > '9' || ++digits > 10) return false;
    v = v * 10 + static_cast<uint32_t>(*s - '0');
  }
  if (v > maxValue) return false;
  *out = static_cast<uint32_t>(v);
  return true;
}

bool parseBool(const char* s, bool* out) {
  if (strcmp(s, "1") == 0 || strcmp(s, "true") == 0) {
    *out = true;
    return true;
  }
  if (strcmp(s, "0") == 0 || strcmp(s, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

bool isKnownField(const char* key) {
  static const char* const kFields[] = {"font",   "align",    "brightness", "scroll",
                                        "speed",  "rotation", "invert",     "lines"};
  for (const char* f : kFields) {
    if (strcmp(key, f) == 0) return true;
  }
  return false;
}

const char* applyField(DisplaySettings* ds, const char* key, const char* value) {
  uint32_t n;
  bool b;
  if (strcmp(key, "font") == 0) {
    FontSize f;
    if (!parseFont(value, &f)) return "font must be small, medium or large";
    ds->font = f;
  } else if (strcmp(key, "align") == 0) {
    Align a;
    if (!parseAlign(value, &a)) return "align must be left, center or right";
    ds->align = a;
  } else if (strcmp(key, "brightness") == 0) {
    if (!parseUint(value, kBrightnessMax, &n)) return "brightness must be a whole number from 0 to 100";
    ds->brightness = static_cast<uint8_t>(n);
  } else if (strcmp(key, "scroll") == 0) {
    if (!parseBool(value, &b)) return "scroll must be 1 or 0";
    ds->scroll = b;
  } else if (strcmp(key, "speed") == 0) {
    if (!parseUint(value, kSpeedMax, &n) || n < kSpeedMin) return "speed must be a whole number from 1 to 10";
    ds->speed = static_cast<uint8_t>(n);
  } else if (strcmp(key, "rotation") == 0) {
    if (strcmp(value, "0") == 0) {
      ds->rotate180 = false;
    } else if (strcmp(value, "180") == 0) {
      ds->rotate180 = true;
    } else {
      return "rotation must be 0 or 180";
    }
  } else if (strcmp(key, "invert") == 0) {
    if (!parseBool(value, &b)) return "invert must be 1 or 0";
    ds->invert = b;
  } else if (strcmp(key, "lines") == 0) {
    if (strcmp(value, "1") == 0) {
      ds->twoLines = false;
    } else if (strcmp(value, "2") == 0) {
      ds->twoLines = true;
    } else {
      return "lines must be 1 or 2";
    }
  } else {
    return "unknown setting";
  }
  return nullptr;
}

uint8_t sanitize(DisplaySettings* ds) {
  const DisplaySettings defaults;
  uint8_t fixed = 0;
  if (static_cast<uint8_t>(ds->font) >= FONT_COUNT) {
    ds->font = defaults.font;
    ++fixed;
  }
  if (static_cast<uint8_t>(ds->align) > static_cast<uint8_t>(Align::Right)) {
    ds->align = defaults.align;
    ++fixed;
  }
  if (ds->brightness > kBrightnessMax) {
    ds->brightness = defaults.brightness;
    ++fixed;
  }
  if (ds->speed < kSpeedMin || ds->speed > kSpeedMax) {
    ds->speed = defaults.speed;
    ++fixed;
  }
  return fixed;
}

}  // namespace settings
