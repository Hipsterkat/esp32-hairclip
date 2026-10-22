#include "json_writer.h"

#include <string.h>

void JsonWriter::prefix(const char* key) {
  if (depth_ > 0) {
    if (!first_[depth_ - 1]) out_.append(',');
    first_[depth_ - 1] = false;
  }
  if (key != nullptr) {
    out_.append('"');
    text::jsonEscape(out_, key, strlen(key));
    out_.append("\":");
  }
}

JsonWriter& JsonWriter::beginObject(const char* key) {
  prefix(key);
  out_.append('{');
  if (depth_ >= kMaxDepth) {
    misuse_ = true;
    return *this;
  }
  first_[depth_++] = true;
  return *this;
}

JsonWriter& JsonWriter::endObject() {
  out_.append('}');
  if (depth_ == 0) {
    misuse_ = true;
  } else {
    --depth_;
  }
  return *this;
}

JsonWriter& JsonWriter::beginArray(const char* key) {
  prefix(key);
  out_.append('[');
  if (depth_ >= kMaxDepth) {
    misuse_ = true;
    return *this;
  }
  first_[depth_++] = true;
  return *this;
}

JsonWriter& JsonWriter::endArray() {
  out_.append(']');
  if (depth_ == 0) {
    misuse_ = true;
  } else {
    --depth_;
  }
  return *this;
}

JsonWriter& JsonWriter::str(const char* key, const char* value) {
  return str(key, value, strlen(value));
}

JsonWriter& JsonWriter::str(const char* key, const char* value, size_t len) {
  prefix(key);
  out_.append('"');
  text::jsonEscape(out_, value, len);
  out_.append('"');
  return *this;
}

JsonWriter& JsonWriter::num(const char* key, int32_t value) {
  prefix(key);
  out_.appendInt(value);
  return *this;
}

JsonWriter& JsonWriter::unum(const char* key, uint32_t value) {
  prefix(key);
  out_.appendUInt(value);
  return *this;
}

JsonWriter& JsonWriter::boolean(const char* key, bool value) {
  prefix(key);
  out_.append(value ? "true" : "false");
  return *this;
}
