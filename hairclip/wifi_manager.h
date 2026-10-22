// Soft access point lifecycle: starts and stops the radio and the web server
// according to WifiPolicy. All functions run on the Arduino loop task.
#pragma once

#include <stdint.h>

#include "wifi_policy.h"

namespace wifi {

void begin(uint32_t nowMs);  // boot: start the AP immediately
void loop(uint32_t nowMs);   // timers and HTTP; call from every loop()
void toggle(uint32_t nowMs); // BOOT long-press
void turnOn(uint32_t nowMs);
void turnOff(uint32_t nowMs, StopReason why);
void activity(uint32_t nowMs);  // user activity from the web UI

ApState state();
uint32_t autoOffInMs(uint32_t nowMs);
uint32_t stopInMs(uint32_t nowMs);
uint8_t clientCount();

}  // namespace wifi
