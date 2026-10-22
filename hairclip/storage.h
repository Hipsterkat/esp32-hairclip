// Persistent settings in NVS flash (Preferences namespace "hairclip").
//
// SettingsStore holds the load/validate/save logic and talks to a tiny
// KeyValueStore interface, so the same code is unit-tested on the host with a
// fake store that can simulate missing keys, corrupted values and failed
// writes. NvsStore is the Preferences-backed implementation used on the chip.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "settings.h"

class KeyValueStore {
 public:
  virtual ~KeyValueStore() {}
  // Each getter returns false if the key is missing or holds another type.
  virtual bool getU8(const char* key, uint8_t* value) = 0;
  // Copies at most cap-1 bytes plus a terminator into buf; *storedLength
  // receives the full stored length so callers can detect truncation.
  virtual bool getString(const char* key, char* buf, size_t cap, size_t* storedLength) = 0;
  // Setters return true only if the value was committed to flash.
  virtual bool putU8(const char* key, uint8_t value) = 0;
  virtual bool putString(const char* key, const char* value) = 0;
  virtual bool eraseAll() = 0;
};

struct LoadReport {
  bool fresh = false;            // nothing stored yet (first boot or after a reset)
  bool messageDefaulted = false; // no usable message stored; using the default
  bool messageTruncated = false; // stored message exceeded the limits and was cut
  uint8_t fieldsRepaired = 0;    // invalid values replaced by defaults
  uint8_t presetsDropped = 0;    // stored presets that failed validation
};

class SettingsStore {
 public:
  explicit SettingsStore(KeyValueStore& kv) : kv_(kv) {}

  // Never fails: missing or invalid values fall back to defaults, and the
  // report says what happened. message must hold MESSAGE_MAX_BYTES + 1.
  LoadReport load(char* message, DisplaySettings* ds, Presets* presets);

  // Writers return false if flash could not be updated. saveDisplay only
  // writes the keys that differ between `stored` and `wanted`.
  bool saveMessage(const char* message);
  bool saveDisplay(const DisplaySettings& stored, const DisplaySettings& wanted);
  bool savePresets(const Presets& presets);
  bool factoryReset();

  uint8_t brownoutCount();
  bool recordBrownout();

  // Serialization of the preset list: items joined with '\x1E' (record
  // separator), which normalized text can never contain. Exposed for tests.
  static size_t joinPresets(const Presets& p, char* out, size_t cap);
  static uint8_t splitPresets(const char* s, size_t len, Presets* p);

 private:
  bool markVersion();

  KeyValueStore& kv_;
  bool versionWritten_ = false;
};

#ifdef ARDUINO
#include <Preferences.h>

class NvsStore : public KeyValueStore {
 public:
  bool begin();
  bool getU8(const char* key, uint8_t* value) override;
  bool getString(const char* key, char* buf, size_t cap, size_t* storedLength) override;
  bool putU8(const char* key, uint8_t value) override;
  bool putString(const char* key, const char* value) override;
  bool eraseAll() override;

 private:
  Preferences prefs_;
  bool open_ = false;
};
#endif
