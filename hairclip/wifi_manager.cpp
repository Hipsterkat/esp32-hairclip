#include "wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "display.h"
#include "web_ui.h"

namespace wifi {

namespace {

WifiPolicy g_policy(WIFI_IDLE_TIMEOUT_MS, WIFI_STOP_GRACE_MS);
bool g_radioOn = false;
char g_ip[16];

bool startRadio() {
  WiFi.persistent(false);  // keep Wi-Fi config in RAM: no flash writes per start
  const IPAddress ip(AP_IP[0], AP_IP[1], AP_IP[2], AP_IP[3]);
  const IPAddress mask(AP_NETMASK[0], AP_NETMASK[1], AP_NETMASK[2], AP_NETMASK[3]);
  if (!WiFi.mode(WIFI_AP) || !WiFi.softAPConfig(ip, ip, mask) ||
      !WiFi.softAP(HAIRCLIP_AP_SSID, HAIRCLIP_AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CLIENTS)) {
    WiFi.mode(WIFI_OFF);
    return false;
  }
  WiFi.setTxPower(static_cast<wifi_power_t>(AP_TX_POWER_QDBM));
  web::start();
  return true;
}

void stopRadio() {
  web::stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

void apply(ApAction action) {
  if (action == ApAction::Start && !g_radioOn) {
    g_radioOn = startRadio();
    if (!g_radioOn) {
      g_policy.startFailed();
      Serial.println("[wifi] access point failed to start");
      display::showToast("Wi-Fi failed", nullptr, 2500);
    } else {
      Serial.printf("[wifi] access point \"%s\" up at http://%s\n", HAIRCLIP_AP_SSID, g_ip);
    }
  } else if (action == ApAction::Stop && g_radioOn) {
    stopRadio();
    g_radioOn = false;
    Serial.println("[wifi] access point off");
  }
  display::setWifiState(g_policy.state());
}

}  // namespace

void begin(uint32_t nowMs) {
  snprintf(g_ip, sizeof(g_ip), "%u.%u.%u.%u", AP_IP[0], AP_IP[1], AP_IP[2], AP_IP[3]);
  apply(g_policy.turnOn(nowMs));
}

void loop(uint32_t nowMs) {
  const ApState before = g_policy.state();
  apply(g_policy.tick(nowMs));
  if (g_policy.state() != before) display::setWifiState(g_policy.state());
  if (g_radioOn) web::handleClient();
}

void toggle(uint32_t nowMs) {
  const ApAction action = g_policy.toggle(nowMs, StopReason::Button);
  if (g_policy.state() == ApState::Stopping) {
    display::showToast("Wi-Fi OFF", nullptr, 1500);
  } else {
    display::showToast("Wi-Fi ON", g_ip, 3000);
  }
  apply(action);
}

void turnOn(uint32_t nowMs) { apply(g_policy.turnOn(nowMs)); }

void turnOff(uint32_t nowMs, StopReason why) {
  g_policy.turnOff(nowMs, why);
  if (why == StopReason::Web) display::showToast("Wi-Fi OFF", "hold BOOT: on", 2500);
  display::setWifiState(g_policy.state());
}

void activity(uint32_t nowMs) { g_policy.activity(nowMs); }

ApState state() { return g_policy.state(); }
uint32_t autoOffInMs(uint32_t nowMs) { return g_policy.autoOffInMs(nowMs); }
uint32_t stopInMs(uint32_t nowMs) { return g_policy.stopInMs(nowMs); }

uint8_t clientCount() { return g_radioOn ? WiFi.softAPgetStationNum() : 0; }

}  // namespace wifi
