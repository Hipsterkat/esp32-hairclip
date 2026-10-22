#!/usr/bin/env python3
"""Read U8g2 font data directly from the installed library's u8g2_fonts.c.

This is a small re-implementation of the glyph decoder in U8g2's
clib/u8g2_font.c (u8g2_read_font_info, u8g2_font_get_glyph_data,
u8g2_font_decode_glyph, u8g2_string_width). It lets the host-side tools work
with the *same bytes* the firmware compiles in:

  * tools/build_web_assets.py embeds the glyphs into the browser preview
  * tests/python/test_fonts.py verifies which characters each font can draw

Usage:
  python3 tools/u8g2font.py info   u8g2_font_helvB12_te
  python3 tools/u8g2font.py render u8g2_font_helvB12_te "Bună ziua"
"""

from __future__ import annotations

import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

HEADER_SIZE = 23  # U8G2_FONT_DATA_STRUCT_SIZE


def find_u8g2_fonts_c(explicit: str | None = None) -> Path:
    """Locate clib/u8g2_fonts.c of the installed U8g2 Arduino library."""
    candidates = []
    if explicit:
        candidates.append(Path(explicit))
    if os.environ.get("U8G2_FONTS_C"):
        candidates.append(Path(os.environ["U8G2_FONTS_C"]))
    home = Path.home()
    for user_dir in (home / "Documents" / "Arduino", home / "Arduino"):
        candidates.append(user_dir / "libraries" / "U8g2" / "src" / "clib" / "u8g2_fonts.c")
    for c in candidates:
        if c.is_file():
            return c
    raise FileNotFoundError(
        "u8g2_fonts.c not found. Install U8g2 (scripts/setup-macos.sh) or set U8G2_FONTS_C."
    )


_ESCAPES = {"n": 10, "t": 9, "r": 13, "a": 7, "b": 8, "f": 12, "v": 11,
            "\\": 92, '"': 34, "'": 39, "?": 63}

# One C string literal, e.g. "\23\0\3;x" (font data may contain ';' and '"').
_LITERAL = re.compile(r'\s*"((?:[^"\\]|\\.)*)"', re.S)


def _string_literals(text: str, pos: int) -> tuple[list[str], int]:
    """Collect adjacent string literals starting at pos, up to the closing ';'."""
    lits = []
    while True:
        m = _LITERAL.match(text, pos)
        if not m:
            break
        lits.append(m.group(1))
        pos = m.end()
    end = re.compile(r"\s*;").match(text, pos)
    if not end:
        raise ValueError(f"expected ';' after font data at offset {pos}")
    return lits, end.end()


def _decode_c_string(lits: list[str]) -> bytes:
    """Decode the concatenated C string literals of a U8g2 font array."""
    out = bytearray()
    for lit in lits:
        i = 0
        while i < len(lit):
            ch = lit[i]
            if ch != "\\":
                out.append(ord(ch))
                i += 1
                continue
            nxt = lit[i + 1]
            if nxt in "01234567":
                m = re.match(r"[0-7]{1,3}", lit[i + 1:])
                out.append(int(m.group(0), 8) & 0xFF)
                i += 1 + len(m.group(0))
            elif nxt == "x":
                m = re.match(r"[0-9a-fA-F]+", lit[i + 2:])
                out.append(int(m.group(0), 16) & 0xFF)
                i += 2 + len(m.group(0))
            else:
                out.append(_ESCAPES[nxt])
                i += 2
    return bytes(out)


@dataclass
class FontSource:
    name: str
    data: bytes
    comment: dict = field(default_factory=dict)


def load_font(name: str, fonts_c: Path | None = None) -> FontSource:
    path = fonts_c or find_u8g2_fonts_c()
    text = path.read_text(encoding="latin-1")
    m = re.search(r"const uint8_t " + re.escape(name) + r"\[(\d+)\][^=]*=", text)
    if not m:
        raise KeyError(f"font {name} not found in {path}")
    size = int(m.group(1))
    lits, _ = _string_literals(text, m.end())
    data = _decode_c_string(lits)
    # The declared array size includes the implicit string terminator.
    if len(data) + 1 != size:
        raise ValueError(f"{name}: decoded {len(data)} bytes, declared [{size}]")
    data += b"\0"
    comment = {}
    cstart = text.rfind("/*", 0, m.start())
    if cstart != -1:
        for line in text[cstart:m.start()].splitlines():
            if ":" in line:
                k, v = line.strip().split(":", 1)
                comment[k.strip()] = v.strip()
    return FontSource(name, data, comment)


def _int8(v: int) -> int:
    v &= 0xFF
    return v - 256 if v >= 128 else v


class _Bits:
    """Mirror of u8g2_font_decode_get_(un)signed_bits (LSB-first bit stream)."""

    def __init__(self, data: bytes, pos: int):
        self.data, self.pos, self.bit = data, pos, 0

    def u(self, cnt: int) -> int:
        val = self.data[self.pos] >> self.bit
        total = self.bit + cnt
        if total >= 8:
            s = 8 - self.bit
            self.pos += 1
            val = (val | (self.data[self.pos] << s)) & 0xFF
            total -= 8
        self.bit = total
        return val & ((1 << cnt) - 1)

    def s(self, cnt: int) -> int:
        return _int8(_int8(self.u(cnt)) - (1 << (cnt - 1)))


@dataclass
class Glyph:
    encoding: int
    w: int
    h: int
    x: int   # x offset of the bitmap from the pen position
    y: int   # bitmap bottom relative to the baseline (negative = descends)
    dx: int  # pen advance
    rows: list  # h rows of w booleans
    record: bytes  # raw glyph record bytes (encoding + size + bit data)


class U8g2Font:
    def __init__(self, src: FontSource):
        self.name = src.name
        self.data = d = src.data
        self.comment = src.comment
        self.glyph_cnt = d[0]
        self.bbx_mode = d[1]
        self.bits_per_0, self.bits_per_1 = d[2], d[3]
        self.bits_w, self.bits_h, self.bits_x, self.bits_y, self.bits_dx = d[4:9]
        self.max_char_width, self.max_char_height = d[9], d[10]
        self.x_offset, self.y_offset = _int8(d[11]), _int8(d[12])
        self.ascent_A, self.descent_g = _int8(d[13]), _int8(d[14])
        self.ascent_para, self.descent_para = _int8(d[15]), _int8(d[16])
        self.start_upper_A = (d[17] << 8) | d[18]
        self.start_lower_a = (d[19] << 8) | d[20]
        self.start_unicode = (d[21] << 8) | d[22]
        self._records: dict[int, tuple[int, bytes]] = {}
        self._cache: dict[int, Glyph] = {}
        self._index()

    def _index(self) -> None:
        d = self.data
        p = HEADER_SIZE
        while d[p + 1] != 0:  # 8-bit section: [enc][size][bits]...
            size = d[p + 1]
            self._records[d[p]] = (p + 2, d[p:p + size])
            p += size
        # Unicode section: lookup table of (offset, last encoding) pairs,
        # terminated by encoding 0xffff, then [enc hi][enc lo][size][bits]...
        table = HEADER_SIZE + self.start_unicode
        p = table + ((d[table] << 8) | d[table + 1])
        while True:
            enc = (d[p] << 8) | d[p + 1]
            if enc == 0:
                break
            size = d[p + 2]
            self._records[enc] = (p + 3, d[p:p + size])
            p += size

    def codepoints(self) -> list[int]:
        return sorted(self._records)

    def has(self, cp: int) -> bool:
        return cp in self._records

    def glyph_bits(self, cp: int) -> bytes:
        """The glyph's encoded bit stream (record without its 2/3-byte header)."""
        rec = self._records[cp][1]
        return rec[2:] if cp < 256 else rec[3:]

    def glyph(self, cp: int) -> Glyph | None:
        if cp in self._cache:
            return self._cache[cp]
        rec = self._records.get(cp)
        if rec is None:
            return None
        b = _Bits(self.data, rec[0])
        w, h = b.u(self.bits_w), b.u(self.bits_h)
        x, y, dx = b.s(self.bits_x), b.s(self.bits_y), b.s(self.bits_dx)
        rows = [[False] * w for _ in range(h)]
        if w > 0:
            pos = [0, 0]  # local x, y

            def run(length: int, fg: bool) -> None:
                cnt, lx, ly = length, pos[0], pos[1]
                while True:
                    rem = w - lx
                    cur = min(cnt, rem)
                    if fg and ly < h:
                        for i in range(cur):
                            rows[ly][lx + i] = True
                    if cnt < rem:
                        break
                    cnt -= rem
                    lx, ly = 0, ly + 1
                pos[0], pos[1] = lx + cnt, ly

            while True:
                a, c = b.u(self.bits_per_0), b.u(self.bits_per_1)
                while True:
                    run(a, False)
                    run(c, True)
                    if b.u(1) == 0:
                        break
                if pos[1] >= h:
                    break
        g = Glyph(cp, w, h, x, y, dx, rows, rec[1])
        self._cache[cp] = g
        return g

    def string_width(self, cps) -> int:
        """Mirror of u8g2_string_width() with U8G2_BALANCED_STR_WIDTH_CALCULATION."""
        w = dx = 0
        glyph_w = 0
        x_off = 0
        initial_x_offset = None
        for cp in cps:
            g = self.glyph(cp)
            if g is None:
                dx = 0
                continue
            dx, glyph_w, x_off = g.dx, g.w, g.x
            if initial_x_offset is None:
                initial_x_offset = g.x
            w += dx
        if glyph_w != 0:
            w = w - dx + glyph_w + x_off
            if initial_x_offset is not None and initial_x_offset > 0:
                w += initial_x_offset
        return w & 0xFFFF

    def extents(self, cps) -> tuple[int, int]:
        """Max pixels above and below the baseline over the given characters."""
        asc = desc = 0
        for cp in cps:
            g = self.glyph(cp)
            if g is None or g.w == 0 or g.h == 0:
                continue
            asc = max(asc, g.h + g.y)
            desc = max(desc, -g.y)
        return asc, desc


def render(font: U8g2Font, cps, width: int, height: int, x: int, baseline: int, canvas=None):
    """Draw like u8g2_DrawUTF8 at (x, baseline) into a width x height canvas."""
    canvas = canvas or [[False] * width for _ in range(height)]
    for cp in cps:
        g = font.glyph(cp)
        if g is None:
            continue
        top = baseline - (g.h + g.y)
        for ry, row in enumerate(g.rows):
            py = top + ry
            if 0 <= py < height:
                for rx, on in enumerate(row):
                    px = x + g.x + rx
                    if on and 0 <= px < width:
                        canvas[py][px] = True
        x += g.dx
    return canvas


def ascii_art(canvas) -> str:
    return "\n".join("".join("#" if p else "." for p in row) for row in canvas)


ROMANIAN = "ĂăÂâÎîȘșȚțŞşŢţ"


def _main(argv: list[str]) -> int:
    if len(argv) < 3 or argv[1] not in ("info", "render"):
        print(__doc__)
        return 2
    font = U8g2Font(load_font(argv[2]))
    if argv[1] == "info":
        print(f"{font.name}: {len(font.codepoints())} glyphs ({font.comment.get('Glyphs', '?')})")
        print(f"  ascent_A={font.ascent_A} descent_g={font.descent_g} "
              f"max_char_height={font.max_char_height}")
        basic = [c for c in range(32, 127)]
        print(f"  ASCII extents (asc, desc): {font.extents(basic)}")
        ro = [ord(c) for c in ROMANIAN]
        print(f"  Romanian extents (asc, desc): {font.extents(ro)}")
        missing = [c for c in ROMANIAN if not font.has(ord(c))]
        print(f"  Romanian missing: {''.join(missing) or 'none'}")
        return 0
    text = argv[3] if len(argv) > 3 else "Hello!"
    cps = [ord(c) for c in text]
    asc, desc = font.extents(cps)
    width = max(font.string_width(cps), 1)
    print(ascii_art(render(font, cps, width, asc + desc, 0, asc)))
    print(f"width={width} asc={asc} desc={desc}")
    return 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv))
