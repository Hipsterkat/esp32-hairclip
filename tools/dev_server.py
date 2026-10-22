#!/usr/bin/env python3
"""Serve the control page locally against a mock of the firmware's API.

For working on web/ without hardware: http://127.0.0.1:8080. The page is the
exact one the firmware embeds (tools/generate_assets.py), sent with the same
Content-Security-Policy. Text normalization runs through the firmware's own
C++ code (tests/host/parity_dump.cpp, compiled on first use); everything else
is a simplified in-memory mock, NOT the device.

  python3 tools/dev_server.py [--port 8080]
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import generate_assets as gen  # noqa: E402

DUMP = ROOT / "build" / "host" / "parity_dump"
IDLE_S = 180


def firmware_normalize(text: str) -> dict:
    if not DUMP.exists():
        DUMP.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["c++", "-std=c++17", "-O1", "-I", str(ROOT / "hairclip"), "-o", str(DUMP),
                        str(ROOT / "tests/host/parity_dump.cpp"), str(ROOT / "hairclip/text.cpp"),
                        str(ROOT / "hairclip/layout.cpp"), str(ROOT / "hairclip/settings.cpp")], check=True)
    out = subprocess.run([str(DUMP)], input=f"N {text.encode().hex() or '-'}\n", capture_output=True, text=True, check=True)
    r = json.loads(out.stdout)
    r["text"] = bytes.fromhex(r["out"]).decode("utf-8")
    return r


def undisplayable(text: str) -> list[str]:
    seen = []
    for ch in text:
        cp = ord(ch)
        ok = cp == 10 or 0x20 <= cp <= 0x7E or 0xA0 <= cp <= 0x17E or 0x218 <= cp <= 0x21B
        if not ok and ch not in seen:
            seen.append(ch)
    return seen[:8]


class Device:
    def __init__(self):
        self.message = "Hello!"
        self.display = {"font": "large", "align": "center", "brightness": 60, "scroll": True, "speed": 5,
                        "rotation": 0, "invert": False, "lines": 1}
        self.stored = dict(self.display)
        self.presets: list[str] = []
        self.started = time.time()
        self.activity = time.time()
        self.stop_at = None

    def wifi_state(self) -> str:
        now = time.time()
        if self.stop_at is not None:
            return "stopping" if now < self.stop_at else "off"
        return "on"


DEVICE = Device()
VALID = {
    "font": lambda v: v in ("small", "medium", "large"),
    "align": lambda v: v in ("left", "center", "right"),
    "brightness": lambda v: v.isdigit() and int(v) <= 100,
    "scroll": lambda v: v in ("0", "1", "true", "false"),
    "speed": lambda v: v.isdigit() and 1 <= int(v) <= 10,
    "rotation": lambda v: v in ("0", "180"),
    "invert": lambda v: v in ("0", "1", "true", "false"),
    "lines": lambda v: v in ("1", "2"),
}


class Handler(BaseHTTPRequestHandler):
    page = csp = None

    def log_message(self, fmt, *args):  # quieter console
        sys.stderr.write("%s %s\n" % (self.command, self.path))

    def send_json(self, code: int, body: dict) -> None:
        data = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def form(self) -> dict:
        length = int(self.headers.get("Content-Length") or 0)
        return {k: v[0] for k, v in parse_qs(self.rfile.read(length).decode(), keep_blank_values=True).items()}

    def do_GET(self):
        path = urlparse(self.path).path
        d = DEVICE
        if path == "/":
            d.activity = time.time()
            body = self.page.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Security-Policy", self.csp)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif path == "/api/status":
            if d.wifi_state() == "off":
                self.close_connection = True
                return
            left = max(0, int(IDLE_S - (time.time() - d.activity)))
            self.send_json(200, {"ok": True, "firmware": "1.0.0 (dev server mock)", "built": "mock",
                                 "uptimeS": int(time.time() - d.started), "resetReason": "power-on", "brownouts": 0,
                                 "wifi": {"state": d.wifi_state(), "ssid": "Hairclip", "ip": "192.168.4.1", "clients": 1,
                                          "idleTimeoutS": IDLE_S, "autoOffInS": left,
                                          "stopInS": max(0, int((d.stop_at or 0) - time.time()))},
                                 "oled": {"detected": True, "busIdleHigh": True, "address": "0x3C", "lines": 1,
                                          "scrolling": False, "textWidth": 0, "frames": 0},
                                 "system": {"chip": "mock", "revision": 0, "cpuMHz": 80, "freeHeap": 0,
                                            "minFreeHeap": 0, "displayStackFree": 0}})
        elif path == "/api/settings":
            d.activity = time.time()
            self.send_json(200, {"ok": True, "message": d.message, "display": d.display,
                                 "unsaved": d.display != d.stored, "presets": d.presets,
                                 "unsupported": undisplayable(d.message),
                                 "limits": {"maxChars": 120, "maxBytes": 256, "maxPresets": 6, "speedMin": 1,
                                            "speedMax": 10, "brightnessMax": 100},
                                 "fonts": [{"id": "small", "label": "Small", "twoLines": True},
                                           {"id": "medium", "label": "Medium", "twoLines": False},
                                           {"id": "large", "label": "Large", "twoLines": False}]})
        else:
            self.send_json(404, {"ok": False, "error": "Not found."})

    def do_POST(self):
        path = urlparse(self.path).path
        d = DEVICE
        if self.headers.get("X-Hairclip") != "1":
            self.send_json(403, {"ok": False, "error": "Missing X-Hairclip header."})
            return
        f = self.form()
        if path != "/api/wifi/off":
            d.activity = time.time()
        if path == "/api/message":
            r = firmware_normalize(f.get("message", ""))
            if r["status"] != "ok":
                self.send_json(400, {"ok": False, "error": "The text is too long (max 120 characters, 256 bytes)."
                                     if r["status"] == "toolong" else "The text is not valid UTF-8."})
                return
            d.message = r["text"]
            self.send_json(200, {"ok": True, "saved": True, "message": d.message, "changed": r["changed"],
                                 "unsupported": undisplayable(d.message)})
        elif path == "/api/settings":
            persist = f.pop("persist", "1") in ("1", "true")
            for k, v in f.items():
                if k not in VALID or not VALID[k](v):
                    self.send_json(400, {"ok": False, "error": f"Invalid value: {k}."})
                    return
            for k, v in f.items():
                d.display[k] = (v in ("1", "true")) if k in ("scroll", "invert") else (v if k in ("font", "align") else int(v))
            if persist:
                d.stored = dict(d.display)
            self.send_json(200, {"ok": True, "saved": persist, "unsaved": d.display != d.stored, "display": d.display})
        elif path == "/api/presets":
            if f.get("action") == "add":
                r = firmware_normalize(f.get("text", ""))
                if r["status"] != "ok" or not r["text"]:
                    self.send_json(400, {"ok": False, "error": "Invalid preset."})
                    return
                if r["text"] not in d.presets:
                    if len(d.presets) >= 6:
                        self.send_json(409, {"ok": False, "error": "The preset list is full (max 6)."})
                        return
                    d.presets.append(r["text"])
            elif f.get("action") == "delete" and f.get("index", "").isdigit() and int(f["index"]) < len(d.presets):
                d.presets.pop(int(f["index"]))
            else:
                self.send_json(400, {"ok": False, "error": "Invalid preset index."})
                return
            self.send_json(200, {"ok": True, "presets": d.presets})
        elif path == "/api/keepalive":
            self.send_json(200, {"ok": True, "autoOffInS": IDLE_S})
        elif path == "/api/wifi/off":
            d.stop_at = time.time() + 3
            self.send_json(200, {"ok": True, "stopInS": 3})
        else:
            self.send_json(404, {"ok": False, "error": "Not found."})


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8080)
    args = ap.parse_args()
    fonts, _ = gen.load_fonts()
    Handler.page, Handler.csp = gen.build_page(fonts)
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"Mock hairclip UI on http://127.0.0.1:{args.port} (Ctrl-C to stop)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
