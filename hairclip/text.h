// UTF-8 handling, the message text policy and output escaping.
// Pure C++: no Arduino dependencies, compiled by the host tests as well.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace text {

// Fixed-capacity string builder. Never writes past its buffer, always keeps it
// NUL-terminated, and drops a whole append (setting overflow()) rather than
// writing half of it, so escaped output is never cut mid-sequence.
class StrBuf {
 public:
  StrBuf(char* buf, size_t cap);
  void clear();
  void append(char c);
  void append(const char* s);
  void append(const char* s, size_t n);
  void appendUInt(uint32_t v);
  void appendInt(int32_t v);
  const char* c_str() const { return buf_; }
  size_t length() const { return len_; }
  bool overflow() const { return overflow_; }

 private:
  char* buf_;
  size_t cap_;
  size_t len_;
  bool overflow_;
};

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

// Decodes the sequence starting at s[i]. Returns its length (1-4) and stores
// the code point, or returns 0 for malformed input: stray continuation bytes,
// overlong forms, UTF-16 surrogates, values above U+10FFFF, truncation.
size_t utf8Decode(const char* s, size_t len, size_t i, uint32_t* cp);
// Encodes a valid scalar value; returns the number of bytes written (1-4).
size_t utf8Encode(uint32_t cp, char out[4]);
bool utf8Valid(const char* s, size_t len);
// Number of code points in valid UTF-8.
size_t utf8Count(const char* s, size_t len);
// Byte length of the longest prefix with at most maxBytes bytes and maxChars
// code points. Never ends inside a multi-byte sequence and stops before any
// malformed byte.
size_t utf8Truncate(const char* s, size_t len, size_t maxBytes, size_t maxChars);

// ---------------------------------------------------------------------------
// Message policy
// ---------------------------------------------------------------------------

enum class NormalizeStatus : uint8_t { Ok, InvalidUtf8, TooLong };

struct NormalizeResult {
  NormalizeStatus status;
  size_t bytes;  // output length, excluding the terminator
  size_t chars;  // output length in code points
  bool changed;  // characters were converted or removed (not counting trims)
};

// Cleans text typed on a phone before it is stored or displayed:
//  * malformed UTF-8 is rejected (InvalidUtf8)
//  * CRLF/CR/U+2028/U+2029 become '\n'; tabs and Unicode spaces become ' '
//  * control, zero-width and emoji-modifier characters are removed
//  * typographic punctuation and a few emoji become ASCII (kCharMap)
//  * leading and trailing whitespace is trimmed
//  * results longer than maxChars/maxBytes are rejected (TooLong), never cut
NormalizeResult normalizeMessage(const char* in, size_t len, char* out, size_t outCap,
                                 size_t maxChars, size_t maxBytes);

// Characters the firmware draws. Every bundled font contains a glyph for each
// of them (verified by tests/python/test_fonts.py):
//   U+0020-007E  ASCII
//   U+00A0-017E  Latin-1 Supplement and Latin Extended-A (incl. Ă ă Â â Î î Ş ş Ţ ţ;
//                U+017F long s is absent from the fonts)
//   U+0218-021B  Romanian comma-below Ș ș Ț ț
bool isDisplayable(uint32_t cp);

typedef bool (*GlyphCheck)(uint32_t cp, void* ctx);

// The code point actually drawn for cp: cp itself when displayable and present
// in the font, otherwise the comma-below/cedilla twin, otherwise '?'.
// hasGlyph may be null, meaning "the font has every displayable glyph".
uint32_t displayCodepoint(uint32_t cp, GlyphCheck hasGlyph, void* ctx);

// Turns a stored message into the renderer's text: each code point becomes
// displayCodepoint(), each line break a space. *firstBreak receives the byte
// offset (in the output) of the first former line break, or -1. Stops at a
// code point boundary if out is too small. Returns the output length.
size_t buildDisplayText(const char* msg, size_t len, GlyphCheck hasGlyph, void* ctx,
                        char* out, size_t outCap, int32_t* firstBreak);

// Collects up to maxOut distinct non-displayable code points of msg (line
// breaks excluded). Returns how many were stored.
size_t collectUndisplayable(const char* msg, size_t len, uint32_t* out, size_t maxOut);

// ---------------------------------------------------------------------------
// Escaping
// ---------------------------------------------------------------------------

// Appends s as the inside of a JSON string literal. Escapes '"', '\\', all
// control characters, '<', '>', '&', U+2028 and U+2029, so the result is
// also inert inside HTML. Malformed UTF-8 bytes become �.
void jsonEscape(StrBuf& out, const char* s, size_t len);
// Appends s with & < > " ' replaced by HTML entities.
void htmlEscape(StrBuf& out, const char* s, size_t len);

}  // namespace text
