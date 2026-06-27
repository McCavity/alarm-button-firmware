#pragma once
#include <cstdint>

// Pure quadrature decoder. Feed current A/B levels each poll; returns a completed detent
// step (-1/0/+1). Uses the standard Gray-code transition table and accumulates sub-steps
// until a full detent (stepsPerDetent sub-steps) is reached. If the bench shows nav doubled
// or halved, adjust stepsPerDetent; if reversed, swap A/B at the call site.
namespace alarmcore {

class QuadratureDecoder {
public:
  explicit QuadratureDecoder(int stepsPerDetent = 4) : perDetent_(stepsPerDetent) {}

  int update(bool a, bool b) {
    static const int8_t TBL[16] = {
      0, -1, +1, 0,
      +1, 0, 0, -1,
      -1, 0, 0, +1,
      0, +1, -1, 0,
    };
    uint8_t cur = (uint8_t)((a ? 2 : 0) | (b ? 1 : 0));
    accum_ += TBL[(prev_ << 2) | cur];
    prev_ = cur;
    if (accum_ >= perDetent_)  { accum_ = 0; return +1; }
    if (accum_ <= -perDetent_) { accum_ = 0; return -1; }
    return 0;
  }

private:
  int perDetent_;
  uint8_t prev_ = 0;   // last 2-bit state (A<<1 | B)
  int accum_ = 0;
};

} // namespace alarmcore
