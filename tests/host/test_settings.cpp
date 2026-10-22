#include "../../hairclip/settings.h"
#include "test.h"

TEST(settings_defaults) {
  const DisplaySettings d;
  CHECK(d.font == FontSize::Large);
  CHECK(d.align == Align::Center);
  CHECK_EQ(d.brightness, 60);
  CHECK(d.scroll);
  CHECK_EQ(d.speed, 5);
  CHECK(!d.rotate180);
  CHECK(!d.invert);
  CHECK(!d.twoLines);
  CHECK_STR(HAIRCLIP_DEFAULT_MESSAGE, "Hello!");
}

TEST(settings_spec_values) {
  CHECK_STR(HAIRCLIP_AP_SSID, "Hairclip");
  CHECK_STR(HAIRCLIP_AP_PASSWORD, "clip12345");
  CHECK_EQ(AP_IP[0], 192);
  CHECK_EQ(AP_IP[1], 168);
  CHECK_EQ(AP_IP[2], 4);
  CHECK_EQ(AP_IP[3], 1);
  CHECK_EQ(WIFI_IDLE_TIMEOUT_MS, 180000u);
  CHECK_EQ(BUTTON_LONG_PRESS_MS, 1500u);
  CHECK_EQ(PIN_OLED_SDA, 5);
  CHECK_EQ(PIN_OLED_SCL, 6);
  CHECK_EQ(PIN_BOOT_BUTTON, 9);
  CHECK_EQ(OLED_I2C_ADDRESS, 0x3C);
  CHECK_EQ(CPU_FREQ_MHZ, 80u);
}

TEST(settings_speed_and_contrast_mapping) {
  CHECK_EQ(settings::speedPixelsPerSecond(1), 8);
  CHECK_EQ(settings::speedPixelsPerSecond(5), 25);
  CHECK_EQ(settings::speedPixelsPerSecond(10), 75);
  CHECK_EQ(settings::speedPixelsPerSecond(0), 8);    // clamped
  CHECK_EQ(settings::speedPixelsPerSecond(200), 75);  // clamped
  for (uint8_t s = 2; s <= 10; ++s) CHECK(settings::speedPixelsPerSecond(s) > settings::speedPixelsPerSecond(s - 1));
  CHECK_EQ(settings::contrastFromBrightness(0), 0);
  CHECK_EQ(settings::contrastFromBrightness(50), 128);
  CHECK_EQ(settings::contrastFromBrightness(100), 255);
  CHECK_EQ(settings::contrastFromBrightness(250), 255);
}

TEST(settings_parse_uint_is_strict) {
  uint32_t v = 0;
  CHECK(settings::parseUint("0", 100, &v) && v == 0);
  CHECK(settings::parseUint("100", 100, &v) && v == 100);
  CHECK(settings::parseUint("007", 100, &v) && v == 7);
  CHECK(settings::parseUint("4294967295", UINT32_MAX, &v) && v == UINT32_MAX);
  const char* bad[] = {"", "-1", "+1", " 1", "1 ", "12a", "0x10", "1.5", "101", "4294967296", "99999999999"};
  for (const char* s : bad) CHECK(!settings::parseUint(s, s[0] == '4' ? UINT32_MAX : 100, &v));
}

TEST(settings_parse_bool_font_align) {
  bool b = false;
  CHECK(settings::parseBool("1", &b) && b);
  CHECK(settings::parseBool("false", &b) && !b);
  CHECK(!settings::parseBool("yes", &b));
  CHECK(!settings::parseBool("TRUE", &b));
  CHECK(!settings::parseBool("", &b));
  FontSize f;
  CHECK(settings::parseFont("small", &f) && f == FontSize::Small);
  CHECK(!settings::parseFont("Small", &f));
  CHECK(!settings::parseFont("2", &f));
  Align a;
  CHECK(settings::parseAlign("right", &a) && a == Align::Right);
  CHECK(!settings::parseAlign("middle", &a));
  CHECK_STR(settings::fontName(FontSize::Medium), "medium");
  CHECK_STR(settings::alignName(Align::Left), "left");
}

TEST(settings_apply_valid_fields) {
  DisplaySettings d;
  CHECK(settings::applyField(&d, "font", "small") == nullptr);
  CHECK(settings::applyField(&d, "align", "left") == nullptr);
  CHECK(settings::applyField(&d, "brightness", "0") == nullptr);
  CHECK(settings::applyField(&d, "scroll", "0") == nullptr);
  CHECK(settings::applyField(&d, "speed", "10") == nullptr);
  CHECK(settings::applyField(&d, "rotation", "180") == nullptr);
  CHECK(settings::applyField(&d, "invert", "true") == nullptr);
  CHECK(settings::applyField(&d, "lines", "2") == nullptr);
  CHECK(d.font == FontSize::Small && d.align == Align::Left && d.brightness == 0 && !d.scroll);
  CHECK(d.speed == 10 && d.rotate180 && d.invert && d.twoLines);
}

TEST(settings_reject_out_of_range_without_side_effects) {
  struct Case {
    const char* key;
    const char* value;
  };
  const Case bad[] = {{"font", "huge"},     {"align", "justify"}, {"brightness", "101"}, {"brightness", "-1"},
                      {"scroll", "2"},      {"speed", "0"},       {"speed", "11"},       {"rotation", "90"},
                      {"invert", "maybe"},  {"lines", "3"},       {"lines", ""},         {"bogus", "1"}};
  for (const Case& c : bad) {
    DisplaySettings d;
    const DisplaySettings before = d;
    CHECK(settings::applyField(&d, c.key, c.value) != nullptr);
    CHECK(d == before);
  }
  CHECK(settings::isKnownField("brightness"));
  CHECK(!settings::isKnownField("persist"));
  CHECK(!settings::isKnownField("message"));
}

TEST(settings_sanitize_repairs_storage_values) {
  DisplaySettings d;
  d.font = static_cast<FontSize>(9);
  d.align = static_cast<Align>(7);
  d.brightness = 180;
  d.speed = 0;
  CHECK_EQ(settings::sanitize(&d), 4);
  CHECK(d == DisplaySettings());
  CHECK_EQ(settings::sanitize(&d), 0);
}

TEST(presets_add_find_remove) {
  Presets p;
  CHECK(p.add("one"));
  CHECK(p.add("two"));
  CHECK(p.add("trei ș"));
  CHECK_EQ(p.find("two"), 1);
  CHECK_EQ(p.find("none"), -1);
  CHECK(p.remove(0));
  CHECK_EQ(p.count, 2);
  CHECK_STR(p.items[0], "two");
  CHECK_STR(p.items[1], "trei ș");
  CHECK(!p.remove(2));
  while (p.count < PRESET_MAX_COUNT) CHECK(p.add("x"));
  CHECK(!p.add("overflow"));
}
