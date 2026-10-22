#include "../../hairclip/generated_fonts.h"
#include "../../hairclip/layout.h"
#include "../../hairclip/text.h"
#include "test.h"

namespace {

// Fixed-width stand-in for U8g2's string width: ctx points to px per character.
uint16_t fixedWidth(const char* s, size_t len, void* ctx) {
  return static_cast<uint16_t>(text::utf8Count(s, len) * *static_cast<int*>(ctx));
}

const FontMetrics& small() { return kFontMetrics[0]; }
const FontMetrics& medium() { return kFontMetrics[1]; }
const FontMetrics& large() { return kFontMetrics[2]; }

layout::Layout run(const std::string& s, int px, const layout::Options& opt, const FontMetrics& m,
                   int32_t firstBreak = -1) {
  return layout::compute(s.data(), s.size(), firstBreak, m, opt, fixedWidth, &px);
}

std::string lane(const std::string& s, const layout::Lane& l) { return s.substr(l.start, l.length); }

}  // namespace

TEST(layout_font_geometry) {
  CHECK(layout::fitsTwoLines(small(), 32));
  CHECK(!layout::fitsTwoLines(medium(), 32));
  CHECK(!layout::fitsTwoLines(large(), 32));
  CHECK_EQ(layout::singleLineBaseline(small(), 32), 19);
  CHECK_EQ(layout::singleLineBaseline(medium(), 32), 20);
  CHECK_EQ(layout::singleLineBaseline(large(), 32), 24);
  int16_t b1, b2;
  layout::twoLineBaselines(small(), 32, &b1, &b2);
  CHECK_EQ(b1, 11);
  CHECK_EQ(b2, 27);
  // Accents (above) and commas (below) of every Latin-1/Romanian glyph stay on the panel.
  for (const FontMetrics& m : kFontMetrics) {
    const int b = layout::singleLineBaseline(m, 32);
    CHECK(b - m.lineAscent >= 0);
    CHECK(b + m.lineDescent <= 32);
  }
  CHECK(b2 + small().lineDescent <= 32);
  CHECK(b1 + small().lineDescent <= b2 - small().lineAscent);  // lines never overlap
}

TEST(layout_alignment_when_text_fits) {
  layout::Options opt;
  opt.align = Align::Center;
  layout::Layout l = run("Hello", 6, opt, large());
  CHECK_EQ(l.laneCount, 1);
  CHECK_EQ(l.lanes[0].width, 30);
  CHECK_EQ(l.lanes[0].x, 49);
  CHECK(!l.lanes[0].scrolls);
  opt.align = Align::Left;
  CHECK_EQ(run("Hello", 6, opt, large()).lanes[0].x, 0);
  opt.align = Align::Right;
  CHECK_EQ(run("Hello", 6, opt, large()).lanes[0].x, 98);
}

TEST(layout_scroll_boundary_at_panel_width) {
  layout::Options opt;
  CHECK(!run(std::string(128, 'x'), 1, opt, large()).lanes[0].scrolls);  // exactly 128 px fits
  layout::Layout l = run(std::string(129, 'x'), 1, opt, large());
  CHECK(l.lanes[0].scrolls);
  CHECK_EQ(l.lanes[0].x, 0);
  opt.scroll = false;
  l = run(std::string(129, 'x'), 1, opt, large());
  CHECK(!l.lanes[0].scrolls);  // shown from its first character
  CHECK_EQ(l.lanes[0].x, 0);
  CHECK(!run("", 6, opt, large()).lanes[0].scrolls);
}

TEST(layout_two_lines_balanced_split) {
  layout::Options opt;
  opt.twoLines = true;
  const std::string s = "aaaa bbbb cccc dddd eeee";  // 24 chars * 8 px = 192 px
  const layout::Layout l = run(s, 8, opt, small());
  CHECK_EQ(l.laneCount, 2);
  CHECK_STR(lane(s, l.lanes[0]), "aaaa bbbb");
  CHECK_STR(lane(s, l.lanes[1]), "cccc dddd eeee");
  CHECK_EQ(l.lanes[0].baseline, 11);
  CHECK_EQ(l.lanes[1].baseline, 27);
  CHECK(!l.lanes[0].scrolls && !l.lanes[1].scrolls);
}

TEST(layout_two_lines_uses_explicit_break) {
  layout::Options opt;
  opt.twoLines = true;
  const std::string s = "Hi  There";  // "Hi\n There" after buildDisplayText
  const layout::Layout l = run(s, 6, opt, small(), 2);
  CHECK_EQ(l.laneCount, 2);
  CHECK_STR(lane(s, l.lanes[0]), "Hi");
  CHECK_STR(lane(s, l.lanes[1]), "There");
  CHECK_EQ(l.lanes[0].x, (128 - 12) / 2);
}

TEST(layout_two_lines_falls_back_to_one_scrolling_line) {
  layout::Options opt;
  opt.twoLines = true;
  // Short text: one centered line even in two-line mode.
  CHECK_EQ(run("Hello", 6, opt, small()).laneCount, 1);
  // A word too long for a line: scroll on one line instead.
  layout::Layout l = run(std::string(40, 'w') + " end", 6, opt, small());
  CHECK_EQ(l.laneCount, 1);
  CHECK(l.lanes[0].scrolls);
  // Explicit break whose second line is too wide.
  const std::string s = "Hi " + std::string(30, 'x');
  l = run(s, 6, opt, small(), 2);
  CHECK_EQ(l.laneCount, 1);
  // Fonts that cannot fit two lines ignore the option.
  CHECK_EQ(run("aaaa bbbb cccc dddd eeee", 8, opt, large()).laneCount, 1);
}

TEST(layout_scroll_timeline) {
  layout::Options opt;
  opt.speedPxPerSec = 25;
  const layout::Layout l = run(std::string(200, 'x'), 1, opt, small());
  const layout::Lane& ln = l.lanes[0];
  CHECK(ln.scrolls);
  CHECK_EQ(l.gap, 16);
  const uint32_t period = 216;
  CHECK_EQ(layout::cycleMs(ln, l), 1200u + 8640u);
  CHECK_EQ(layout::scrollOffset(ln, l, 0), 0u);
  CHECK_EQ(layout::scrollOffset(ln, l, 1199), 0u);
  CHECK_EQ(layout::scrollOffset(ln, l, 1240), 1u);
  CHECK_EQ(layout::scrollOffset(ln, l, 1200 + 8639), 215u);
  CHECK_EQ(layout::scrollOffset(ln, l, 9840), 0u);  // next cycle
  uint32_t last = 0;
  for (uint32_t t = 0; t < layout::cycleMs(ln, l); ++t) {
    const uint32_t off = layout::scrollOffset(ln, l, t);
    CHECK(off < period);
    CHECK(off >= last);  // monotonic within a cycle
    last = off;
  }
}

TEST(layout_next_step_timing) {
  layout::Options opt;
  opt.speedPxPerSec = 25;
  const layout::Layout l = run(std::string(200, 'x'), 1, opt, small());
  const layout::Lane& ln = l.lanes[0];
  CHECK_EQ(layout::msUntilNextStep(ln, l, 0), 1200u);
  CHECK_EQ(layout::msUntilNextStep(ln, l, 1200), 40u);
  CHECK_EQ(layout::msUntilNextStep(ln, l, 1239), 1u);
  CHECK_EQ(layout::msUntilNextStep(ln, l, 9839), 1u);
  // Waiting exactly that long always changes the offset (or starts a new cycle).
  for (uint32_t t = 1200; t < 9840; t += 7) {
    const uint32_t wait = layout::msUntilNextStep(ln, l, t);
    CHECK(wait >= 1);
    const uint32_t a = layout::scrollOffset(ln, l, t);
    const uint32_t b = layout::scrollOffset(ln, l, t + wait);
    CHECK(b != a || (t + wait) % layout::cycleMs(ln, l) < 1200);
  }
  const layout::Layout fits = run("short", 6, opt, small());
  CHECK_EQ(layout::msUntilNextStep(fits.lanes[0], fits, 5), UINT32_MAX);
}

TEST(layout_scroll_survives_large_elapsed_values) {
  layout::Options opt;
  opt.speedPxPerSec = 75;
  const layout::Layout l = run(std::string(MESSAGE_MAX_CHARS, 'W'), 20, opt, large());
  const layout::Lane& ln = l.lanes[0];
  const uint32_t period = ln.width + l.gap;
  const uint32_t probes[] = {0u, 123456u, 0x7FFFFFFFu, 0xFFFFFFFEu, 0xFFFFFFFFu};
  for (uint32_t t : probes) CHECK(layout::scrollOffset(ln, l, t) < period);
}
