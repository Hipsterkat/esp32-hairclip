#include <algorithm>
#include <map>

#include "../../hairclip/storage.h"
#include "../../hairclip/text.h"
#include "test.h"

namespace {

// In-memory stand-in for NVS with fault injection.
class FakeStore : public KeyValueStore {
 public:
  std::map<std::string, uint8_t> u8;
  std::map<std::string, std::string> str;
  bool failWrites = false;
  int writes = 0;

  bool getU8(const char* key, uint8_t* value) override {
    auto it = u8.find(key);
    if (it == u8.end()) return false;
    *value = it->second;
    return true;
  }
  bool getString(const char* key, char* buf, size_t cap, size_t* storedLength) override {
    auto it = str.find(key);
    if (it == str.end() || cap == 0) return false;
    *storedLength = it->second.size();
    const size_t n = std::min(it->second.size(), cap - 1);
    memcpy(buf, it->second.data(), n);
    buf[n] = '\0';
    return true;
  }
  bool putU8(const char* key, uint8_t value) override {
    if (failWrites) return false;
    ++writes;
    str.erase(key);
    u8[key] = value;
    return true;
  }
  bool putString(const char* key, const char* value) override {
    if (failWrites) return false;
    ++writes;
    u8.erase(key);
    str[key] = value;
    return true;
  }
  bool eraseAll() override {
    u8.clear();
    str.clear();
    return true;
  }
};

struct Loaded {
  LoadReport report;
  char message[MESSAGE_MAX_BYTES + 1];
  DisplaySettings display;
  Presets presets;
};

Loaded load(FakeStore& kv) {
  Loaded l;
  SettingsStore store(kv);
  l.report = store.load(l.message, &l.display, &l.presets);
  return l;
}

std::string repeat(const std::string& s, size_t n) {
  std::string out;
  for (size_t i = 0; i < n; ++i) out += s;
  return out;
}

}  // namespace

TEST(storage_missing_values_give_defaults) {
  FakeStore kv;
  const Loaded l = load(kv);
  CHECK(l.report.fresh);
  CHECK(l.report.messageDefaulted);
  CHECK_EQ(l.report.fieldsRepaired, 0);
  CHECK_STR(l.message, "Hello!");
  CHECK(l.display == DisplaySettings());
  CHECK_EQ(l.presets.count, 0);
}

TEST(storage_roundtrip) {
  FakeStore kv;
  SettingsStore store(kv);
  DisplaySettings want;
  want.font = FontSize::Small;
  want.align = Align::Right;
  want.brightness = 35;
  want.scroll = false;
  want.speed = 9;
  want.rotate180 = true;
  want.invert = true;
  want.twoLines = true;
  Presets p;
  p.add("Bună dimineața");
  p.add("Love You");
  CHECK(store.saveMessage("Țară dragă"));
  CHECK(store.saveDisplay(DisplaySettings(), want));
  CHECK(store.savePresets(p));

  const Loaded l = load(kv);
  CHECK(!l.report.fresh);
  CHECK(!l.report.messageDefaulted);
  CHECK_STR(l.message, "Țară dragă");
  CHECK(l.display == want);
  CHECK_EQ(l.presets.count, 2);
  CHECK_STR(l.presets.items[0], "Bună dimineața");
  CHECK_STR(l.presets.items[1], "Love You");
}

TEST(storage_empty_message_is_kept) {
  FakeStore kv;
  SettingsStore store(kv);
  CHECK(store.saveMessage(""));
  const Loaded l = load(kv);
  CHECK_STR(l.message, "");
  CHECK(!l.report.messageDefaulted);
}

TEST(storage_writes_only_changed_keys) {
  FakeStore kv;
  SettingsStore store(kv);
  DisplaySettings a;
  CHECK(store.saveDisplay(a, a));
  CHECK_EQ(kv.writes, 0);
  DisplaySettings b = a;
  b.brightness = 10;
  CHECK(store.saveDisplay(a, b));
  CHECK_EQ(kv.writes, 2);  // "bright" + the one-time schema version
  CHECK(store.saveDisplay(b, b));
  CHECK_EQ(kv.writes, 2);
  DisplaySettings c = b;
  c.invert = true;
  CHECK(store.saveDisplay(b, c));
  CHECK_EQ(kv.writes, 3);
}

TEST(storage_repairs_corrupted_fields) {
  FakeStore kv;
  kv.u8["ver"] = 1;
  kv.u8["font"] = 7;
  kv.u8["align"] = 9;
  kv.u8["bright"] = 101;
  kv.u8["speed"] = 0;
  kv.u8["scroll"] = 2;
  kv.u8["inv"] = 1;
  const Loaded l = load(kv);
  CHECK_EQ(l.report.fieldsRepaired, 5);
  DisplaySettings expected;
  expected.invert = true;  // the one valid stored value
  CHECK(l.display == expected);
}

TEST(storage_wrong_type_is_treated_as_missing) {
  FakeStore kv;
  kv.str["font"] = "small";
  kv.u8["msg"] = 3;
  const Loaded l = load(kv);
  CHECK(l.display.font == FontSize::Large);
  CHECK_STR(l.message, "Hello!");
}

TEST(storage_invalid_utf8_message_falls_back_to_default) {
  FakeStore kv;
  kv.str["msg"] = "\xFF\xFE garbage";
  const Loaded l = load(kv);
  CHECK_STR(l.message, "Hello!");
  CHECK(l.report.messageDefaulted);
  CHECK_EQ(l.report.fieldsRepaired, 1);
}

TEST(storage_overlong_message_is_truncated_on_a_character_boundary) {
  FakeStore kv;
  kv.str["msg"] = repeat("a", 300);
  Loaded l = load(kv);
  CHECK(l.report.messageTruncated);
  CHECK_EQ(strlen(l.message), MESSAGE_MAX_CHARS);

  // 401 bytes: the 256-byte read ends inside a two-byte 'ș'.
  kv.str["msg"] = "a" + repeat("ș", 200);
  l = load(kv);
  CHECK(l.report.messageTruncated);
  CHECK(text::utf8Valid(l.message, strlen(l.message)));
  CHECK_EQ(text::utf8Count(l.message, strlen(l.message)), MESSAGE_MAX_CHARS);
  CHECK(strlen(l.message) <= MESSAGE_MAX_BYTES);
}

TEST(storage_drops_invalid_presets) {
  FakeStore kv;
  kv.str["presets"] = std::string("good\x1E\x1E" "bad\xFF\x1E") + repeat("x", 130) + "\x1E" + "also good";
  const Loaded l = load(kv);
  CHECK_EQ(l.presets.count, 2);
  CHECK_STR(l.presets.items[0], "good");
  CHECK_STR(l.presets.items[1], "also good");
  CHECK_EQ(l.report.presetsDropped, 2);
}

TEST(storage_preset_serialization_roundtrip) {
  Presets p;
  p.add("Hello!");
  p.add("Mulțumesc");
  p.add("a b");
  char blob[2048];
  const size_t n = SettingsStore::joinPresets(p, blob, sizeof(blob));
  CHECK_STR(std::string(blob, n), "Hello!\x1EMulțumesc\x1E" "a b");
  Presets q;
  CHECK_EQ(SettingsStore::splitPresets(blob, n, &q), 0);
  CHECK_EQ(q.count, 3);
  CHECK_STR(q.items[1], "Mulțumesc");
  Presets empty;
  CHECK_EQ(SettingsStore::splitPresets("", 0, &empty), 0);
  CHECK_EQ(empty.count, 0);
}

TEST(storage_reports_failed_writes) {
  FakeStore kv;
  kv.failWrites = true;
  SettingsStore store(kv);
  CHECK(!store.saveMessage("x"));
  DisplaySettings changed;
  changed.speed = 1;
  CHECK(!store.saveDisplay(DisplaySettings(), changed));
  Presets p;
  p.add("x");
  CHECK(!store.savePresets(p));
  CHECK(!store.recordBrownout());
}

TEST(storage_factory_reset_and_brownouts) {
  FakeStore kv;
  SettingsStore store(kv);
  CHECK(store.saveMessage("x"));
  CHECK_EQ(store.brownoutCount(), 0);
  CHECK(store.recordBrownout());
  CHECK_EQ(store.brownoutCount(), 1);
  kv.u8["brownouts"] = 255;
  CHECK(store.recordBrownout());
  CHECK_EQ(store.brownoutCount(), 255);  // saturates
  CHECK(store.factoryReset());
  const Loaded l = load(kv);
  CHECK(l.report.fresh);
  CHECK_STR(l.message, "Hello!");
}
