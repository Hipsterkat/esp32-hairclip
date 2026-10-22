#include "button.h"

bool LongPressButton::update(bool rawPressed, uint32_t nowMs) {
  if (rawPressed != lastRaw_) {
    lastRaw_ = rawPressed;
    lastChangeMs_ = nowMs;
  }
  if (rawPressed != stable_ && static_cast<uint32_t>(nowMs - lastChangeMs_) >= debounceMs_) {
    stable_ = rawPressed;
    if (stable_) {
      pressStartMs_ = lastChangeMs_;
      fired_ = false;
    } else {
      armed_ = true;
    }
  }
  if (stable_ && armed_ && !fired_ &&
      static_cast<uint32_t>(nowMs - pressStartMs_) >= longPressMs_) {
    fired_ = true;
    return true;
  }
  return false;
}
