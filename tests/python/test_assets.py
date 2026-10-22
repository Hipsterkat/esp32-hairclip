"""The web page embedded in the firmware: up to date, offline, locked down."""

from __future__ import annotations

import base64
import gzip
import hashlib
import re
import subprocess
import sys
import unittest

from common import FIRMWARE, ROOT, WEB

import generate_assets as gen


def header_bytes() -> bytes:
    text = (FIRMWARE / "generated_web.h").read_text()
    body = text[text.index("WEB_INDEX_GZ[] = {"):]
    return bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", body))


class WebAssets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fonts, _ = gen.load_fonts()
        cls.html, cls.csp = gen.build_page(fonts)

    def test_committed_headers_are_current(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/generate_assets.py"), "--check"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_embedded_gzip_is_the_page(self):
        data = header_bytes()
        length = int(re.search(r"WEB_INDEX_GZ_LEN = (\d+);", (FIRMWARE / "generated_web.h").read_text()).group(1))
        self.assertEqual(len(data), length)
        self.assertEqual(gzip.decompress(data).decode("utf-8"), self.html)

    def test_page_needs_no_network(self):
        for pattern in (r"https?://", r"<script[^>]+src=", r"<link[^>]+stylesheet", r"@import", r"url\("):
            self.assertIsNone(re.search(pattern, self.html, re.I), pattern)

    def test_csp_hashes_match_inline_code(self):
        script = re.search(r"<script>(.*)</script>", self.html, re.S).group(1)
        style = re.search(r"<style>(.*?)</style>", self.html, re.S).group(1)

        def sha(s: str) -> str:
            return base64.b64encode(hashlib.sha256(s.encode()).digest()).decode()

        self.assertIn(f"script-src 'sha256-{sha(script)}'", self.csp)
        self.assertIn(f"style-src 'sha256-{sha(style)}'", self.csp)
        self.assertIn("default-src 'none'", self.csp)
        self.assertIn("connect-src 'self'", self.csp)
        self.assertNotIn("unsafe", self.csp)
        self.assertEqual(self.html.count("<script>"), 1)
        self.assertNotRegex(self.html, r"\son[a-z]+=", "inline event handlers would be blocked by the CSP")
        self.assertNotRegex(self.html, r"\sstyle=", "inline style attributes would be blocked by the CSP")

    def test_scripts_never_inject_html(self):
        for name in ("app.js", "preview.js"):
            js = (WEB / name).read_text()
            for api in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval(", "new Function"):
                self.assertNotIn(api, js, f"{name} uses {api}")

    def test_preview_is_labelled_approximate(self):
        self.assertIn("Approximate browser preview", self.html)

    def test_no_invented_battery_level(self):
        page = self.html.lower()
        self.assertIn("battery", page)
        self.assertIn("not measured", page)
        self.assertNotRegex(page, r"battery[^<]{0,40}\d+\s*%")

    def test_char_map_replacements_never_grow_text(self):
        for first, last, repl in gen.charmap():
            self.assertTrue(repl.isascii(), hex(first))
            self.assertLessEqual(first, last)
            shortest = len(chr(first).encode("utf-8"))
            self.assertLessEqual(len(repl.encode()), shortest, f"U+{first:04X} -> {repl!r}")

    def test_page_size_budget(self):
        self.assertLess(len(header_bytes()), 48 * 1024, "keep the embedded page small")


if __name__ == "__main__":
    unittest.main()
