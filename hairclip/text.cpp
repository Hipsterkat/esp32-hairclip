#include "text.h"

#include <string.h>

namespace text {

// ---------------------------------------------------------------------------
// StrBuf
// ---------------------------------------------------------------------------

StrBuf::StrBuf(char* buf, size_t cap) : buf_(buf), cap_(cap), len_(0), overflow_(false) {
  if (cap_ > 0) buf_[0] = '\0';
}

void StrBuf::clear() {
  len_ = 0;
  overflow_ = false;
  if (cap_ > 0) buf_[0] = '\0';
}

void StrBuf::append(char c) { append(&c, 1); }

void StrBuf::append(const char* s) { append(s, strlen(s)); }

void StrBuf::append(const char* s, size_t n) {
  if (cap_ == 0 || n > cap_ - 1 - len_) {
    overflow_ = true;
    return;
  }
  memcpy(buf_ + len_, s, n);
  len_ += n;
  buf_[len_] = '\0';
}

void StrBuf::appendUInt(uint32_t v) {
  char tmp[10];
  size_t n = 0;
  do {
    tmp[sizeof(tmp) - 1 - n++] = static_cast<char>('0' + v % 10);
    v /= 10;
  } while (v != 0);
  append(tmp + sizeof(tmp) - n, n);
}

void StrBuf::appendInt(int32_t v) {
  char tmp[11];
  uint32_t mag = v < 0 ? static_cast<uint32_t>(-(static_cast<int64_t>(v))) : static_cast<uint32_t>(v);
  size_t n = 0;
  do {
    tmp[sizeof(tmp) - 1 - n++] = static_cast<char>('0' + mag % 10);
    mag /= 10;
  } while (mag != 0);
  if (v < 0) tmp[sizeof(tmp) - 1 - n++] = '-';
  append(tmp + sizeof(tmp) - n, n);
}

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

size_t utf8Decode(const char* s, size_t len, size_t i, uint32_t* cp) {
  if (i >= len) return 0;
  const uint8_t b0 = static_cast<uint8_t>(s[i]);
  if (b0 < 0x80) {
    *cp = b0;
    return 1;
  }
  size_t n;
  uint32_t v;
  uint32_t min;
  if (b0 >= 0xC2 && b0 <= 0xDF) {
    n = 2;
    v = b0 & 0x1Fu;
    min = 0x80;
  } else if (b0 >= 0xE0 && b0 <= 0xEF) {
    n = 3;
    v = b0 & 0x0Fu;
    min = 0x800;
  } else if (b0 >= 0xF0 && b0 <= 0xF4) {
    n = 4;
    v = b0 & 0x07u;
    min = 0x10000;
  } else {
    return 0;  // continuation byte, C0/C1 overlong lead, or F5..FF
  }
  if (n > len - i) return 0;
  for (size_t k = 1; k < n; ++k) {
    const uint8_t b = static_cast<uint8_t>(s[i + k]);
    if ((b & 0xC0) != 0x80) return 0;
    v = (v << 6) | (b & 0x3Fu);
  }
  if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 0;
  *cp = v;
  return n;
}

size_t utf8Encode(uint32_t cp, char out[4]) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}

bool utf8Valid(const char* s, size_t len) {
  size_t i = 0;
  while (i < len) {
    uint32_t cp;
    const size_t n = utf8Decode(s, len, i, &cp);
    if (n == 0) return false;
    i += n;
  }
  return true;
}

size_t utf8Count(const char* s, size_t len) {
  size_t count = 0;
  for (size_t i = 0; i < len; ++i) {
    if ((static_cast<uint8_t>(s[i]) & 0xC0) != 0x80) ++count;
  }
  return count;
}

size_t utf8Truncate(const char* s, size_t len, size_t maxBytes, size_t maxChars) {
  size_t i = 0;
  size_t chars = 0;
  while (i < len && chars < maxChars) {
    uint32_t cp;
    const size_t n = utf8Decode(s, len, i, &cp);
    if (n == 0 || n > maxBytes - i) break;
    i += n;
    ++chars;
  }
  return i;
}

// ---------------------------------------------------------------------------
// Message policy
// ---------------------------------------------------------------------------

namespace {

struct CharMapEntry {
  uint32_t first;
  uint32_t last;
  const char* replacement;  // ASCII only; never longer than the UTF-8 it replaces
};

// BEGIN CHARMAP (tools/generate_assets.py copies this table into the web UI)
const CharMapEntry kCharMap[] = {
    // Line and paragraph separators.
    {0x0085, 0x0085, "\n"},
    {0x2028, 0x2029, "\n"},
    // Tabs and Unicode spaces.
    {0x0009, 0x0009, " "},
    {0x000B, 0x000C, " "},
    {0x00A0, 0x00A0, " "},
    {0x1680, 0x1680, " "},
    {0x2000, 0x200A, " "},
    {0x202F, 0x202F, " "},
    {0x205F, 0x205F, " "},
    {0x3000, 0x3000, " "},
    // Invisible formatting: soft hyphen, joiners, direction marks, variation
    // selectors, byte-order mark, emoji skin tones and tag characters.
    {0x00AD, 0x00AD, ""},
    {0x034F, 0x034F, ""},
    {0x200B, 0x200F, ""},
    {0x202A, 0x202E, ""},
    {0x2060, 0x2064, ""},
    {0x2066, 0x206F, ""},
    {0xFE00, 0xFE0F, ""},
    {0xFEFF, 0xFEFF, ""},
    {0x1F3FB, 0x1F3FF, ""},
    {0xE0000, 0xE007F, ""},
    // Typographic punctuation inserted by phone keyboards.
    {0x2018, 0x201B, "'"},
    {0x2032, 0x2032, "'"},
    {0x201C, 0x201F, "\""},
    {0x2033, 0x2033, "\""},
    {0x2010, 0x2015, "-"},
    {0x2212, 0x2212, "-"},
    {0x2026, 0x2026, "..."},
    {0x2022, 0x2022, "*"},
    {0x2039, 0x2039, "<"},
    {0x203A, 0x203A, ">"},
    {0x20AC, 0x20AC, "EUR"},
    {0x2122, 0x2122, "TM"},
    // A few emoji, as classic emoticons.
    {0x2661, 0x2661, "<3"},
    {0x2665, 0x2665, "<3"},
    {0x2763, 0x2764, "<3"},
    {0x1F493, 0x1F493, "<3"},
    {0x1F495, 0x1F49F, "<3"},
    {0x1F5A4, 0x1F5A4, "<3"},
    {0x1F90D, 0x1F90E, "<3"},
    {0x1F9E1, 0x1F9E1, "<3"},
    {0x1FA75, 0x1FA77, "<3"},
    {0x1F494, 0x1F494, "</3"},
    {0x263A, 0x263A, ":)"},
    {0x1F60A, 0x1F60A, ":)"},
    {0x1F642, 0x1F642, ":)"},
    {0x1F600, 0x1F606, ":D"},
    {0x1F923, 0x1F923, ":D"},
    {0x1F609, 0x1F609, ";)"},
    {0x1F61B, 0x1F61C, ":P"},
    {0x1F618, 0x1F618, ":*"},
    {0x1F60E, 0x1F60E, "B)"},
    {0x2639, 0x2639, ":("},
    {0x1F641, 0x1F641, ":("},
    {0x1F61E, 0x1F61E, ":("},
    {0x1F622, 0x1F622, ":("},
    {0x1F62D, 0x1F62D, ":("},
    {0x2605, 0x2606, "*"},
    {0x2728, 0x2728, "*"},
    {0x2B50, 0x2B50, "*"},
    {0x1F31F, 0x1F31F, "*"},
};
// END CHARMAP

const char* lookupCharMap(uint32_t cp) {
  for (const CharMapEntry& e : kCharMap) {
    if (cp >= e.first && cp <= e.last) return e.replacement;
  }
  return nullptr;
}

bool isControl(uint32_t cp) { return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F); }

// Comma-below and cedilla forms of Romanian S/T stand in for each other.
uint32_t romanianTwin(uint32_t cp) {
  switch (cp) {
    case 0x0218: return 0x015E;
    case 0x0219: return 0x015F;
    case 0x021A: return 0x0162;
    case 0x021B: return 0x0163;
    case 0x015E: return 0x0218;
    case 0x015F: return 0x0219;
    case 0x0162: return 0x021A;
    case 0x0163: return 0x021B;
    default: return 0;
  }
}

// Output side of normalizeMessage: drops leading whitespace, defers the
// decision on trailing whitespace, and tracks the byte/char limits.
class Normalizer {
 public:
  Normalizer(char* out, size_t limit) : out_(out), limit_(limit) {}

  void put(uint32_t cp) {
    if (overflow_) return;
    const bool ws = (cp == ' ' || cp == '\n');
    if (ws && !started_) return;
    char enc[4];
    const size_t n = utf8Encode(cp, enc);
    if (n > limit_ - len_) {
      // Whitespace that does not fit only matters if content follows it.
      if (ws) {
        wsDropped_ = true;
      } else {
        overflow_ = true;
      }
      return;
    }
    if (!ws && wsDropped_) {
      overflow_ = true;
      return;
    }
    memcpy(out_ + len_, enc, n);
    len_ += n;
    ++chars_;
    if (!ws) {
      started_ = true;
      contentLen_ = len_;
      contentChars_ = chars_;
    }
  }

  void putAscii(const char* s) {
    for (; *s != '\0'; ++s) put(static_cast<uint8_t>(*s));
  }

  bool overflow() const { return overflow_; }
  size_t contentLen() const { return contentLen_; }
  size_t contentChars() const { return contentChars_; }

 private:
  char* out_;
  size_t limit_;
  size_t len_ = 0;
  size_t chars_ = 0;
  size_t contentLen_ = 0;
  size_t contentChars_ = 0;
  bool started_ = false;
  bool wsDropped_ = false;
  bool overflow_ = false;
};

}  // namespace

NormalizeResult normalizeMessage(const char* in, size_t len, char* out, size_t outCap,
                                 size_t maxChars, size_t maxBytes) {
  NormalizeResult r{NormalizeStatus::Ok, 0, 0, false};
  if (outCap == 0) {
    r.status = NormalizeStatus::TooLong;
    return r;
  }
  out[0] = '\0';
  if (!utf8Valid(in, len)) {
    r.status = NormalizeStatus::InvalidUtf8;
    return r;
  }
  const size_t limit = maxBytes < outCap - 1 ? maxBytes : outCap - 1;
  Normalizer norm(out, limit);
  size_t i = 0;
  while (i < len && !norm.overflow()) {
    uint32_t cp;
    i += utf8Decode(in, len, i, &cp);
    if (cp == '\r') {
      if (i < len && in[i] == '\n') continue;  // CRLF: the LF follows
      norm.put('\n');
    } else if (cp == '\n') {
      norm.put('\n');
    } else if (const char* repl = lookupCharMap(cp)) {
      norm.putAscii(repl);
      r.changed = true;
    } else if (isControl(cp)) {
      r.changed = true;
    } else {
      norm.put(cp);
    }
  }
  out[norm.contentLen()] = '\0';
  r.bytes = norm.contentLen();
  r.chars = norm.contentChars();
  if (norm.overflow() || r.chars > maxChars) {
    r.status = NormalizeStatus::TooLong;
    out[0] = '\0';
    r.bytes = 0;
    r.chars = 0;
  }
  return r;
}

bool isDisplayable(uint32_t cp) {
  return (cp >= 0x20 && cp <= 0x7E) || (cp >= 0xA0 && cp <= 0x17E) ||
         (cp >= 0x218 && cp <= 0x21B);
}

uint32_t displayCodepoint(uint32_t cp, GlyphCheck hasGlyph, void* ctx) {
  if (isDisplayable(cp) && (hasGlyph == nullptr || hasGlyph(cp, ctx))) return cp;
  const uint32_t twin = romanianTwin(cp);
  if (twin != 0 && (hasGlyph == nullptr || hasGlyph(twin, ctx))) return twin;
  return '?';
}

size_t buildDisplayText(const char* msg, size_t len, GlyphCheck hasGlyph, void* ctx,
                        char* out, size_t outCap, int32_t* firstBreak) {
  *firstBreak = -1;
  if (outCap == 0) return 0;
  size_t o = 0;
  size_t i = 0;
  while (i < len) {
    uint32_t cp;
    size_t n = utf8Decode(msg, len, i, &cp);
    if (n == 0) {  // tolerate corrupted input: draw '?' and resynchronize
      cp = 0xFFFD;
      n = 1;
    }
    i += n;
    uint32_t shown;
    if (cp == '\n') {
      if (*firstBreak < 0) *firstBreak = static_cast<int32_t>(o);
      shown = ' ';
    } else {
      shown = displayCodepoint(cp, hasGlyph, ctx);
    }
    char enc[4];
    const size_t m = utf8Encode(shown, enc);
    if (m > outCap - 1 - o) break;
    memcpy(out + o, enc, m);
    o += m;
  }
  out[o] = '\0';
  return o;
}

size_t collectUndisplayable(const char* msg, size_t len, uint32_t* out, size_t maxOut) {
  size_t count = 0;
  size_t i = 0;
  while (i < len && count < maxOut) {
    uint32_t cp;
    size_t n = utf8Decode(msg, len, i, &cp);
    if (n == 0) {
      cp = 0xFFFD;
      n = 1;
    }
    i += n;
    if (cp == '\n' || isDisplayable(cp)) continue;
    bool seen = false;
    for (size_t k = 0; k < count; ++k) seen = seen || out[k] == cp;
    if (!seen) out[count++] = cp;
  }
  return count;
}

// ---------------------------------------------------------------------------
// Escaping
// ---------------------------------------------------------------------------

void jsonEscape(StrBuf& out, const char* s, size_t len) {
  static const char kHex[] = "0123456789abcdef";
  size_t i = 0;
  while (i < len) {
    uint32_t cp;
    const size_t n = utf8Decode(s, len, i, &cp);
    if (n == 0) {
      out.append("\\ufffd");
      ++i;
      continue;
    }
    switch (cp) {
      case '"': out.append("\\\""); break;
      case '\\': out.append("\\\\"); break;
      case '\n': out.append("\\n"); break;
      case '\r': out.append("\\r"); break;
      case '\t': out.append("\\t"); break;
      case '\b': out.append("\\b"); break;
      case '\f': out.append("\\f"); break;
      default:
        if (cp < 0x20 || cp == 0x7F || cp == '<' || cp == '>' || cp == '&' || cp == 0x2028 ||
            cp == 0x2029) {
          const char esc[6] = {'\\', 'u', kHex[(cp >> 12) & 15], kHex[(cp >> 8) & 15],
                               kHex[(cp >> 4) & 15], kHex[cp & 15]};
          out.append(esc, sizeof(esc));
        } else {
          out.append(s + i, n);
        }
    }
    i += n;
  }
}

void htmlEscape(StrBuf& out, const char* s, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    switch (s[i]) {
      case '&': out.append("&amp;"); break;
      case '<': out.append("&lt;"); break;
      case '>': out.append("&gt;"); break;
      case '"': out.append("&quot;"); break;
      case '\'': out.append("&#39;"); break;
      default: out.append(s[i]);
    }
  }
}

}  // namespace text
