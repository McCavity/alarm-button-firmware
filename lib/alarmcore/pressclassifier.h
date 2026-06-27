#pragma once
#include <cstdint>

// Pure long-press / short-click discriminator (design §3: long-press >= 1500 ms = mute,
// short click = detail toggle). Feed an already-debounced pressed flag + monotonic millis.
namespace alarmcore {

enum class PressEvent { NONE, SHORT_CLICK, LONG_PRESS };

class PressClassifier {
public:
  explicit PressClassifier(uint32_t longMs = 1500) : longMs_(longMs) {}

  PressEvent update(bool pressed, uint32_t nowMs) {
    if (pressed && !wasPressed_) {            // press starts
      wasPressed_ = true; pressStart_ = nowMs; longFired_ = false;
    } else if (pressed && wasPressed_) {      // held
      if (!longFired_ && (nowMs - pressStart_) >= longMs_) {
        longFired_ = true;
        return PressEvent::LONG_PRESS;
      }
    } else if (!pressed && wasPressed_) {     // released
      wasPressed_ = false;
      if (!longFired_) return PressEvent::SHORT_CLICK;
    }
    return PressEvent::NONE;
  }

private:
  uint32_t longMs_;
  bool wasPressed_ = false;
  bool longFired_ = false;
  uint32_t pressStart_ = 0;
};

} // namespace alarmcore
