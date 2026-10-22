// Minimal streaming JSON writer into a fixed buffer (no heap).
// Pure C++: no Arduino dependencies, compiled by the host tests as well.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "text.h"

class JsonWriter {
 public:
  explicit JsonWriter(text::StrBuf& out) : out_(out) {}

  // `key` names the member inside an object; pass nullptr inside arrays.
  JsonWriter& beginObject(const char* key = nullptr);
  JsonWriter& endObject();
  JsonWriter& beginArray(const char* key = nullptr);
  JsonWriter& endArray();
  JsonWriter& str(const char* key, const char* value);
  JsonWriter& str(const char* key, const char* value, size_t len);
  JsonWriter& num(const char* key, int32_t value);
  JsonWriter& unum(const char* key, uint32_t value);
  JsonWriter& boolean(const char* key, bool value);

  // True if everything fit and every object/array was closed.
  bool ok() const { return !out_.overflow() && !misuse_ && depth_ == 0; }

 private:
  static constexpr uint8_t kMaxDepth = 8;
  void prefix(const char* key);

  text::StrBuf& out_;
  uint8_t depth_ = 0;
  bool first_[kMaxDepth] = {};
  bool misuse_ = false;
};
