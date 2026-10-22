#include "../../hairclip/button.h"
#include "test.h"

namespace {

// Feeds a level for durationMs in 10 ms steps (the firmware's sample period).
// Returns the number of long-press events and the time of the last one.
struct Sim {
  LongPressButton button{30, 1500};
  uint32_t now;
  int events = 0;
  uint32_t lastEvent = 0;

  explicit Sim(uint32_t start = 1000) : now(start) {}

  void hold(bool pressed, uint32_t durationMs) {
    for (uint32_t t = 0; t < durationMs; t += 10) {
      if (button.update(pressed, now)) {
        ++events;
        lastEvent = now;
      }
      now += 10;
    }
  }
};

}  // namespace

TEST(button_idle_never_fires) {
  Sim s;
  s.hold(false, 10000);
  CHECK_EQ(s.events, 0);
}

TEST(button_long_press_fires_once_per_hold) {
  Sim s;
  s.hold(false, 100);
  const uint32_t pressedAt = s.now;
  s.hold(true, 5000);
  CHECK_EQ(s.events, 1);
  CHECK(s.lastEvent >= pressedAt + 1500 && s.lastEvent <= pressedAt + 1510);
  s.hold(false, 200);
  s.hold(true, 1600);
  CHECK_EQ(s.events, 2);  // release and press again: second toggle
}

TEST(button_short_press_is_ignored) {
  Sim s;
  s.hold(false, 100);
  s.hold(true, 1400);
  s.hold(false, 500);
  CHECK_EQ(s.events, 0);
}

TEST(button_debounces_contact_bounce) {
  Sim s;
  s.hold(false, 100);
  for (int i = 0; i < 3; ++i) {  // bouncing for 60 ms
    s.hold(true, 10);
    s.hold(false, 10);
  }
  CHECK(!s.button.pressed());
  const uint32_t settledAt = s.now;
  s.hold(true, 1600);
  CHECK_EQ(s.events, 1);
  CHECK(s.lastEvent >= settledAt + 1500);
}

TEST(button_brief_release_glitch_does_not_restart_hold) {
  Sim s;
  s.hold(false, 100);
  s.hold(true, 800);
  s.hold(false, 10);  // shorter than the debounce time
  s.hold(true, 800);
  CHECK_EQ(s.events, 1);
}

TEST(button_held_through_boot_needs_a_release) {
  Sim s;
  s.hold(true, 5000);  // held from power-on (e.g. after a download-mode reset)
  CHECK_EQ(s.events, 0);
  s.hold(false, 100);
  s.hold(true, 1600);
  CHECK_EQ(s.events, 1);
}

TEST(button_millis_rollover) {
  Sim s(0xFFFFFFFFu - 800);
  s.hold(false, 100);
  s.hold(true, 1600);  // the hold crosses the 32-bit wrap
  CHECK_EQ(s.events, 1);
}
