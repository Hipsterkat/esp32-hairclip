# HTTP API

The clip serves its API on the access point at `http://192.168.4.1`. All
responses are JSON (`Content-Type: application/json`, `Cache-Control:
no-store`) except `GET /`. Every response has `"ok": true|false`; failures add
an `"error"` string meant for people.

## Conventions

* **POST bodies** are `application/x-www-form-urlencoded`, UTF-8.
* **POST requests must send `X-Hairclip: 1`.** Without it the server answers
  `403`. Browsers cannot add a custom header to a cross-site form or simple
  request without a CORS preflight, which this server never approves, so
  other websites cannot drive the clip.
* **Validation happens on the device.** Out-of-range values are rejected with
  `400`; nothing is silently clamped or truncated.
* **Activity:** every request except `GET /api/status` restarts the
  3-minute Wi-Fi timeout. The page polls status every 4 s without keeping the
  radio on, and sends `/api/keepalive` while you type or tap.
* **Status codes:** `200` OK, `400` invalid input, `403` missing header, `404`
  unknown path, `405` wrong method (with an `Allow` header), `409` preset list
  full, `500` flash write failed.

## GET /

The control page: one gzipped HTML file (about 29 KB) with all CSS, JavaScript
and preview font data inline. It is sent with a strict Content-Security-Policy:
`default-src 'none'`, the script and style allowed only by SHA-256 hash, and
`connect-src 'self'`. Clients must accept gzip (every browser does); otherwise
the server answers `406`.

## GET /api/status

Not counted as activity.

```json
{
  "ok": true,
  "firmware": "1.0.0",
  "built": "Oct 22 2026 21:07:13",
  "uptimeS": 412,
  "resetReason": "power-on",
  "brownouts": 0,
  "wifi": {"state": "on", "ssid": "Hairclip", "ip": "192.168.4.1", "clients": 1,
           "idleTimeoutS": 180, "autoOffInS": 151, "stopInS": 0},
  "oled": {"detected": true, "busIdleHigh": true, "address": "0x3C", "lines": 1,
           "scrolling": false, "textWidth": 68, "frames": 3},
  "system": {"chip": "ESP32-C3", "revision": 4, "cpuMHz": 80, "freeHeap": 181000,
             "minFreeHeap": 170000, "displayStackFree": 3000}
}
```

* `wifi.state` is `on`, `stopping` (inside the 3 s grace period) or `off`.
* `resetReason` is one of `power-on`, `external`, `software`, `panic`,
  `interrupt-watchdog`, `task-watchdog`, `watchdog`, `deep-sleep`, `brownout`,
  `sdio`, `usb`, `jtag`, `unknown`.
* `brownouts` counts brownout resets since the last factory reset (capped at
  255).
* `oled.detected` reports whether an SSD1306 answered on I²C at boot.
  `oled.busIdleHigh` is false if SDA or SCL read LOW before I²C started,
  which means no pull-ups or no OLED power.
* `task-watchdog` as `resetReason` means the firmware hung for 30 s and the
  watchdog restarted it.

The values above are illustrative.

## GET /api/settings

```json
{
  "ok": true,
  "message": "Hello!",
  "display": {"font": "large", "align": "center", "brightness": 60, "scroll": true,
              "speed": 5, "rotation": 0, "invert": false, "lines": 1},
  "unsaved": false,
  "presets": ["Bună dimineața!"],
  "unsupported": [],
  "limits": {"maxChars": 120, "maxBytes": 256, "maxPresets": 6, "speedMin": 1,
             "speedMax": 10, "brightnessMax": 100},
  "fonts": [{"id": "small", "label": "Small", "twoLines": true},
            {"id": "medium", "label": "Medium", "twoLines": false},
            {"id": "large", "label": "Large", "twoLines": false}]
}
```

* `unsaved` is true when live changes (`persist=0`) differ from what is stored.
* `unsupported` lists characters of the current message that are drawn as `?`.

## POST /api/message

| Field | Rule |
|---|---|
| `message` | UTF-8, after normalization at most 120 characters and 256 bytes; may be empty (blank display) |

Normalization, which is the same in the firmware and the page: CR/CRLF become
LF; tabs and Unicode spaces become spaces; control and zero-width characters
are removed; smart quotes, dashes, ellipsis, € and a few emoji become ASCII;
leading and trailing whitespace is trimmed.

```json
{"ok": true, "saved": true, "message": "Don't panic <3", "changed": true, "unsupported": []}
```

The message is applied and stored only if it differs from the current one.
`500` with `"saved": false` means the display shows the new text but flash
could not be written.

## POST /api/settings

Any subset of:

| Field | Values |
|---|---|
| `font` | `small`, `medium`, `large` |
| `align` | `left`, `center`, `right` |
| `brightness` | `0`–`100` (percent, sent as SSD1306 contrast 0–255) |
| `scroll` | `1`/`0` (or `true`/`false`) |
| `speed` | `1`–`10` (8 to 75 pixels per second) |
| `rotation` | `0` or `180` |
| `invert` | `1`/`0` |
| `lines` | `1` or `2` (two lines need the small font and text that fits) |
| `persist` | `1` (default) saves to flash; `0` applies live only |

```json
{"ok": true, "saved": true, "unsaved": false, "display": {"font": "small", "align": "center", "brightness": 60, "scroll": true, "speed": 5, "rotation": 0, "invert": false, "lines": 2}}
```

Only changed keys are written to flash.

## POST /api/presets

* `action=add&text=…`: the same rules as `message`, and it may not be empty.
  A duplicate is accepted without change. `409` when 6 presets exist.
* `action=delete&index=N`: `N` is a zero-based position in `presets`.

```json
{"ok": true, "presets": ["Bună dimineața!", "Love You"]}
```

## POST /api/keepalive

Restarts the inactivity timer. Answers `{"ok": true, "autoOffInS": 180}`.

## POST /api/wifi/off

Starts switching the access point off: it keeps serving for 3 s, so the page
can show a notice, then shuts down. Answers `{"ok": true, "stopInS": 3}`.

## Examples

```bash
curl -s http://192.168.4.1/api/status
```

```bash
curl -s -X POST -H 'X-Hairclip: 1' --data-urlencode 'message=Bună ziua!' http://192.168.4.1/api/message
```

```bash
curl -s -X POST -H 'X-Hairclip: 1' -d 'font=small&lines=2' http://192.168.4.1/api/settings
```
