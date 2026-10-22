"""The browser preview (web/preview.js) must follow the firmware's rules.

Runs the same inputs through the C++ code (tests/host/parity_dump.cpp) and the
JavaScript preview (tests/js/parity.js) and compares the results. Glyph bitmaps
and string widths decoded in JavaScript are compared with tools/u8g2font.py.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from common import BUILD, FIRMWARE, ROOT, compile_cpp

import generate_assets as gen
from u8g2font import U8g2Font, find_u8g2_fonts_c, load_font

NORMALIZE_CASES = [
    "Hello!", "  Bună ziua, Ștefan!  ", "Don’t “panic” – ok…", "I ❤️ you 🙂", "a\r\nb\rc\nd",
    "tab\there", "a​b﻿", "a\x01b\x7fc", "x" * 120, "x" * 121, "🚀" * 64, "🚀" * 65,
    " " * 300 + "Hi", "a" + " " * 300 + "b", "5 € and ™", "👍🏽", "", "\n\n", "Ăă Ââ Îî Șș Țț Şş Ţţ",
]

LAYOUT_CASES = [  # (font, align, scroll, twoLines, speed, px per char, firstBreak, text)
    (2, "center", 1, 0, 5, 6, -1, "Hello"),
    (2, "left", 1, 0, 5, 6, -1, "Hello"),
    (2, "right", 1, 0, 5, 6, -1, "Hello"),
    (2, "center", 1, 0, 5, 1, -1, "x" * 128),
    (2, "center", 1, 0, 5, 1, -1, "x" * 129),
    (2, "center", 0, 0, 5, 1, -1, "x" * 129),
    (1, "center", 1, 0, 10, 7, -1, "A longer message that has to scroll across the panel"),
    (0, "center", 1, 1, 3, 8, -1, "aaaa bbbb cccc dddd eeee"),
    (0, "left", 1, 1, 3, 6, 2, "Hi  There"),
    (0, "center", 1, 1, 3, 6, 2, "Hi " + "x" * 30),
    (0, "center", 1, 1, 3, 6, -1, "w" * 40 + " end"),
    (0, "center", 1, 1, 3, 6, -1, "short"),
    (2, "center", 1, 1, 3, 8, -1, "aaaa bbbb cccc dddd eeee"),
    (0, "center", 1, 0, 1, 5, -1, ""),
]

GLYPHS = [(f, cp) for f in ("small", "medium", "large") for cp in map(ord, "AgȘțĂ?â~ Ů")]
WIDTHS = [(f, t) for f in ("small", "medium", "large") for t in ("Hello!", "Be Happy :)", "Bună ziua, Ștefan!", "ș", " ")]


@unittest.skipUnless(shutil.which("node"), "node is not installed")
class PreviewParity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        dump = compile_cpp(BUILD / "parity_dump", [
            ROOT / "tests/host/parity_dump.cpp", FIRMWARE / "text.cpp", FIRMWARE / "layout.cpp",
            FIRMWARE / "settings.cpp",
        ])
        lines = [f"N {s.encode().hex() or '-'}" for s in NORMALIZE_CASES]
        lines += [f"L {f} {a} {s} {t} {sp} {px} {b} {txt.encode().hex() or '-'}"
                  for f, a, s, t, sp, px, b, txt in LAYOUT_CASES]
        out = subprocess.run([str(dump)], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True)
        results = [json.loads(line) for line in out.stdout.splitlines()]
        cls.cpp_normalize = results[:len(NORMALIZE_CASES)]
        cls.cpp_layout = results[len(NORMALIZE_CASES):]

        fonts, _ = gen.load_fonts()
        cls.fonts = {fid: f for (fid, _, _), f in zip(gen.FONTS, fonts)}
        data = {
            "fonts": {fid: {"label": label, "metrics": gen.metrics(f), "data": gen.font_blob(f)}
                      for (fid, label, _), f in zip(gen.FONTS, fonts)},
            "charmap": gen.charmap(),
            "limits": {"maxChars": 120, "maxBytes": 256},
            "normalize": [s.encode().hex() for s in NORMALIZE_CASES],
            "layout": [{"font": f, "align": a, "scroll": s, "twoLines": t, "speed": sp, "px": px,
                        "firstBreak": b, "hex": txt.encode().hex()} for f, a, s, t, sp, px, b, txt in LAYOUT_CASES],
            "glyphs": [{"font": f, "cp": cp} for f, cp in GLYPHS],
            "widths": [{"font": f, "text": t} for f, t in WIDTHS],
        }
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "cases.json"
            path.write_text(json.dumps(data))
            out = subprocess.run(["node", str(ROOT / "tests/js/parity.js"), str(path)],
                                 capture_output=True, text=True, check=True)
        cls.js = json.loads(out.stdout)

    def test_normalization_matches_firmware(self):
        for case, cpp, js in zip(NORMALIZE_CASES, self.cpp_normalize, self.js["normalize"]):
            with self.subTest(case=case[:40]):
                self.assertEqual(cpp, js)

    def test_layout_and_scrolling_match_firmware(self):
        for case, cpp, js in zip(LAYOUT_CASES, self.cpp_layout, self.js["layout"]):
            with self.subTest(case=case):
                self.assertEqual(cpp, js)

    def test_glyph_decoding_matches_reference(self):
        for (fid, cp), js in zip(GLYPHS, self.js["glyphs"]):
            g = self.fonts[fid].glyph(cp)
            rows = ["".join("#" if p else "." for p in row) for row in g.rows]
            with self.subTest(font=fid, char=chr(cp)):
                self.assertEqual({"w": g.w, "h": g.h, "x": g.x, "y": g.y, "dx": g.dx, "rows": rows}, js)

    def test_string_widths_match_reference(self):
        for (fid, text), js in zip(WIDTHS, self.js["widths"]):
            with self.subTest(font=fid, text=text):
                self.assertEqual(self.fonts[fid].string_width([ord(c) for c in text]), js)


if __name__ == "__main__":
    unittest.main()
