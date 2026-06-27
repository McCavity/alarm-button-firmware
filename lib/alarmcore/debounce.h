#pragma once
#include <cstdint>

// Pure software debounce: feed a raw reading + monotonic millis; update() returns true
// exactly on a confirmed state change (raw held steady for >= stableMs). Host-testable.
namespace alarmcore {

class Debouncer {
public:
  explicit Debouncer(uint32_t stableMs = 5) : stableMs_(stableMs) {}

  bool update(bool raw, uint32_t nowMs) {
    if (raw != candidate_) { candidate_ = raw; lastChange_ = nowMs; }
    if (candidate_ != stable_ && (nowMs - lastChange_) >= stableMs_) {
      stable_ = candidate_;
      return true;
    }
    return false;
  }

  bool state() const { return stable_; }

private:
  uint32_t stableMs_;
  bool stable_ = false;
  bool candidate_ = false;
  uint32_t lastChange_ = 0;
};

} // namespace alarmcore
