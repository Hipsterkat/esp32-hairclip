#include "../../hairclip/config.h"
#include "../../hairclip/text.h"
#include "test.h"

using text::NormalizeStatus;

namespace {

struct Norm {
  NormalizeStatus status;
  std::string out;
  bool changed;
  size_t chars;
};

Norm normalize(const std::string& in, size_t maxChars = MESSAGE_MAX_CHARS, size_t maxBytes = MESSAGE_MAX_BYTES) {
  char out[MESSAGE_MAX_BYTES + 1];
  const text::NormalizeResult r = text::normalizeMessage(in.data(), in.size(), out, sizeof(out), maxChars, maxBytes);
  return {r.status, std::string(out, r.bytes), r.changed, r.chars};
}

std::string repeat(const std::string& s, size_t n) {
  std::string out;
  for (size_t i = 0; i < n; ++i) out += s;
  return out;
}

std::string json(const std::string& s) {
  char buf[512];
  text::StrBuf b(buf, sizeof(buf));
  text::jsonEscape(b, s.data(), s.size());
  return b.c_str();
}

std::string html(const std::string& s) {
  char buf[512];
  text::StrBuf b(buf, sizeof(buf));
  text::htmlEscape(b, s.data(), s.size());
  return b.c_str();
}

std::string display(const std::string& s, text::GlyphCheck check = nullptr, void* ctx = nullptr,
                    int32_t* brk = nullptr, size_t cap = 300) {
  std::vector<char> out(cap);
  int32_t ignored;
  const size_t n = text::buildDisplayText(s.data(), s.size(), check, ctx, out.data(), cap, brk ? brk : &ignored);
  return std::string(out.data(), n);
}

bool fontWithoutCommaBelow(uint32_t cp, void*) { return cp < 0x218 || cp > 0x21B; }
bool fontWithoutRomanianST(uint32_t cp, void*) {
  return !(cp >= 0x218 && cp <= 0x21B) && cp != 0x15E && cp != 0x15F && cp != 0x162 && cp != 0x163;
}

}  // namespace

// ---- UTF-8 ------------------------------------------------------------------

TEST(utf8_decodes_valid_sequences) {
  const std::string s = "Aă€😀";  // 1, 2, 3 and 4 byte sequences
  uint32_t cp = 0;
  CHECK_EQ(text::utf8Decode(s.data(), s.size(), 0, &cp), 1u);
  CHECK_EQ(cp, 0x41u);
  CHECK_EQ(text::utf8Decode(s.data(), s.size(), 1, &cp), 2u);
  CHECK_EQ(cp, 0x103u);
  CHECK_EQ(text::utf8Decode(s.data(), s.size(), 3, &cp), 3u);
  CHECK_EQ(cp, 0x20ACu);
  CHECK_EQ(text::utf8Decode(s.data(), s.size(), 6, &cp), 4u);
  CHECK_EQ(cp, 0x1F600u);
  CHECK(text::utf8Valid(s.data(), s.size()));
  CHECK_EQ(text::utf8Count(s.data(), s.size()), 4u);
}

TEST(utf8_rejects_malformed_sequences) {
  const char* bad[] = {
      "\x80",              // stray continuation
      "\xC0\x80",          // overlong NUL
      "\xC1\xBF",          // overlong
      "\xE0\x80\x80",      // overlong 3-byte
      "\xED\xA0\x80",      // UTF-16 surrogate
      "\xF4\x90\x80\x80",  // above U+10FFFF
      "\xF5\x80\x80\x80",  // invalid lead
      "\xFF",              // invalid byte
      "\xC3",              // truncated 2-byte
      "\xE2\x82",          // truncated 3-byte
      "\xC3\x28",          // bad continuation
  };
  for (const char* b : bad) {
    uint32_t cp;
    CHECK_EQ(text::utf8Decode(b, strlen(b), 0, &cp), 0u);
    CHECK(!text::utf8Valid(b, strlen(b)));
  }
}

TEST(utf8_encode_roundtrip) {
  const uint32_t cps[] = {0x24, 0xA2, 0x219, 0x20AC, 0xFFFD, 0x10348, 0x10FFFF};
  for (uint32_t cp : cps) {
    char buf[4];
    const size_t n = text::utf8Encode(cp, buf);
    uint32_t back = 0;
    CHECK_EQ(text::utf8Decode(buf, n, 0, &back), n);
    CHECK_EQ(back, cp);
  }
}

TEST(utf8_truncate_never_splits_sequences) {
  const std::string s = "aăb";  // 1 + 2 + 1 bytes
  CHECK_EQ(text::utf8Truncate(s.data(), s.size(), 1, 99), 1u);
  CHECK_EQ(text::utf8Truncate(s.data(), s.size(), 2, 99), 1u);  // would split 'ă'
  CHECK_EQ(text::utf8Truncate(s.data(), s.size(), 3, 99), 3u);
  CHECK_EQ(text::utf8Truncate(s.data(), s.size(), 99, 2), 3u);  // char limit
  CHECK_EQ(text::utf8Truncate(s.data(), s.size(), 0, 99), 0u);

  const std::string emoji = "ok😀";
  for (size_t maxBytes = 2; maxBytes < 6; ++maxBytes) {
    CHECK_EQ(text::utf8Truncate(emoji.data(), emoji.size(), maxBytes, 99), 2u);
  }
  CHECK_EQ(text::utf8Truncate(emoji.data(), emoji.size(), 6, 99), 6u);

  // Every prefix length of a multi-byte string gives valid UTF-8.
  const std::string ro = "Țară școală";
  for (size_t max = 0; max <= ro.size(); ++max) {
    const size_t n = text::utf8Truncate(ro.data(), ro.size(), max, 99);
    CHECK(n <= max);
    CHECK(text::utf8Valid(ro.data(), n));
  }

  // Stops before malformed bytes.
  const std::string broken = "ab\xFF" "cd";
  CHECK_EQ(text::utf8Truncate(broken.data(), broken.size(), 99, 99), 2u);
}

// ---- normalization ----------------------------------------------------------

TEST(normalize_keeps_plain_and_romanian_text) {
  Norm n = normalize("Bună ziua, Ștefan! Țară, mâine, în");
  CHECK(n.status == NormalizeStatus::Ok);
  CHECK_STR(n.out, "Bună ziua, Ștefan! Țară, mâine, în");
  CHECK(!n.changed);
  n = normalize("Hello!");
  CHECK_STR(n.out, "Hello!");
  CHECK_EQ(n.chars, 6u);
}

TEST(normalize_trims_whitespace) {
  Norm n = normalize("  \n Hello \n  ");
  CHECK_STR(n.out, "Hello");
  CHECK(!n.changed);
  CHECK_STR(normalize("").out, "");
  CHECK(normalize("   ").status == NormalizeStatus::Ok);
  CHECK_STR(normalize("   ").out, "");
}

TEST(normalize_line_breaks) {
  CHECK_STR(normalize("a\r\nb\rc\nd").out, "a\nb\nc\nd");
  CHECK_STR(normalize("a\xE2\x80\xA8" "b").out, "a\nb");  // U+2028
}

TEST(normalize_converts_phone_typography) {
  Norm n = normalize("Don’t “panic” – ok…");
  CHECK_STR(n.out, "Don't \"panic\" - ok...");
  CHECK(n.changed);
  CHECK_STR(normalize("5 €").out, "5 EUR");
  CHECK_STR(normalize("a\xC2\xA0" "b").out, "a b");  // no-break space
  CHECK_STR(normalize("tab\there").out, "tab here");
}

TEST(normalize_maps_emoji_to_emoticons) {
  CHECK_STR(normalize("I ❤️ you").out, "I <3 you");  // U+2764 U+FE0F
  CHECK_STR(normalize("hi 🙂").out, "hi :)");
  CHECK_STR(normalize("👍🏽").out, "👍");  // skin tone removed, thumbs-up kept
  CHECK_STR(normalize("💔").out, "</3");
}

TEST(normalize_removes_controls_and_invisible_characters) {
  Norm n = normalize(std::string("a\x01" "b\x7F" "c", 5));
  CHECK_STR(n.out, "abc");
  CHECK(n.changed);
  CHECK_STR(normalize(std::string("a\0b", 3)).out, "ab");
  CHECK_STR(normalize("a\xE2\x80\x8B" "b").out, "ab");  // zero-width space
  CHECK_STR(normalize("\xEF\xBB\xBFhi").out, "hi");     // byte-order mark
}

TEST(normalize_rejects_invalid_utf8) {
  CHECK(normalize("ab\xFF").status == NormalizeStatus::InvalidUtf8);
  CHECK(normalize("\xC3").status == NormalizeStatus::InvalidUtf8);
  CHECK(normalize("\xED\xA0\x80").status == NormalizeStatus::InvalidUtf8);
}

TEST(normalize_enforces_character_limit) {
  Norm n = normalize(repeat("a", MESSAGE_MAX_CHARS));
  CHECK(n.status == NormalizeStatus::Ok);
  CHECK_EQ(n.chars, MESSAGE_MAX_CHARS);
  CHECK(normalize(repeat("a", MESSAGE_MAX_CHARS + 1)).status == NormalizeStatus::TooLong);
  CHECK(normalize(repeat("ș", MESSAGE_MAX_CHARS)).status == NormalizeStatus::Ok);
  CHECK(normalize(repeat("ș", MESSAGE_MAX_CHARS + 1)).status == NormalizeStatus::TooLong);
}

TEST(normalize_enforces_byte_limit) {
  // U+1F680 is 4 bytes and not in the char map: 64 fit in 256 bytes, 65 do not.
  CHECK(normalize(repeat("🚀", 64)).status == NormalizeStatus::Ok);
  Norm n = normalize(repeat("🚀", 65));
  CHECK(n.status == NormalizeStatus::TooLong);
  CHECK_STR(n.out, "");
}

TEST(normalize_whitespace_padding_does_not_count) {
  CHECK_STR(normalize(repeat(" ", 400) + "Hi" + repeat(" ", 400)).out, "Hi");
  CHECK(normalize("a" + repeat(" ", 300) + "b").status == NormalizeStatus::TooLong);
}

// ---- display text -----------------------------------------------------------

TEST(displayable_set) {
  CHECK(text::isDisplayable(' '));
  CHECK(text::isDisplayable('~'));
  CHECK(!text::isDisplayable(0x7F));
  CHECK(!text::isDisplayable(0x9F));
  CHECK(text::isDisplayable(0xA0));
  CHECK(text::isDisplayable(0x103));  // ă
  CHECK(text::isDisplayable(0x17E));
  CHECK(!text::isDisplayable(0x17F));  // long s: not in the fonts
  CHECK(text::isDisplayable(0x219));   // ș
  CHECK(!text::isDisplayable(0x21C));
  CHECK(!text::isDisplayable(0x1F600));
}

TEST(display_fallbacks) {
  CHECK_EQ(text::displayCodepoint(0x219, nullptr, nullptr), 0x219u);
  CHECK_EQ(text::displayCodepoint(0x1F600, nullptr, nullptr), static_cast<uint32_t>('?'));
  // Comma-below missing from the font: use the cedilla twin.
  CHECK_EQ(text::displayCodepoint(0x219, fontWithoutCommaBelow, nullptr), 0x15Fu);
  CHECK_EQ(text::displayCodepoint(0x21A, fontWithoutCommaBelow, nullptr), 0x162u);
  // Neither form: '?'.
  CHECK_EQ(text::displayCodepoint(0x219, fontWithoutRomanianST, nullptr), static_cast<uint32_t>('?'));
}

TEST(display_text_replaces_breaks_and_unsupported) {
  int32_t brk = 0;
  CHECK_STR(display("Line1\nLine2", nullptr, nullptr, &brk), "Line1 Line2");
  CHECK_EQ(brk, 5);
  CHECK_STR(display("ș\nț\nx", nullptr, nullptr, &brk), "ș ț x");
  CHECK_EQ(brk, 2);  // byte offset of the first break
  CHECK_STR(display("no break", nullptr, nullptr, &brk), "no break");
  CHECK_EQ(brk, -1);
  CHECK_STR(display("hi 🚀!"), "hi ?!");
  CHECK_STR(display("bad \xFF byte"), "bad ? byte");
  CHECK_STR(display("ăăă", nullptr, nullptr, nullptr, 4), "ă");  // stops at a boundary
}

TEST(collect_undisplayable_is_distinct) {
  const std::string s = "a😀b😀🚀\nc";
  uint32_t out[8];
  const size_t n = text::collectUndisplayable(s.data(), s.size(), out, 8);
  CHECK_EQ(n, 2u);
  CHECK_EQ(out[0], 0x1F600u);
  CHECK_EQ(out[1], 0x1F680u);
  CHECK_EQ(text::collectUndisplayable(s.data(), s.size(), out, 1), 1u);
}

// ---- escaping ---------------------------------------------------------------

TEST(json_escape) {
  CHECK_STR(json("plain"), "plain");
  CHECK_STR(json("a\"b\\c"), "a\\\"b\\\\c");
  CHECK_STR(json("line\nnext\ttab"), "line\\nnext\\ttab");
  CHECK_STR(json(std::string("\x01\x1F\x7F", 3)), "\\u0001\\u001f\\u007f");
  CHECK_STR(json("</script><b>&"), "\\u003c/script\\u003e\\u003cb\\u003e\\u0026");
  CHECK_STR(json("\xE2\x80\xA8\xE2\x80\xA9"), "\\u2028\\u2029");
  CHECK_STR(json("ș ț 😀"), "ș ț 😀");
  CHECK_STR(json("bad\xFF"), "bad\\ufffd");
}

TEST(html_escape) {
  CHECK_STR(html("<a href=\"x\">'&'</a>"), "&lt;a href=&quot;x&quot;&gt;&#39;&amp;&#39;&lt;/a&gt;");
  CHECK_STR(html("Bună"), "Bună");
  CHECK_STR(html("<script>alert(1)</script>"), "&lt;script&gt;alert(1)&lt;/script&gt;");
}

TEST(strbuf_is_bounded_and_atomic) {
  char buf[8];
  text::StrBuf b(buf, sizeof(buf));
  b.append("1234");
  b.append("5678");  // would not fit: dropped entirely
  CHECK_STR(b.c_str(), "1234");
  CHECK(b.overflow());
  b.clear();
  b.appendInt(INT32_MIN);
  CHECK(b.overflow());  // 11 characters do not fit in 7
  char wide[32];
  text::StrBuf w(wide, sizeof(wide));
  w.appendInt(INT32_MIN);
  w.append(' ');
  w.appendUInt(UINT32_MAX);
  w.append(' ');
  w.appendUInt(0);
  CHECK_STR(w.c_str(), "-2147483648 4294967295 0");
  CHECK(!w.overflow());
}
