#include "layout.h"

namespace layout {

namespace {

int16_t alignedX(Align align, uint16_t width, uint16_t view) {
  if (width >= view) return 0;
  switch (align) {
    case Align::Left: return 0;
    case Align::Right: return static_cast<int16_t>(view - width);
    default: return static_cast<int16_t>((view - width) / 2);
  }
}

void trimSpaces(const char* t, size_t* start, size_t* end) {
  while (*start < *end && t[*start] == ' ') ++*start;
  while (*end > *start && t[*end - 1] == ' ') --*end;
}

uint32_t moveMs(const Lane& lane, const Layout& l) {
  const uint32_t period = static_cast<uint32_t>(lane.width) + l.gap;
  return (period * 1000u + l.speedPxPerSec - 1) / l.speedPxPerSec;
}

Lane makeLane(size_t start, size_t end, uint16_t width, int16_t baseline, Align align,
              uint16_t view) {
  Lane lane;
  lane.start = static_cast<uint16_t>(start);
  lane.length = static_cast<uint16_t>(end - start);
  lane.width = width;
  lane.baseline = baseline;
  lane.x = alignedX(align, width, view);
  return lane;
}

}  // namespace

bool fitsTwoLines(const FontMetrics& m, uint16_t viewHeight) {
  return 2 * (m.lineAscent + m.lineDescent) <= viewHeight;
}

int16_t singleLineBaseline(const FontMetrics& m, uint16_t viewHeight) {
  // Center ordinary text, then nudge so Latin-1/Romanian accents and commas
  // stay on the panel.
  int b = (viewHeight - (m.capAscent + m.capDescent)) / 2 + m.capAscent;
  const int lo = m.lineAscent;
  const int hi = viewHeight - m.lineDescent;
  if (lo <= hi) {
    if (b < lo) b = lo;
    if (b > hi) b = hi;
  }
  return static_cast<int16_t>(b);
}

void twoLineBaselines(const FontMetrics& m, uint16_t viewHeight, int16_t* first, int16_t* second) {
  const int slot = viewHeight / 2;
  int pad = (slot - (m.lineAscent + m.lineDescent)) / 2;
  if (pad < 0) pad = 0;
  *first = static_cast<int16_t>(pad + m.lineAscent);
  *second = static_cast<int16_t>(slot + pad + m.lineAscent);
}

uint16_t scrollGap(const FontMetrics& m) {
  const int g = (m.capAscent + m.capDescent) * 3 / 2;
  return static_cast<uint16_t>(g < 16 ? 16 : g);
}

Layout compute(const char* text, size_t len, int32_t firstBreak, const FontMetrics& m,
               const Options& opt, MeasureFn measure, void* ctx) {
  Layout l;
  l.gap = scrollGap(m);
  l.speedPxPerSec = opt.speedPxPerSec > 0 ? opt.speedPxPerSec : 1;
  l.pauseMs = kScrollPauseMs;
  const uint16_t view = opt.viewWidth;

  if (opt.twoLines && len > 0 && fitsTwoLines(m, opt.viewHeight)) {
    size_t s1 = 0, e1 = 0, s2 = 0, e2 = 0;
    uint16_t w1 = 0, w2 = 0;
    bool found = false;
    if (firstBreak >= 0 && static_cast<size_t>(firstBreak) < len) {
      e1 = static_cast<size_t>(firstBreak);
      s2 = e1 + 1;
      e2 = len;
      trimSpaces(text, &s1, &e1);
      trimSpaces(text, &s2, &e2);
      if (e1 > s1 && e2 > s2) {
        w1 = measure(text + s1, e1 - s1, ctx);
        w2 = measure(text + s2, e2 - s2, ctx);
        found = w1 <= view && w2 <= view;
      }
    } else if (firstBreak < 0 && measure(text, len, ctx) > view) {
      uint32_t best = UINT32_MAX;
      for (size_t i = 1; i + 1 < len; ++i) {
        if (text[i] != ' ') continue;
        size_t a0 = 0, a1 = i, b0 = i + 1, b1 = len;
        trimSpaces(text, &a0, &a1);
        trimSpaces(text, &b0, &b1);
        if (a1 <= a0 || b1 <= b0) continue;
        const uint16_t wa = measure(text + a0, a1 - a0, ctx);
        if (wa > view) break;  // later splits only make the first line wider
        const uint16_t wb = measure(text + b0, b1 - b0, ctx);
        if (wb > view) continue;
        const uint32_t score = wa > wb ? wa : wb;
        if (score < best) {
          best = score;
          s1 = a0, e1 = a1, s2 = b0, e2 = b1, w1 = wa, w2 = wb;
          found = true;
        }
      }
    }
    if (found) {
      int16_t b1, b2;
      twoLineBaselines(m, opt.viewHeight, &b1, &b2);
      l.laneCount = 2;
      l.lanes[0] = makeLane(s1, e1, w1, b1, opt.align, view);
      l.lanes[1] = makeLane(s2, e2, w2, b2, opt.align, view);
      return l;
    }
  }

  const uint16_t width = len > 0 ? measure(text, len, ctx) : 0;
  l.laneCount = 1;
  l.lanes[0] = makeLane(0, len, width, singleLineBaseline(m, opt.viewHeight), opt.align, view);
  l.lanes[0].scrolls = width > view && opt.scroll;
  return l;
}

uint32_t cycleMs(const Lane& lane, const Layout& l) {
  if (!lane.scrolls) return 0;
  return l.pauseMs + moveMs(lane, l);
}

uint32_t scrollOffset(const Lane& lane, const Layout& l, uint32_t elapsedMs) {
  if (!lane.scrolls) return 0;
  const uint32_t period = static_cast<uint32_t>(lane.width) + l.gap;
  const uint32_t t = elapsedMs % cycleMs(lane, l);
  if (t < l.pauseMs) return 0;
  const uint32_t off =
      static_cast<uint32_t>(static_cast<uint64_t>(t - l.pauseMs) * l.speedPxPerSec / 1000u);
  return off < period ? off : 0;  // offset == period looks exactly like offset 0
}

uint32_t msUntilNextStep(const Lane& lane, const Layout& l, uint32_t elapsedMs) {
  if (!lane.scrolls) return UINT32_MAX;
  const uint32_t cycle = cycleMs(lane, l);
  const uint32_t t = elapsedMs % cycle;
  if (t < l.pauseMs) return l.pauseMs - t;
  const uint32_t moved = t - l.pauseMs;
  const uint32_t off = static_cast<uint32_t>(static_cast<uint64_t>(moved) * l.speedPxPerSec / 1000u);
  const uint32_t nextAt =
      static_cast<uint32_t>((static_cast<uint64_t>(off + 1) * 1000u + l.speedPxPerSec - 1) / l.speedPxPerSec);
  uint32_t wait = nextAt - moved;
  if (wait > cycle - t) wait = cycle - t;
  return wait > 0 ? wait : 1;
}

}  // namespace layout
