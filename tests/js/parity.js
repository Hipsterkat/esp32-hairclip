// Runs web/preview.js on the cases in the JSON file given as argv[2] and
// prints the results as JSON (driven by tests/python/test_parity.py).
'use strict';

const fs = require('fs');
const path = require('path');
const { createPreview } = require(path.join(__dirname, '..', '..', 'web', 'preview.js'));

const data = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const preview = createPreview(data.fonts, data.charmap);
const FONT_IDS = ['small', 'medium', 'large'];
const PROBES = [0, 1199, 1200, 1300, 2500, 5000, 12345, 99999];
const toHex = (s) => Buffer.from(s, 'utf8').toString('hex');

const out = { normalize: [], layout: [], glyphs: [], widths: [] };

for (const hex of data.normalize) {
  const n = preview.normalize(Buffer.from(hex, 'hex').toString('utf8'), data.limits.maxChars, data.limits.maxBytes);
  out.normalize.push({ status: n.tooLong ? 'toolong' : 'ok', out: n.tooLong ? '' : toHex(n.text), changed: n.changed });
}

for (const c of data.layout) {
  const cps = Array.from(Buffer.from(c.hex, 'hex').toString('utf8'), (ch) => ch.codePointAt(0));
  const font = preview.fonts[FONT_IDS[c.font]];
  const opt = {
    align: c.align, scroll: !!c.scroll, twoLines: !!c.twoLines, speed: c.speed,
    measure: (part) => part.length * c.px,
  };
  const l = preview.compute(cps, c.firstBreak, font, opt);
  out.layout.push({
    gap: l.gap,
    lanes: l.lanes.map((ln) => ({ start: ln.start, end: ln.end, width: ln.width, baseline: ln.baseline, x: ln.x, scrolls: ln.scrolls })),
    offsets: PROBES.map((t) => preview.scrollOffset(l.lanes[0], l, t)),
  });
}

for (const g of data.glyphs) {
  const glyph = preview.glyph(preview.fonts[g.font], g.cp);
  const rows = [];
  for (let y = 0; y < glyph.h; y++) {
    let row = '';
    for (let x = 0; x < glyph.w; x++) row += glyph.px[y * glyph.w + x] ? '#' : '.';
    rows.push(row);
  }
  out.glyphs.push({ w: glyph.w, h: glyph.h, x: glyph.x, y: glyph.y, dx: glyph.dx, rows });
}

for (const w of data.widths) {
  out.widths.push(preview.stringWidth(preview.fonts[w.font], Array.from(w.text, (ch) => ch.codePointAt(0))));
}

process.stdout.write(JSON.stringify(out));
