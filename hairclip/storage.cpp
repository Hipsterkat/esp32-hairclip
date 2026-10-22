#include "storage.h"

#include <string.h>

#include "text.h"

#ifdef ARDUINO
#include <new>
#endif

namespace {

constexpr uint8_t kSchemaVersion = 1;
constexpr char kPresetSeparator = '\x1E';

// Buffer for the serialized preset list (all items plus separators).
constexpr size_t kPresetBlobCap = PRESET_MAX_COUNT * (MESSAGE_MAX_BYTES + 1);

bool readFlag(KeyValueStore& kv, const char* key, bool* value, uint8_t* repaired) {
  uint8_t v;
  if (!kv.getU8(key, &v)) return false;
  if (v > 1) {
    ++*repaired;
    return false;
  }
  *value = v != 0;
  return true;
}

// Reads a u8 key; values outside [lo, hi] count as repaired and are ignored.
bool readRange(KeyValueStore& kv, const char* key, uint8_t lo, uint8_t hi, uint8_t* value,
               uint8_t* repaired) {
  uint8_t v;
  if (!kv.getU8(key, &v)) return false;
  if (v < lo || v > hi) {
    ++*repaired;
    return false;
  }
  *value = v;
  return true;
}

}  // namespace

LoadReport SettingsStore::load(char* message, DisplaySettings* ds, Presets* presets) {
  LoadReport rep;
  uint8_t version;
  rep.fresh = !kv_.getU8("ver", &version);
  versionWritten_ = !rep.fresh;

  // --- message -------------------------------------------------------------
  size_t stored = 0;
  bool usable = kv_.getString("msg", message, MESSAGE_MAX_BYTES + 1, &stored);
  if (usable) {
    const size_t copied = strlen(message);
    const bool cutByBuffer = stored > copied;
    // Invalid UTF-8 that is not explained by our own cut means corruption.
    if (!cutByBuffer && !text::utf8Valid(message, copied)) {
      usable = false;
      ++rep.fieldsRepaired;
    } else {
      const size_t keep = text::utf8Truncate(message, copied, MESSAGE_MAX_BYTES, MESSAGE_MAX_CHARS);
      if (keep != copied || cutByBuffer) rep.messageTruncated = true;
      message[keep] = '\0';
    }
  }
  if (!usable) {
    strcpy(message, HAIRCLIP_DEFAULT_MESSAGE);
    rep.messageDefaulted = true;
  }

  // --- display settings ----------------------------------------------------
  *ds = DisplaySettings();
  uint8_t v;
  if (readRange(kv_, "font", 0, FONT_COUNT - 1, &v, &rep.fieldsRepaired)) ds->font = static_cast<FontSize>(v);
  if (readRange(kv_, "align", 0, 2, &v, &rep.fieldsRepaired)) ds->align = static_cast<Align>(v);
  if (readRange(kv_, "bright", 0, settings::kBrightnessMax, &v, &rep.fieldsRepaired)) ds->brightness = v;
  if (readRange(kv_, "speed", settings::kSpeedMin, settings::kSpeedMax, &v, &rep.fieldsRepaired)) ds->speed = v;
  readFlag(kv_, "scroll", &ds->scroll, &rep.fieldsRepaired);
  readFlag(kv_, "rot", &ds->rotate180, &rep.fieldsRepaired);
  readFlag(kv_, "inv", &ds->invert, &rep.fieldsRepaired);
  readFlag(kv_, "lines2", &ds->twoLines, &rep.fieldsRepaired);

  // --- presets -------------------------------------------------------------
  *presets = Presets();
  static char blob[kPresetBlobCap];
  size_t blobLen = 0;
  if (kv_.getString("presets", blob, sizeof(blob), &blobLen)) {
    rep.presetsDropped = splitPresets(blob, strlen(blob), presets);
  }
  return rep;
}

bool SettingsStore::markVersion() {
  if (versionWritten_) return true;
  versionWritten_ = kv_.putU8("ver", kSchemaVersion);
  return versionWritten_;
}

bool SettingsStore::saveMessage(const char* message) {
  const bool ok = kv_.putString("msg", message);
  return markVersion() && ok;
}

bool SettingsStore::saveDisplay(const DisplaySettings& stored, const DisplaySettings& wanted) {
  bool ok = true;
  if (stored.font != wanted.font) ok = kv_.putU8("font", static_cast<uint8_t>(wanted.font)) && ok;
  if (stored.align != wanted.align) ok = kv_.putU8("align", static_cast<uint8_t>(wanted.align)) && ok;
  if (stored.brightness != wanted.brightness) ok = kv_.putU8("bright", wanted.brightness) && ok;
  if (stored.speed != wanted.speed) ok = kv_.putU8("speed", wanted.speed) && ok;
  if (stored.scroll != wanted.scroll) ok = kv_.putU8("scroll", wanted.scroll ? 1 : 0) && ok;
  if (stored.rotate180 != wanted.rotate180) ok = kv_.putU8("rot", wanted.rotate180 ? 1 : 0) && ok;
  if (stored.invert != wanted.invert) ok = kv_.putU8("inv", wanted.invert ? 1 : 0) && ok;
  if (stored.twoLines != wanted.twoLines) ok = kv_.putU8("lines2", wanted.twoLines ? 1 : 0) && ok;
  if (stored != wanted) ok = markVersion() && ok;
  return ok;
}

bool SettingsStore::savePresets(const Presets& presets) {
  static char blob[kPresetBlobCap];
  joinPresets(presets, blob, sizeof(blob));
  const bool ok = kv_.putString("presets", blob);
  return markVersion() && ok;
}

bool SettingsStore::factoryReset() {
  versionWritten_ = false;
  return kv_.eraseAll();
}

uint8_t SettingsStore::brownoutCount() {
  uint8_t v = 0;
  return kv_.getU8("brownouts", &v) ? v : 0;
}

bool SettingsStore::recordBrownout() {
  const uint8_t n = brownoutCount();
  return kv_.putU8("brownouts", n == 255 ? n : static_cast<uint8_t>(n + 1));
}

size_t SettingsStore::joinPresets(const Presets& p, char* out, size_t cap) {
  text::StrBuf buf(out, cap);
  for (uint8_t i = 0; i < p.count; ++i) {
    if (i > 0) buf.append(kPresetSeparator);
    buf.append(p.items[i]);
  }
  return buf.length();
}

uint8_t SettingsStore::splitPresets(const char* s, size_t len, Presets* p) {
  uint8_t dropped = 0;
  size_t start = 0;
  while (start <= len) {
    const char* sep = static_cast<const char*>(memchr(s + start, kPresetSeparator, len - start));
    const size_t end = sep ? static_cast<size_t>(sep - s) : len;
    const size_t n = end - start;
    const char* item = s + start;
    const bool valid = n > 0 && n <= MESSAGE_MAX_BYTES && text::utf8Valid(item, n) &&
                       text::utf8Count(item, n) <= MESSAGE_MAX_CHARS &&
                       memchr(item, '\0', n) == nullptr;
    if (valid && p->count < PRESET_MAX_COUNT) {
      memcpy(p->items[p->count], item, n);
      p->items[p->count][n] = '\0';
      ++p->count;
    } else if (n > 0) {
      ++dropped;
    }
    if (!sep) break;
    start = end + 1;
  }
  return dropped;
}

#ifdef ARDUINO

bool NvsStore::begin() {
  open_ = prefs_.begin("hairclip", false);
  return open_;
}

bool NvsStore::getU8(const char* key, uint8_t* value) {
  if (!open_ || prefs_.getType(key) != PT_U8) return false;
  *value = prefs_.getUChar(key);
  return true;
}

bool NvsStore::getString(const char* key, char* buf, size_t cap, size_t* storedLength) {
  if (!open_ || cap == 0 || prefs_.getType(key) != PT_STR) return false;
  const size_t need = prefs_.getStringLength(key);  // includes the terminator
  if (need == 0) return false;
  *storedLength = need - 1;
  if (need <= cap) return prefs_.getString(key, buf, cap) > 0;
  // Longer than this firmware allows (corruption or a newer firmware):
  // read it whole, then hand back the first cap-1 bytes for validation.
  char* tmp = new (std::nothrow) char[need];
  if (tmp == nullptr) return false;
  const bool ok = prefs_.getString(key, tmp, need) > 0;
  if (ok) {
    memcpy(buf, tmp, cap - 1);
    buf[cap - 1] = '\0';
  }
  delete[] tmp;
  return ok;
}

bool NvsStore::putU8(const char* key, uint8_t value) {
  return open_ && prefs_.putUChar(key, value) == 1;
}

bool NvsStore::putString(const char* key, const char* value) {
  if (!open_) return false;
  const size_t n = strlen(value);
  if (n > 0) return prefs_.putString(key, value) == n;
  // Preferences reports 0 for an empty string even on success: read it back.
  prefs_.putString(key, value);
  return prefs_.getType(key) == PT_STR && prefs_.getStringLength(key) == 1;
}

bool NvsStore::eraseAll() { return open_ && prefs_.clear(); }

#endif  // ARDUINO
