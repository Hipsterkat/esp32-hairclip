"""Which characters the firmware can draw, checked against the U8g2 font data.

The documented character support (README "Supported characters") is only
claimed because these tests find a glyph for every listed code point in each
font the firmware compiles in.
"""

from __future__ import annotations

import re
import unittest

from common import FIRMWARE

import generate_assets as gen
from u8g2font import U8g2Font, find_u8g2_fonts_c, load_font, render

ROMANIAN = "ĂăÂâÎîȘșȚțŞşŢţ"


class FontCoverage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = find_u8g2_fonts_c()
        cls.fonts = {fid: U8g2Font(load_font(name, path)) for fid, _, name in gen.FONTS}

    def test_decoder_reads_every_glyph(self):
        for fid, f in self.fonts.items():
            declared = int(f.comment["Glyphs"].split("/")[0])
            self.assertEqual(len(f.codepoints()), declared, fid)
            self.assertEqual(len(f.codepoints()) & 0xFF, f.glyph_cnt, fid)

    def test_every_displayable_character_has_a_glyph(self):
        for fid, f in self.fonts.items():
            missing = [f"U+{cp:04X}" for cp in gen.displayable() if not f.has(cp)]
            self.assertEqual(missing, [], fid)

    def test_romanian_letters_render_with_their_marks(self):
        for fid, f in self.fonts.items():
            for ch in ROMANIAN:
                g = f.glyph(ord(ch))
                base = f.glyph(ord({"Ă": "A", "ă": "a", "Â": "A", "â": "a", "Î": "I", "î": "i"}.get(ch, "S" if ch in "ȘŞ" else "s" if ch in "șş" else "T" if ch in "ȚŢ" else "t")))
                ink = sum(map(sum, g.rows))
                with self.subTest(font=fid, char=ch):
                    self.assertTrue(any(map(any, g.rows)))
                    self.assertGreater(ink, sum(map(sum, base.rows)), "accent/comma adds pixels")

    def test_long_s_is_absent_so_it_is_not_claimed(self):
        for f in self.fonts.values():
            self.assertFalse(f.has(0x17F))

    def test_generated_metrics_match_font_data(self):
        header = (FIRMWARE / "generated_fonts.h").read_text()
        rows = re.findall(r"\{([-\d, ]+)\},\s*//\s*(\w+):", header)
        self.assertEqual(len(rows), 3)
        for values, fid in rows:
            m = gen.metrics(self.fonts[fid])
            expected = [m[k] for k in ("capAscent", "capDescent", "lineAscent", "lineDescent", "maxAscent", "maxDescent")]
            self.assertEqual([int(v) for v in values.split(",")], expected, fid)

    def test_text_fits_the_32_pixel_panel(self):
        for fid, f in self.fonts.items():
            m = gen.metrics(f)
            self.assertLessEqual(m["lineAscent"] + m["lineDescent"], 32, fid)

    def test_render_hello(self):
        f = self.fonts["small"]
        canvas = render(f, [ord(c) for c in "Hello!"], 128, 32, 0, 19)
        lit = sum(map(sum, canvas))
        self.assertGreater(lit, 40)
        self.assertTrue(all(not any(row) for row in canvas[:8]))  # nothing above the text box


if __name__ == "__main__":
    unittest.main()
