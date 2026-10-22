#include "../../hairclip/wifi_policy.h"
#include "test.h"

namespace {
constexpr uint32_t kIdle = 180000;
constexpr uint32_t kGrace = 3000;
}

TEST(wifi_starts_on_and_times_out) {
  WifiPolicy p(kIdle, kGrace);
  CHECK(p.state() == ApState::Off);
  CHECK(p.turnOn(1000) == ApAction::Start);
  CHECK(p.state() == ApState::On);
  CHECK_EQ(p.autoOffInMs(1000), kIdle);
  CHECK(p.tick(1000 + kIdle - 1) == ApAction::None);
  CHECK(p.state() == ApState::On);
  CHECK(p.tick(1000 + kIdle) == ApAction::None);
  CHECK(p.state() == ApState::Stopping);
  CHECK(p.stopReason() == StopReason::Timeout);
  CHECK_EQ(p.stopInMs(1000 + kIdle), kGrace);
  CHECK(p.tick(1000 + kIdle + kGrace - 1) == ApAction::None);
  CHECK(p.tick(1000 + kIdle + kGrace) == ApAction::Stop);
  CHECK(p.state() == ApState::Off);
  CHECK_EQ(p.autoOffInMs(5000000), 0u);
}

TEST(wifi_activity_extends_the_timeout) {
  WifiPolicy p(kIdle, kGrace);
  p.turnOn(0);
  p.activity(100000);
  CHECK(p.tick(250000) == ApAction::None);
  CHECK(p.state() == ApState::On);
  CHECK_EQ(p.autoOffInMs(250000), 30000u);
  p.tick(280000);
  CHECK(p.state() == ApState::Stopping);
}

TEST(wifi_activity_cancels_only_a_timeout_stop) {
  WifiPolicy p(kIdle, kGrace);
  p.turnOn(0);
  p.tick(kIdle);
  CHECK(p.state() == ApState::Stopping);
  p.activity(kIdle + 1000);  // user tapped during the grace period
  CHECK(p.state() == ApState::On);

  p.turnOff(200000, StopReason::Web);  // user asked for off: activity must not undo it
  p.activity(200500);
  CHECK(p.state() == ApState::Stopping);
  CHECK(p.tick(200000 + kGrace) == ApAction::Stop);
}

TEST(wifi_button_toggle) {
  WifiPolicy p(kIdle, kGrace);
  CHECK(p.toggle(0, StopReason::Button) == ApAction::Start);  // off -> on
  CHECK(p.toggle(1000, StopReason::Button) == ApAction::None);  // on -> stopping
  CHECK(p.state() == ApState::Stopping);
  CHECK(p.toggle(2000, StopReason::Button) == ApAction::None);  // stopping -> on (cancel)
  CHECK(p.state() == ApState::On);
  CHECK(p.toggle(3000, StopReason::Button) == ApAction::None);
  CHECK(p.tick(3000 + kGrace) == ApAction::Stop);
  CHECK(p.toggle(9000, StopReason::Button) == ApAction::Start);  // off -> on again
}

TEST(wifi_turn_off_is_ignored_when_not_on) {
  WifiPolicy p(kIdle, kGrace);
  p.turnOff(0, StopReason::Web);
  CHECK(p.state() == ApState::Off);
  CHECK(p.tick(100) == ApAction::None);
}

TEST(wifi_start_failure_returns_to_off) {
  WifiPolicy p(kIdle, kGrace);
  CHECK(p.turnOn(0) == ApAction::Start);
  p.startFailed();
  CHECK(p.state() == ApState::Off);
  CHECK(p.turnOn(10) == ApAction::Start);  // can retry
}

TEST(wifi_timers_survive_millis_rollover) {
  WifiPolicy p(kIdle, kGrace);
  const uint32_t start = 0xFFFFFF00u;
  p.turnOn(start);
  CHECK(p.tick(0x00010000u) == ApAction::None);  // 65792 ms later, after the wrap
  CHECK(p.state() == ApState::On);
  p.tick(start + kIdle);  // wraps around
  CHECK(p.state() == ApState::Stopping);
  CHECK(p.tick(start + kIdle + kGrace) == ApAction::Stop);
}
