// Local HTTP control interface (http://192.168.4.1): the embedded page and a
// small JSON API. Documented in docs/API.md. Runs on the Arduino loop task.
#pragma once

#include "settings.h"
#include "storage.h"

// Firmware state shared by the web handlers, owned by hairclip.ino.
struct AppState {
  char message[MESSAGE_MAX_BYTES + 1];
  DisplaySettings display;        // what the panel shows now
  DisplaySettings storedDisplay;  // what is saved in flash
  Presets presets;
  uint8_t brownouts = 0;
  const char* resetReason = "unknown";
};

namespace web {

void begin(AppState& state, SettingsStore& store);  // registers the routes
void start();                                       // AP is up: listen on port 80
void stop();
void handleClient();

}  // namespace web
