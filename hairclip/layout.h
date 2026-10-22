// Text placement on the 128x32 panel and the scrolling timeline.
// Pure C++: no Arduino dependencies, compiled by the host tests as well.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "settings.h"

// Pixel extents of a font, measured from the U8g2 glyph data by
// tools/generate_assets.py (see generated_fonts.h).
struct FontMetrics {
  int8_t capAscent;    // rows above the baseline used by ASCII text
  int8_t capDescent;   // rows below the baseline used by ASCII text
  int8_t lineAscent;   // ... by ASCII, Latin-1 and Romanian: used to place lines
  int8_t lineDescent;  //     (a rare tall Latin Extended-A capital such as U+016E
                       //     may lose its top pixel row)
  int8_t maxAscent;    // rows above the baseline used by any displayable character
  int8_t maxDescent;   // rows below the baseline used by any displayable character
};

namespace layout {

static constexpr uint32_t kScrollPauseMs = 1200;

struct Options {
  uint16_t viewWidth = OLED_WIDTH;
  uint16_t viewHeight = OLED_HEIGHT;
  Align align = Align::Center;
  bool scroll = true;
  bool twoLines = false;
  uint16_t speedPxPerSec = 25;
};

struct Lane {
  uint16_t start = 0;    // byte offset into the display text
  uint16_t length = 0;   // bytes
  uint16_t width = 0;    // pixels, from the measure function
  int16_t baseline = 0;  // y of the baseline
  int16_t x = 0;         // left edge while not scrolling
  bool scrolls = false;
};

struct Layout {
  uint8_t laneCount = 0;
  Lane lanes[2];
  uint16_t gap = 0;  // pixels between the end of scrolling text and its next copy
  uint16_t speedPxPerSec = 1;
  uint32_t pauseMs = 0;  // the start of scrolling text rests this long each cycle
};

typedef uint16_t (*MeasureFn)(const char* s, size_t len, void* ctx);

bool fitsTwoLines(const FontMetrics& m, uint16_t viewHeight);
int16_t singleLineBaseline(const FontMetrics& m, uint16_t viewHeight);
void twoLineBaselines(const FontMetrics& m, uint16_t viewHeight, int16_t* first, int16_t* second);
uint16_t scrollGap(const FontMetrics& m);

// Lays out display text (which contains no '\n', see text::buildDisplayText).
// firstBreak is the byte offset of the user's first line break, or -1.
//  * Two lines are used only when requested, when the font fits two lines and
//    when both lines fit the width. The split is the user's line break, or
//    else the space that best balances the two line widths.
//  * Otherwise one vertically centered line. Text wider than the panel
//    scrolls if scrolling is enabled, or is shown from its first character.
Layout compute(const char* text, size_t len, int32_t firstBreak, const FontMetrics& m,
               const Options& opt, MeasureFn measure, void* ctx);

// Length of one scroll cycle (pause + one full period) in milliseconds.
uint32_t cycleMs(const Lane& lane, const Layout& l);
// Scroll position (0 .. width+gap-1) of a scrolling lane, elapsedMs after the
// animation started. The text rests at offset 0 for pauseMs every cycle.
uint32_t scrollOffset(const Lane& lane, const Layout& l, uint32_t elapsedMs);
// Milliseconds (>= 1) until scrollOffset() next changes; UINT32_MAX if the
// lane does not scroll.
uint32_t msUntilNextStep(const Lane& lane, const Layout& l, uint32_t elapsedMs);

}  // namespace layout
