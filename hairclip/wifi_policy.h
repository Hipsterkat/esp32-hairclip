// When the Wi-Fi access point should be on: boot, BOOT long-press, the web
// "turn off" button and the inactivity timeout.
// Pure C++: no Arduino dependencies, compiled by the host tests as well.
#pragma once

#include <stdint.h>

enum class ApState : uint8_t { Off, On, Stopping };
enum class ApAction : uint8_t { None, Start, Stop };
enum class StopReason : uint8_t { None, Button, Web, Timeout, Console };

class WifiPolicy {
 public:
  WifiPolicy(uint32_t idleTimeoutMs, uint32_t graceMs)
      : idleTimeoutMs_(idleTimeoutMs), graceMs_(graceMs) {}

  // Off -> On (returns Start). Stopping -> On (cancels the stop).
  ApAction turnOn(uint32_t nowMs);
  // On -> Stopping. The AP keeps serving for the grace period, then tick()
  // returns Stop.
  void turnOff(uint32_t nowMs, StopReason why);
  // BOOT long-press: On -> Stopping, Stopping -> On, Off -> On.
  ApAction toggle(uint32_t nowMs, StopReason why);
  // User activity from the web UI. Restarts the idle timer; also cancels a
  // stop that the idle timeout started.
  void activity(uint32_t nowMs);
  // Advances the timers; returns Stop when the AP must be shut down now.
  ApAction tick(uint32_t nowMs);
  // The radio could not be started: back to Off.
  void startFailed();

  ApState state() const { return state_; }
  StopReason stopReason() const { return reason_; }
  uint32_t idleTimeoutMs() const { return idleTimeoutMs_; }
  // Time left before the idle timeout (0 unless On).
  uint32_t autoOffInMs(uint32_t nowMs) const;
  // Time left in the grace period (0 unless Stopping).
  uint32_t stopInMs(uint32_t nowMs) const;

 private:
  uint32_t idleTimeoutMs_;
  uint32_t graceMs_;
  ApState state_ = ApState::Off;
  StopReason reason_ = StopReason::None;
  uint32_t lastActivityMs_ = 0;
  uint32_t stopStartMs_ = 0;
};
