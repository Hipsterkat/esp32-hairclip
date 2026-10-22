/*
 * Browser-side approximation of the hairclip's 128x32 OLED.
 *
 * Mirrors hairclip/text.cpp (normalization, displayable characters),
 * hairclip/layout.cpp (placement, two-line split, scrolling) and U8g2's glyph
 * decoder, using glyph data that tools/generate_assets.py extracts from the
 * exact U8g2 fonts compiled into the firmware. Timing, brightness and the look
 * of the pixels are approximations of the physical panel.
 */
'use strict';

function createPreview(fontData, charMap) {
  const VIEW_W = 128;
  const VIEW_H = 32;
  const PAUSE_MS = 1200;
  const SPEED_PX = [8, 12, 16, 20, 25, 30, 40, 50, 60, 75];
  const ICON = [0x40, 0x50, 0x54, 0x55, 0x55]; // 7x5 "radio on" icon, XBM rows
  const TWIN = { 0x218: 0x15e, 0x219: 0x15f, 0x21a: 0x162, 0x21b: 0x163, 0x15e: 0x218, 0x15f: 0x219, 0x162: 0x21a, 0x163: 0x21b };

  // ---- U8g2 font data -----------------------------------------------------

  function bytesFromBase64(b64) {
    const s = atob(b64);
    const out = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
    return out;
  }

  function parseFont(spec) {
    const d = bytesFromBase64(spec.data);
    const font = {
      d, bp0: d[2], bp1: d[3], bw: d[4], bh: d[5], bx: d[6], by: d[7], bdx: d[8],
      glyphs: new Map(), cache: new Map(), metrics: spec.metrics, label: spec.label,
    };
    // After the 23-byte U8g2 header: [code point hi][lo][n][n bytes of glyph bits]...
    let p = 23;
    while (p + 3 <= d.length) {
      font.glyphs.set((d[p] << 8) | d[p + 1], p + 3);
      p += 3 + d[p + 2];
    }
    return font;
  }

  const int8 = (v) => ((v & 0xff) >= 128 ? (v & 0xff) - 256 : v & 0xff);

  // Port of u8g2_font_decode_glyph(): run-length encoded, LSB-first bits.
  function glyph(font, cp) {
    if (font.cache.has(cp)) return font.cache.get(cp);
    const start = font.glyphs.get(cp);
    if (start === undefined) return null;
    const d = font.d;
    let pos = start;
    let bit = 0;
    const u = (cnt) => {
      let val = d[pos] >> bit;
      let total = bit + cnt;
      if (total >= 8) {
        const s = 8 - bit;
        pos++;
        val = (val | ((d[pos] || 0) << s)) & 0xff;
        total -= 8;
      }
      bit = total;
      return val & ((1 << cnt) - 1);
    };
    const s = (cnt) => int8(int8(u(cnt)) - (1 << (cnt - 1)));
    const w = u(font.bw);
    const h = u(font.bh);
    const x = s(font.bx);
    const y = s(font.by);
    const dx = s(font.bdx);
    const px = new Uint8Array(w * h);
    if (w > 0) {
      let lx = 0;
      let ly = 0;
      const run = (len, fg) => {
        let cnt = len;
        for (;;) {
          const rem = w - lx;
          const cur = Math.min(cnt, rem);
          if (fg && ly < h) px.fill(1, ly * w + lx, ly * w + lx + cur);
          if (cnt < rem) break;
          cnt -= rem;
          lx = 0;
          ly++;
        }
        lx += cnt;
      };
      for (;;) {
        const a = u(font.bp0);
        const b = u(font.bp1);
        do {
          run(a, false);
          run(b, true);
        } while (u(1) !== 0);
        if (ly >= h) break;
      }
    }
    const g = { w, h, x, y, dx, px };
    font.cache.set(cp, g);
    return g;
  }

  // Port of u8g2_string_width() with U8G2_BALANCED_STR_WIDTH_CALCULATION.
  function stringWidth(font, cps) {
    let w = 0;
    let dx = 0;
    let glyphW = 0;
    let xOff = 0;
    let initial = null;
    for (const cp of cps) {
      const g = glyph(font, cp);
      if (!g) {
        dx = 0;
        continue;
      }
      dx = g.dx;
      glyphW = g.w;
      xOff = g.x;
      if (initial === null) initial = g.x;
      w += dx;
    }
    if (glyphW !== 0) {
      w = w - dx + glyphW + xOff;
      if (initial !== null && initial > 0) w += initial;
    }
    return w & 0xffff;
  }

  const fonts = {};
  for (const id of Object.keys(fontData)) fonts[id] = parseFont(fontData[id]);

  // ---- Text policy (hairclip/text.cpp) --------------------------------------

  function lookup(cp) {
    for (const [first, last, repl] of charMap) if (cp >= first && cp <= last) return repl;
    return null;
  }
  const isControl = (cp) => cp < 0x20 || (cp >= 0x7f && cp <= 0x9f);
  const utf8Len = (cp) => (cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4);

  function normalize(input, maxChars, maxBytes) {
    const src = Array.from(input, (ch) => ch.codePointAt(0));
    const out = [];
    let started = false;
    let changed = false;
    const put = (cp) => {
      const ws = cp === 32 || cp === 10;
      if (ws && !started) return;
      out.push(cp);
      if (!ws) started = true;
    };
    for (let i = 0; i < src.length; i++) {
      let cp = src[i];
      if (cp >= 0xd800 && cp <= 0xdfff) cp = 0xfffd; // a lone surrogate is sent as U+FFFD
      if (cp === 13) {
        if (src[i + 1] !== 10) put(10);
      } else if (cp === 10) {
        put(10);
      } else {
        const repl = lookup(cp);
        if (repl !== null) {
          for (const ch of repl) put(ch.codePointAt(0));
          changed = true;
        } else if (isControl(cp)) {
          changed = true;
        } else {
          put(cp);
        }
      }
    }
    while (out.length && (out[out.length - 1] === 32 || out[out.length - 1] === 10)) out.pop();
    const bytes = out.reduce((n, cp) => n + utf8Len(cp), 0);
    return {
      cps: out,
      text: String.fromCodePoint(...out),
      chars: out.length,
      bytes,
      changed,
      tooLong: out.length > maxChars || bytes > maxBytes,
    };
  }

  const isDisplayable = (cp) =>
    (cp >= 0x20 && cp <= 0x7e) || (cp >= 0xa0 && cp <= 0x17e) || (cp >= 0x218 && cp <= 0x21b);

  function displayCodepoint(font, cp) {
    if (isDisplayable(cp) && font.glyphs.has(cp)) return cp;
    const twin = TWIN[cp];
    if (twin && font.glyphs.has(twin)) return twin;
    return 63; // '?'
  }

  // ---- Layout (hairclip/layout.cpp) ---------------------------------------

  const fitsTwoLines = (m) => 2 * (m.lineAscent + m.lineDescent) <= VIEW_H;

  function singleBaseline(m) {
    let b = Math.trunc((VIEW_H - (m.capAscent + m.capDescent)) / 2) + m.capAscent;
    const lo = m.lineAscent;
    const hi = VIEW_H - m.lineDescent;
    if (lo <= hi) b = Math.min(Math.max(b, lo), hi);
    return b;
  }

  function twoBaselines(m) {
    const slot = VIEW_H / 2;
    const pad = Math.max(0, Math.trunc((slot - (m.lineAscent + m.lineDescent)) / 2));
    return [pad + m.lineAscent, slot + pad + m.lineAscent];
  }

  function scrollGap(m) {
    const g = Math.trunc(((m.capAscent + m.capDescent) * 3) / 2);
    return g < 16 ? 16 : g;
  }

  function alignedX(align, w) {
    if (w >= VIEW_W || align === 'left') return 0;
    if (align === 'right') return VIEW_W - w;
    return Math.trunc((VIEW_W - w) / 2);
  }

  function trim(cps, s, e) {
    while (s < e && cps[s] === 32) s++;
    while (e > s && cps[e - 1] === 32) e--;
    return [s, e];
  }

  // cps: display code points (no line breaks); firstBreak: index of the
  // user's first line break or -1. opt: {align, scroll, twoLines, speed} and,
  // for tests, an optional measure(cps) replacing the font's string width.
  function compute(cps, firstBreak, font, opt) {
    const m = font.metrics;
    const width = opt.measure || ((part) => stringWidth(font, part));
    const measure = (s, e) => width(cps.slice(s, e));
    const lane = (s, e, w, baseline) => ({ start: s, end: e, width: w, baseline, x: alignedX(opt.align, w), scrolls: false });
    const l = { lanes: [], gap: scrollGap(m), speed: SPEED_PX[opt.speed - 1] || 25, pause: PAUSE_MS };
    const len = cps.length;
    if (opt.twoLines && len > 0 && fitsTwoLines(m)) {
      let best = null;
      if (firstBreak >= 0 && firstBreak < len) {
        const [s1, e1] = trim(cps, 0, firstBreak);
        const [s2, e2] = trim(cps, firstBreak + 1, len);
        if (e1 > s1 && e2 > s2) {
          const w1 = measure(s1, e1);
          const w2 = measure(s2, e2);
          if (w1 <= VIEW_W && w2 <= VIEW_W) best = [s1, e1, w1, s2, e2, w2];
        }
      } else if (firstBreak < 0 && measure(0, len) > VIEW_W) {
        let bestScore = Infinity;
        for (let i = 1; i + 1 < len; i++) {
          if (cps[i] !== 32) continue;
          const [a0, a1] = trim(cps, 0, i);
          const [b0, b1] = trim(cps, i + 1, len);
          if (a1 <= a0 || b1 <= b0) continue;
          const wa = measure(a0, a1);
          if (wa > VIEW_W) break;
          const wb = measure(b0, b1);
          if (wb > VIEW_W) continue;
          const score = Math.max(wa, wb);
          if (score < bestScore) {
            bestScore = score;
            best = [a0, a1, wa, b0, b1, wb];
          }
        }
      }
      if (best) {
        const [b1, b2] = twoBaselines(m);
        l.lanes.push(lane(best[0], best[1], best[2], b1), lane(best[3], best[4], best[5], b2));
        return l;
      }
    }
    const w = len ? measure(0, len) : 0;
    const only = lane(0, len, w, singleBaseline(m));
    only.scrolls = w > VIEW_W && opt.scroll;
    l.lanes.push(only);
    return l;
  }

  function cycleMs(lane, l) {
    return l.pause + Math.ceil(((lane.width + l.gap) * 1000) / l.speed);
  }

  function scrollOffset(lane, l, elapsed) {
    if (!lane.scrolls) return 0;
    const period = lane.width + l.gap;
    const t = Math.floor(elapsed) % cycleMs(lane, l);
    if (t < l.pause) return 0;
    const off = Math.floor(((t - l.pause) * l.speed) / 1000);
    return off < period ? off : 0;
  }

  // ---- Frames -------------------------------------------------------------

  function drawText(buf, font, cps, x, baseline) {
    for (const cp of cps) {
      const g = glyph(font, cp);
      if (!g) continue;
      const top = baseline - (g.h + g.y);
      for (let ry = 0; ry < g.h; ry++) {
        const py = top + ry;
        if (py < 0 || py >= VIEW_H) continue;
        for (let rx = 0; rx < g.w; rx++) {
          const px = x + g.x + rx;
          if (g.px[ry * g.w + rx] && px >= 0 && px < VIEW_W) buf[py * VIEW_W + px] = 1;
        }
      }
      x += g.dx;
    }
  }

  // Everything needed to animate one message with one set of settings.
  function makeScene(message, display, wifiOn) {
    const font = fonts[display.font] || fonts.large;
    const src = Array.from(message, (ch) => ch.codePointAt(0));
    let firstBreak = -1;
    const cps = src.map((cp, i) => {
      if (cp === 10) {
        if (firstBreak < 0) firstBreak = i;
        return 32;
      }
      return displayCodepoint(font, cp);
    });
    const opt = { align: display.align, scroll: display.scroll, twoLines: display.lines === 2, speed: display.speed };
    const layout = compute(cps, firstBreak, font, opt);
    return {
      font, layout, wifiOn,
      lanes: layout.lanes.map((ln) => cps.slice(ln.start, ln.end)),
      animated: layout.lanes.length === 1 && layout.lanes[0].scrolls,
    };
  }

  // Returns {pixels: Uint8Array(128*32), key} for the scene at elapsed ms.
  function frame(scene, elapsed) {
    const buf = new Uint8Array(VIEW_W * VIEW_H);
    let key = 0;
    scene.layout.lanes.forEach((ln, i) => {
      if (!ln.scrolls) {
        drawText(buf, scene.font, scene.lanes[i], ln.x, ln.baseline);
        return;
      }
      const off = scrollOffset(ln, scene.layout, elapsed);
      key = off;
      drawText(buf, scene.font, scene.lanes[i], -off, ln.baseline);
      const x2 = -off + ln.width + scene.layout.gap;
      if (x2 < VIEW_W) drawText(buf, scene.font, scene.lanes[i], x2, ln.baseline);
    });
    if (scene.wifiOn) {
      for (let y = 0; y <= ICON.length; y++) for (let x = VIEW_W - 8; x < VIEW_W; x++) buf[y * VIEW_W + x] = 0;
      ICON.forEach((row, y) => {
        for (let b = 0; b < 7; b++) if (row & (1 << b)) buf[y * VIEW_W + VIEW_W - 7 + b] = 1;
      });
    }
    return { pixels: buf, key };
  }

  return {
    VIEW_W, VIEW_H, fonts, glyph, stringWidth, normalize, isDisplayable, displayCodepoint,
    fitsTwoLines, compute, cycleMs, scrollOffset, makeScene, frame,
  };
}

if (typeof module !== 'undefined') module.exports = { createPreview };
