#include "wifi_policy.h"

ApAction WifiPolicy::turnOn(uint32_t nowMs) {
  const ApState before = state_;
  state_ = ApState::On;
  reason_ = StopReason::None;
  lastActivityMs_ = nowMs;
  return before == ApState::Off ? ApAction::Start : ApAction::None;
}

void WifiPolicy::turnOff(uint32_t nowMs, StopReason why) {
  if (state_ != ApState::On) return;
  state_ = ApState::Stopping;
  reason_ = why;
  stopStartMs_ = nowMs;
}

ApAction WifiPolicy::toggle(uint32_t nowMs, StopReason why) {
  if (state_ == ApState::On) {
    turnOff(nowMs, why);
    return ApAction::None;
  }
  return turnOn(nowMs);
}

void WifiPolicy::activity(uint32_t nowMs) {
  if (state_ == ApState::On) {
    lastActivityMs_ = nowMs;
  } else if (state_ == ApState::Stopping && reason_ == StopReason::Timeout) {
    turnOn(nowMs);
  }
}

ApAction WifiPolicy::tick(uint32_t nowMs) {
  if (state_ == ApState::On && static_cast<uint32_t>(nowMs - lastActivityMs_) >= idleTimeoutMs_) {
    turnOff(nowMs, StopReason::Timeout);
  }
  if (state_ == ApState::Stopping && static_cast<uint32_t>(nowMs - stopStartMs_) >= graceMs_) {
    state_ = ApState::Off;
    return ApAction::Stop;
  }
  return ApAction::None;
}

void WifiPolicy::startFailed() {
  state_ = ApState::Off;
  reason_ = StopReason::None;
}

uint32_t WifiPolicy::autoOffInMs(uint32_t nowMs) const {
  if (state_ != ApState::On) return 0;
  const uint32_t idle = nowMs - lastActivityMs_;
  return idle >= idleTimeoutMs_ ? 0 : idleTimeoutMs_ - idle;
}

uint32_t WifiPolicy::stopInMs(uint32_t nowMs) const {
  if (state_ != ApState::Stopping) return 0;
  const uint32_t elapsed = nowMs - stopStartMs_;
  return elapsed >= graceMs_ ? 0 : graceMs_ - elapsed;
}
