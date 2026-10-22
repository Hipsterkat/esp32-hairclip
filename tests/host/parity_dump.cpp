// Prints the firmware's normalization and layout results as JSON lines so
// tests/python/test_parity.py can compare them with web/preview.js.
//
// Input lines (stdin):
//   N <hex utf-8 text>
//   L <font 0-2> <align left|center|right> <scroll 0|1> <twoLines 0|1> <speed 1-10> <px per char>
//     <firstBreak> <hex text>
// Empty text is written as "-".
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

#include "../../hairclip/generated_fonts.h"
#include "../../hairclip/layout.h"
#include "../../hairclip/text.h"

namespace {

// "-" stands for the empty string.
std::string fromHex(const std::string& hex) {
  std::string out;
  if (hex == "-") return out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) out += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
  return out;
}

std::string toHex(const std::string& s) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (unsigned char c : s) {
    out += digits[c >> 4];
    out += digits[c & 15];
  }
  return out;
}

uint16_t fixedWidth(const char* s, size_t len, void* ctx) {
  return static_cast<uint16_t>(text::utf8Count(s, len) * *static_cast<int*>(ctx));
}

}  // namespace

int main() {
  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream in(line);
    std::string kind;
    in >> kind;
    if (kind == "N") {
      std::string hex;
      in >> hex;
      const std::string src = fromHex(hex);
      char out[MESSAGE_MAX_BYTES + 1];
      const text::NormalizeResult r =
          text::normalizeMessage(src.data(), src.size(), out, sizeof(out), MESSAGE_MAX_CHARS, MESSAGE_MAX_BYTES);
      const char* status = r.status == text::NormalizeStatus::Ok ? "ok"
                           : r.status == text::NormalizeStatus::TooLong ? "toolong"
                                                                         : "invalid";
      std::printf("{\"status\":\"%s\",\"out\":\"%s\",\"changed\":%s}\n", status,
                  toHex(std::string(out, r.bytes)).c_str(), r.changed ? "true" : "false");
    } else if (kind == "L") {
      int font, scroll, two, speed, px, firstBreak;
      std::string align, hex;
      in >> font >> align >> scroll >> two >> speed >> px >> firstBreak >> hex;
      const std::string s = fromHex(hex);
      layout::Options opt;
      settings::parseAlign(align.c_str(), &opt.align);
      opt.scroll = scroll != 0;
      opt.twoLines = two != 0;
      opt.speedPxPerSec = settings::speedPixelsPerSecond(static_cast<uint8_t>(speed));
      const layout::Layout l = layout::compute(s.data(), s.size(), firstBreak, kFontMetrics[font], opt, fixedWidth, &px);
      std::printf("{\"gap\":%u,\"lanes\":[", l.gap);
      for (uint8_t i = 0; i < l.laneCount; ++i) {
        const layout::Lane& ln = l.lanes[i];
        std::printf("%s{\"start\":%u,\"end\":%u,\"width\":%u,\"baseline\":%d,\"x\":%d,\"scrolls\":%s}", i ? "," : "",
                    ln.start, ln.start + ln.length, ln.width, ln.baseline, ln.x, ln.scrolls ? "true" : "false");
      }
      std::printf("],\"offsets\":[");
      const uint32_t probes[] = {0, 1199, 1200, 1300, 2500, 5000, 12345, 99999};
      for (size_t k = 0; k < sizeof(probes) / sizeof(probes[0]); ++k) {
        std::printf("%s%u", k ? "," : "", layout::scrollOffset(l.lanes[0], l, probes[k]));
      }
      std::printf("]}\n");
    }
  }
  return 0;
}
