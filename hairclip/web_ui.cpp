#include "web_ui.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_timer.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "display.h"
#include "generated_fonts.h"
#include "generated_web.h"
#include "json_writer.h"
#include "layout.h"
#include "text.h"
#include "wifi_manager.h"

namespace web {

namespace {

WebServer g_server(80);
AppState* g_state = nullptr;
SettingsStore* g_store = nullptr;
bool g_running = false;

// One response at a time (WebServer is single-threaded). Sized for the worst
// case: seven texts of 120 characters that each escape to 6 bytes.
char g_out[8192];

const char* kCollectedHeaders[] = {"X-Hairclip", "Accept-Encoding"};

struct Route {
  const char* path;
  const char* allow;
};
const Route kRoutes[] = {
    {"/", "GET"},
    {"/api/status", "GET"},
    {"/api/settings", "GET, POST"},
    {"/api/message", "POST"},
    {"/api/presets", "POST"},
    {"/api/keepalive", "POST"},
    {"/api/wifi/off", "POST"},
};

void sendJson(int code, const text::StrBuf& body) {
  g_server.sendHeader("Cache-Control", "no-store");
  g_server.sendHeader("X-Content-Type-Options", "nosniff");
  g_server.send(code, "application/json", body.c_str());
}

void sendError(int code, const char* message) {
  text::StrBuf b(g_out, sizeof(g_out));
  JsonWriter j(b);
  j.beginObject().boolean("ok", false).str("error", message).endObject();
  sendJson(code, b);
}

void finish(int code, JsonWriter& j, const text::StrBuf& b) {
  if (j.ok()) {
    sendJson(code, b);
  } else {
    sendError(500, "Response too large.");
  }
}

// Mutating requests must carry "X-Hairclip: 1". Browsers will not attach a
// custom header to a cross-site form post or simple request without a CORS
// preflight, which this server never answers, so other sites cannot drive it.
bool checkPost() {
  if (g_server.header("X-Hairclip") != "1") {
    sendError(403, "Missing X-Hairclip header.");
    return false;
  }
  return true;
}

void markActivity() { wifi::activity(millis()); }

const char* apStateName(ApState s) {
  switch (s) {
    case ApState::On: return "on";
    case ApState::Stopping: return "stopping";
    default: return "off";
  }
}

uint32_t ceilSeconds(uint32_t ms) { return (ms + 999) / 1000; }

void writeDisplay(JsonWriter& j, const DisplaySettings& ds) {
  j.beginObject("display")
      .str("font", settings::fontName(ds.font))
      .str("align", settings::alignName(ds.align))
      .unum("brightness", ds.brightness)
      .boolean("scroll", ds.scroll)
      .unum("speed", ds.speed)
      .unum("rotation", ds.rotate180 ? 180 : 0)
      .boolean("invert", ds.invert)
      .unum("lines", ds.twoLines ? 2 : 1)
      .endObject();
}

void writePresets(JsonWriter& j, const Presets& p) {
  j.beginArray("presets");
  for (uint8_t i = 0; i < p.count; ++i) j.str(nullptr, p.items[i]);
  j.endArray();
}

void writeUndisplayable(JsonWriter& j, const char* msg) {
  uint32_t cps[8];
  const size_t n = text::collectUndisplayable(msg, strlen(msg), cps, 8);
  j.beginArray("unsupported");
  for (size_t k = 0; k < n; ++k) {
    char enc[4];
    j.str(nullptr, enc, text::utf8Encode(cps[k], enc));
  }
  j.endArray();
}

// Normalizes a text field. On failure sends a 400 and returns false.
bool readText(const char* field, char* out, size_t cap, text::NormalizeResult* result) {
  if (!g_server.hasArg(field)) {
    char msg[48];
    snprintf(msg, sizeof(msg), "Missing '%s'.", field);
    sendError(400, msg);
    return false;
  }
  const String raw = g_server.arg(field);
  *result = text::normalizeMessage(raw.c_str(), raw.length(), out, cap, MESSAGE_MAX_CHARS, MESSAGE_MAX_BYTES);
  if (result->status == text::NormalizeStatus::InvalidUtf8) {
    sendError(400, "The text is not valid UTF-8.");
    return false;
  }
  if (result->status == text::NormalizeStatus::TooLong) {
    char msg[80];
    snprintf(msg, sizeof(msg), "The text is too long (max %u characters, %u bytes).",
             static_cast<unsigned>(MESSAGE_MAX_CHARS), static_cast<unsigned>(MESSAGE_MAX_BYTES));
    sendError(400, msg);
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

void handleRoot() {
  markActivity();
  if (g_server.header("Accept-Encoding").indexOf("gzip") < 0) {
    g_server.send(406, "text/plain", "Open this page in a web browser (gzip support required).\n");
    return;
  }
  g_server.sendHeader("Content-Encoding", "gzip");
  g_server.sendHeader("Cache-Control", "no-cache");
  g_server.sendHeader("Content-Security-Policy", WEB_CSP);
  g_server.sendHeader("X-Content-Type-Options", "nosniff");
  g_server.sendHeader("Referrer-Policy", "no-referrer");
  g_server.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(WEB_INDEX_GZ),
                  WEB_INDEX_GZ_LEN);
}

// Polled by the page every few seconds. Deliberately NOT counted as activity:
// an idle open page must not keep the radio on forever.
void handleStatus() {
  const uint32_t now = millis();
  const display::Info d = display::info();
  char ip[16];
  snprintf(ip, sizeof(ip), "%u.%u.%u.%u", AP_IP[0], AP_IP[1], AP_IP[2], AP_IP[3]);
  char addr[8];
  snprintf(addr, sizeof(addr), "0x%02X", d.address);

  text::StrBuf b(g_out, sizeof(g_out));
  JsonWriter j(b);
  j.beginObject()
      .boolean("ok", true)
      .str("firmware", HAIRCLIP_FIRMWARE_VERSION)
      .str("built", __DATE__ " " __TIME__)
      .unum("uptimeS", static_cast<uint32_t>(esp_timer_get_time() / 1000000))
      .str("resetReason", g_state->resetReason)
      .unum("brownouts", g_state->brownouts);
  j.beginObject("wifi")
      .str("state", apStateName(wifi::state()))
      .str("ssid", HAIRCLIP_AP_SSID)
      .str("ip", ip)
      .unum("clients", wifi::clientCount())
      .unum("idleTimeoutS", WIFI_IDLE_TIMEOUT_MS / 1000)
      .unum("autoOffInS", ceilSeconds(wifi::autoOffInMs(now)))
      .unum("stopInS", ceilSeconds(wifi::stopInMs(now)))
      .endObject();
  j.beginObject("oled")
      .boolean("detected", d.detected)
      .str("address", addr)
      .unum("lines", d.lines)
      .boolean("scrolling", d.scrolling)
      .unum("textWidth", d.textWidth)
      .unum("frames", d.frames)
      .endObject();
  j.beginObject("system")
      .str("chip", ESP.getChipModel())
      .unum("revision", ESP.getChipRevision())
      .unum("cpuMHz", getCpuFrequencyMhz())
      .unum("freeHeap", ESP.getFreeHeap())
      .unum("minFreeHeap", ESP.getMinFreeHeap())
      .unum("displayStackFree", d.stackFree)
      .endObject();
  j.endObject();
  finish(200, j, b);
}

void handleGetSettings() {
  markActivity();
  text::StrBuf b(g_out, sizeof(g_out));
  JsonWriter j(b);
  j.beginObject().boolean("ok", true).str("message", g_state->message);
  writeDisplay(j, g_state->display);
  j.boolean("unsaved", g_state->display != g_state->storedDisplay);
  writePresets(j, g_state->presets);
  writeUndisplayable(j, g_state->message);
  j.beginObject("limits")
      .unum("maxChars", MESSAGE_MAX_CHARS)
      .unum("maxBytes", MESSAGE_MAX_BYTES)
      .unum("maxPresets", PRESET_MAX_COUNT)
      .unum("speedMin", settings::kSpeedMin)
      .unum("speedMax", settings::kSpeedMax)
      .unum("brightnessMax", settings::kBrightnessMax)
      .endObject();
  j.beginArray("fonts");
  for (uint8_t i = 0; i < FONT_COUNT; ++i) {
    j.beginObject()
        .str("id", settings::fontName(static_cast<FontSize>(i)))
        .str("label", kFontLabels[i])
        .boolean("twoLines", layout::fitsTwoLines(kFontMetrics[i], OLED_HEIGHT))
        .endObject();
  }
  j.endArray();
  j.endObject();
  finish(200, j, b);
}

void handleMessage() {
  if (!checkPost()) return;
  markActivity();
  static char norm[MESSAGE_MAX_BYTES + 1];
  text::NormalizeResult r;
  if (!readText("message", norm, sizeof(norm), &r)) return;

  bool saved = true;
  if (strcmp(norm, g_state->message) != 0) {
    saved = g_store->saveMessage(norm);
    memcpy(g_state->message, norm, r.bytes + 1);
    display::setContent(g_state->message, g_state->display);
  }
  text::StrBuf b(g_out, sizeof(g_out));
  JsonWriter j(b);
  j.beginObject().boolean("ok", saved).boolean("saved", saved).str("message", g_state->message);
  j.boolean("changed", r.changed);
  writeUndisplayable(j, g_state->message);
  if (!saved) j.str("error", "Shown on the display, but saving to flash failed: it will be lost on restart.");
  j.endObject();
  finish(saved ? 200 : 500, j, b);
}

void handleSettings() {
  if (!checkPost()) return;
  markActivity();
  DisplaySettings next = g_state->display;
  bool persist = true;
  int fields = 0;
  char msg[96];
  for (int i = 0; i < g_server.args(); ++i) {
    const String key = g_server.argName(i);
    const String value = g_server.arg(i);
    if (key == "persist") {
      if (!settings::parseBool(value.c_str(), &persist)) {
        sendError(400, "persist must be 1 or 0.");
        return;
      }
      continue;
    }
    if (!settings::isKnownField(key.c_str())) {
      snprintf(msg, sizeof(msg), "Unknown setting '%.24s'.", key.c_str());
      sendError(400, msg);
      return;
    }
    if (const char* err = settings::applyField(&next, key.c_str(), value.c_str())) {
      snprintf(msg, sizeof(msg), "Invalid value: %s.", err);
      sendError(400, msg);
      return;
    }
    ++fields;
  }
  if (fields == 0) {
    sendError(400, "No settings given.");
    return;
  }
  if (next != g_state->display) {
    g_state->display = next;
    display::setContent(g_state->message, g_state->display);
  }
  bool saved = true;
  if (persist && g_state->storedDisplay != g_state->display) {
    saved = g_store->saveDisplay(g_state->storedDisplay, g_state->display);
    if (saved) g_state->storedDisplay = g_state->display;
  }
  text::StrBuf b(g_out, sizeof(g_out));
  JsonWriter j(b);
  j.beginObject().boolean("ok", saved).boolean("saved", persist && saved);
  j.boolean("unsaved", g_state->display != g_state->storedDisplay);
  writeDisplay(j, g_state->display);
  if (!saved) j.str("error", "Applied, but saving to flash failed: it will be lost on restart.");
  j.endObject();
  finish(saved ? 200 : 500, j, b);
}

void handlePresets() {
  if (!checkPost()) return;
  markActivity();
  static Presets next;
  next = g_state->presets;
  const String action = g_server.arg("action");
  if (action == "add") {
    static char norm[MESSAGE_MAX_BYTES + 1];
    text::NormalizeResult r;
    if (!readText("text", norm, sizeof(norm), &r)) return;
    if (r.bytes == 0) {
      sendError(400, "A preset cannot be empty.");
      return;
    }
    if (next.find(norm) < 0 && !next.add(norm)) {
      char msg[48];
      snprintf(msg, sizeof(msg), "The preset list is full (max %u).", static_cast<unsigned>(PRESET_MAX_COUNT));
      sendError(409, msg);
      return;
    }
  } else if (action == "delete") {
    uint32_t index;
    if (!settings::parseUint(g_server.arg("index").c_str(), 255, &index) ||
        !next.remove(static_cast<uint8_t>(index))) {
      sendError(400, "Invalid preset index.");
      return;
    }
  } else {
    sendError(400, "action must be add or delete.");
    return;
  }
  bool same = next.count == g_state->presets.count;
  for (uint8_t i = 0; same && i < next.count; ++i) same = strcmp(next.items[i], g_state->presets.items[i]) == 0;
  bool saved = true;
  if (!same) {
    saved = g_store->savePresets(next);
    if (saved) g_state->presets = next;
  }
  text::StrBuf b(g_out, sizeof(g_out));
  JsonWriter j(b);
  j.beginObject().boolean("ok", saved);
  writePresets(j, g_state->presets);
  if (!saved) j.str("error", "Saving the presets to flash failed.");
  j.endObject();
  finish(saved ? 200 : 500, j, b);
}

void handleKeepalive() {
  if (!checkPost()) return;
  markActivity();
  text::StrBuf b(g_out, sizeof(g_out));
  JsonWriter j(b);
  j.beginObject().boolean("ok", true).unum("autoOffInS", ceilSeconds(wifi::autoOffInMs(millis()))).endObject();
  finish(200, j, b);
}

void handleWifiOff() {
  if (!checkPost()) return;
  const uint32_t now = millis();
  wifi::turnOff(now, StopReason::Web);
  text::StrBuf b(g_out, sizeof(g_out));
  JsonWriter j(b);
  j.beginObject().boolean("ok", true).unum("stopInS", ceilSeconds(wifi::stopInMs(now))).endObject();
  finish(200, j, b);
}

void handleNotFound() {
  const String uri = g_server.uri();
  for (const Route& r : kRoutes) {
    if (uri == r.path) {
      g_server.sendHeader("Allow", r.allow);
      sendError(405, "Method not allowed.");
      return;
    }
  }
  sendError(404, "Not found.");
}

}  // namespace

void begin(AppState& state, SettingsStore& store) {
  g_state = &state;
  g_store = &store;
  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/api/status", HTTP_GET, handleStatus);
  g_server.on("/api/settings", HTTP_GET, handleGetSettings);
  g_server.on("/api/settings", HTTP_POST, handleSettings);
  g_server.on("/api/message", HTTP_POST, handleMessage);
  g_server.on("/api/presets", HTTP_POST, handlePresets);
  g_server.on("/api/keepalive", HTTP_POST, handleKeepalive);
  g_server.on("/api/wifi/off", HTTP_POST, handleWifiOff);
  g_server.onNotFound(handleNotFound);
  g_server.collectHeaders(kCollectedHeaders, sizeof(kCollectedHeaders) / sizeof(kCollectedHeaders[0]));
}

void start() {
  if (g_running) return;
  g_server.begin();
  g_running = true;
}

void stop() {
  if (!g_running) return;
  g_server.stop();
  g_running = false;
}

void handleClient() {
  if (g_running) g_server.handleClient();
}

}  // namespace web
