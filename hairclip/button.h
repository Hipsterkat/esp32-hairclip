// Debounced long-press detector for the BOOT button (GPIO9).
// Pure C++: no Arduino dependencies, compiled by the host tests as well.
#pragma once

#include <stdint.h>

class LongPressButton {
 public:
  LongPressButton(uint32_t debounceMs, uint32_t longPressMs)
      : debounceMs_(debounceMs), longPressMs_(longPressMs) {}

  // Feed the raw level (true = pressed) and the current time. Returns true
  // exactly once per hold, when the debounced press has lasted longPressMs.
  // The detector starts disarmed: it fires only after it has seen the button
  // released, so a button held through reset or boot never triggers.
  // Time comparisons are unsigned differences, so millis() rollover is safe.
  bool update(bool rawPressed, uint32_t nowMs);

  bool pressed() const { return stable_; }

 private:
  uint32_t debounceMs_;
  uint32_t longPressMs_;
  bool lastRaw_ = true;
  bool stable_ = true;  // assume "held" until a stable release is seen
  bool armed_ = false;
  bool fired_ = false;
  uint32_t lastChangeMs_ = 0;
  uint32_t pressStartMs_ = 0;
};
