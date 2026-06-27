# AxiometaHAL + core UI state machine — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the alarm button render a (canned) alarm list on the LCD and react to encoder + button input on real hardware, wired through a small host-tested core. No MQTT yet.

**Architecture:** Pure, host-tested logic in `lib/alarmcore/` (debounce, quadrature, press-classify, and an `AppCore` UI state machine wrapping the existing `computeView()`); ESP32-only hardware code in `src/` (`AxiometaHAL` implements the abstract `AlarmButtonHAL`, `main.cpp` is a canned-data demo). Tasks 1–4 are TDD with `pio test -e native`; tasks 5–6 are bench-verified on the board.

**Tech Stack:** C++17, PlatformIO, Arduino-ESP32 core, Adafruit ST7735, ArduinoJson 7, Unity.

## Global Constraints

- `lib/alarmcore/` MUST compile natively — no `Arduino.h`, no ESP APIs (hardware lives only behind `AlarmButtonHAL`). Hardware code goes in `src/`.
- New core logic is driven native-test-first (`pio test -e native`) before any hardware code.
- All native tests live in the single Unity binary `test/test_alarmcore/test_alarmcore.cpp` and MUST be registered in its `main()` (`RUN_TEST(...)`).
- Confirmed pins (CLAUDE.md "Module layout"): LCD MOSI 12 / SCK 14 / CS 4 / DC 2 / RST 3 (ST7735 `INITR_MINI160x80`, `setRotation(3)`, `invertDisplay(false)`); buzzer 6; button 16 (active-low); button LED 15 (active-high); encoder A 17 / B 18 / push 1.
- Commit headers: English `type(scope): …`. Footer `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- Branch: `feat/phase0c-axiometa-hal` (already created).

---

### Task 1: Debouncer (pure)

**Files:**
- Create: `lib/alarmcore/debounce.h`
- Test: `test/test_alarmcore/test_alarmcore.cpp` (add tests + register)

**Interfaces:**
- Consumes: nothing.
- Produces: `class alarmcore::Debouncer { explicit Debouncer(uint32_t stableMs = 5); bool update(bool raw, uint32_t nowMs); bool state() const; }` — `update` returns `true` exactly when the debounced state flips; `state()` is the current stable value.

- [ ] **Step 1: Write the failing tests**

Add to `test/test_alarmcore/test_alarmcore.cpp` (after the includes, add `#include "debounce.h"`; place tests before `main()`):

```cpp
void test_debounce_stabilizes_after_window() {
  Debouncer d(5);
  TEST_ASSERT_FALSE(d.update(true, 0));    // raw high, but not yet stable
  TEST_ASSERT_FALSE(d.update(true, 4));    // 4 ms < 5 ms window
  TEST_ASSERT_TRUE(d.update(true, 5));     // 5 ms reached -> flips
  TEST_ASSERT_TRUE(d.state());
}

void test_debounce_rejects_bounce() {
  Debouncer d(5);
  d.update(true, 0);
  TEST_ASSERT_FALSE(d.update(false, 2));   // bounced back before window
  TEST_ASSERT_FALSE(d.update(false, 6));   // stable low == initial, no flip
  TEST_ASSERT_FALSE(d.state());
}
```

Register in `main()`:

```cpp
  RUN_TEST(test_debounce_stabilizes_after_window);
  RUN_TEST(test_debounce_rejects_bounce);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL (compile error: `debounce.h` not found / `Debouncer` undefined).

- [ ] **Step 3: Write the implementation**

Create `lib/alarmcore/debounce.h`:

```cpp
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
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `pio test -e native`
Expected: PASS (all tests, including the two new ones).

- [ ] **Step 5: Commit**

```bash
git add lib/alarmcore/debounce.h test/test_alarmcore/test_alarmcore.cpp
git commit -m "feat(core): host-tested Debouncer

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: QuadratureDecoder (pure)

**Files:**
- Create: `lib/alarmcore/quadrature.h`
- Test: `test/test_alarmcore/test_alarmcore.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `class alarmcore::QuadratureDecoder { explicit QuadratureDecoder(int stepsPerDetent = 4); int update(bool a, bool b); }` — returns `-1`/`0`/`+1`; non-zero once per completed detent. CW (sequence 00→10→11→01→00) yields `+1`.

- [ ] **Step 1: Write the failing tests**

Add `#include "quadrature.h"`, then:

```cpp
void test_quadrature_cw_one_detent() {
  QuadratureDecoder q(4);
  TEST_ASSERT_EQUAL_INT(0, q.update(true, false));   // 00 -> 10
  TEST_ASSERT_EQUAL_INT(0, q.update(true, true));    // 10 -> 11
  TEST_ASSERT_EQUAL_INT(0, q.update(false, true));   // 11 -> 01
  TEST_ASSERT_EQUAL_INT(1, q.update(false, false));  // 01 -> 00  (detent complete)
}

void test_quadrature_ccw_one_detent() {
  QuadratureDecoder q(4);
  TEST_ASSERT_EQUAL_INT(0, q.update(false, true));   // 00 -> 01
  TEST_ASSERT_EQUAL_INT(0, q.update(true, true));    // 01 -> 11
  TEST_ASSERT_EQUAL_INT(0, q.update(true, false));   // 11 -> 10
  TEST_ASSERT_EQUAL_INT(-1, q.update(false, false)); // 10 -> 00  (detent complete)
}
```

Register:

```cpp
  RUN_TEST(test_quadrature_cw_one_detent);
  RUN_TEST(test_quadrature_ccw_one_detent);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL (`quadrature.h` not found).

- [ ] **Step 3: Write the implementation**

Create `lib/alarmcore/quadrature.h`:

```cpp
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
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `pio test -e native`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/alarmcore/quadrature.h test/test_alarmcore/test_alarmcore.cpp
git commit -m "feat(core): host-tested QuadratureDecoder

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: PressClassifier (pure)

**Files:**
- Create: `lib/alarmcore/pressclassifier.h`
- Test: `test/test_alarmcore/test_alarmcore.cpp`

**Interfaces:**
- Consumes: nothing (fed an already-debounced `pressed` flag).
- Produces: `enum class alarmcore::PressEvent { NONE, SHORT_CLICK, LONG_PRESS };` and `class alarmcore::PressClassifier { explicit PressClassifier(uint32_t longMs = 1500); PressEvent update(bool pressed, uint32_t nowMs); }` — `SHORT_CLICK` on release before `longMs`; `LONG_PRESS` once when `longMs` is crossed while held (and then no `SHORT_CLICK` on that release).

- [ ] **Step 1: Write the failing tests**

Add `#include "pressclassifier.h"`, then:

```cpp
void test_press_short_click_on_release() {
  PressClassifier p(1500);
  TEST_ASSERT_EQUAL_INT((int)PressEvent::NONE, (int)p.update(true, 0));    // press
  TEST_ASSERT_EQUAL_INT((int)PressEvent::NONE, (int)p.update(true, 100));  // held briefly
  TEST_ASSERT_EQUAL_INT((int)PressEvent::SHORT_CLICK, (int)p.update(false, 120)); // release
}

void test_press_long_press_fires_once() {
  PressClassifier p(1500);
  p.update(true, 0);                                                       // press
  TEST_ASSERT_EQUAL_INT((int)PressEvent::NONE, (int)p.update(true, 1499));
  TEST_ASSERT_EQUAL_INT((int)PressEvent::LONG_PRESS, (int)p.update(true, 1500));
  TEST_ASSERT_EQUAL_INT((int)PressEvent::NONE, (int)p.update(true, 1800)); // still held
  TEST_ASSERT_EQUAL_INT((int)PressEvent::NONE, (int)p.update(false, 1900));// release: no short
}

void test_press_idle_is_none() {
  PressClassifier p(1500);
  TEST_ASSERT_EQUAL_INT((int)PressEvent::NONE, (int)p.update(false, 0));
  TEST_ASSERT_EQUAL_INT((int)PressEvent::NONE, (int)p.update(false, 5000));
}
```

Register:

```cpp
  RUN_TEST(test_press_short_click_on_release);
  RUN_TEST(test_press_long_press_fires_once);
  RUN_TEST(test_press_idle_is_none);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL (`pressclassifier.h` not found).

- [ ] **Step 3: Write the implementation**

Create `lib/alarmcore/pressclassifier.h`:

```cpp
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
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `pio test -e native`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/alarmcore/pressclassifier.h test/test_alarmcore/test_alarmcore.cpp
git commit -m "feat(core): host-tested PressClassifier (short-click / long-press)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: AppCore (UI state machine)

**Files:**
- Create: `lib/alarmcore/appcore.h`, `lib/alarmcore/appcore.cpp`
- Test: `test/test_alarmcore/test_alarmcore.cpp`

**Interfaces:**
- Consumes: `ListPayload`, `NewPayload`, `Heartbeat` (contract.h); `computeView()`, `LedMode` (view.h).
- Produces:
  - `enum class alarmcore::Screen { LIST, DETAIL, STATUS };`
  - `struct alarmcore::RenderModel { LedMode led; bool beep; Screen screen; std::vector<std::string> lines; int selectedIdx; int count; std::string maxSeverity; std::string detailText; std::string statusText; };`
  - `class alarmcore::AppCore` with: `void setList(const ListPayload&)`, `void onNew(const NewPayload&)`, `void onHeartbeat(const Heartbeat&, bool stale)`, `void nav(int delta)`, `void toggleDetail()`, `void toggleMute()`, `void acknowledge()`, `bool takeAckRequest()`, `bool muted() const`, `RenderModel render()`.

- [ ] **Step 1: Write the failing tests**

Add `#include "appcore.h"`, then a small builder + tests:

```cpp
static ListPayload makeList(int n) {
  ListPayload p; p.valid = true; p.count = n;
  p.max_severity = n > 0 ? "critical" : "";
  for (int i = 0; i < n; i++) {
    Alarm a; a.id = "id" + std::to_string(i); a.host = "host" + std::to_string(i);
    a.name = "name" + std::to_string(i); a.severity = "warning";
    a.summary = "summary" + std::to_string(i); a.since = "2026-06-27T10:00:00Z";
    p.alarms.push_back(a);
  }
  return p;
}

void test_appcore_selection_clamps() {
  AppCore app; app.setList(makeList(3));
  app.nav(+5);
  TEST_ASSERT_EQUAL_INT(2, app.render().selectedIdx);
  app.nav(-10);
  TEST_ASSERT_EQUAL_INT(0, app.render().selectedIdx);
}

void test_appcore_detail_toggle() {
  AppCore app; app.setList(makeList(3)); app.nav(+1);
  app.toggleDetail();
  RenderModel m = app.render();
  TEST_ASSERT_EQUAL_INT((int)Screen::DETAIL, (int)m.screen);
  TEST_ASSERT_TRUE(m.detailText.find("host1") != std::string::npos);
  app.toggleDetail();
  TEST_ASSERT_EQUAL_INT((int)Screen::LIST, (int)app.render().screen);
}

void test_appcore_detail_ignored_when_empty() {
  AppCore app; app.setList(makeList(0));
  app.toggleDetail();
  TEST_ASSERT_EQUAL_INT((int)Screen::LIST, (int)app.render().screen);
}

void test_appcore_mute_gates_beep() {
  AppCore muted; muted.setList(makeList(2));
  NewPayload n; n.valid = true; n.count_new = 1; n.max_severity = "critical";
  muted.toggleMute();
  muted.onNew(n);
  TEST_ASSERT_FALSE(muted.render().beep);

  AppCore loud; loud.setList(makeList(2));
  loud.onNew(n);
  TEST_ASSERT_TRUE(loud.render().beep);
}

void test_appcore_beep_is_one_shot() {
  AppCore app; app.setList(makeList(2));
  NewPayload n; n.valid = true; n.count_new = 1; n.max_severity = "critical";
  app.onNew(n);
  TEST_ASSERT_TRUE(app.render().beep);    // first render consumes it
  TEST_ASSERT_FALSE(app.render().beep);   // subsequent render: no repeat
}

void test_appcore_ack_request_is_one_shot() {
  AppCore app;
  app.acknowledge();
  TEST_ASSERT_TRUE(app.takeAckRequest());
  TEST_ASSERT_FALSE(app.takeAckRequest());
}

void test_appcore_new_list_reclamps_selection() {
  AppCore app; app.setList(makeList(3)); app.nav(+2);
  TEST_ASSERT_EQUAL_INT(2, app.render().selectedIdx);
  app.setList(makeList(1));
  TEST_ASSERT_EQUAL_INT(0, app.render().selectedIdx);
}

void test_appcore_conn_down_shows_status() {
  AppCore app; app.setList(makeList(2));
  Heartbeat hb;
  app.onHeartbeat(hb, true);   // stale -> ioBroker down
  RenderModel m = app.render();
  TEST_ASSERT_EQUAL_INT((int)Screen::STATUS, (int)m.screen);
  TEST_ASSERT_EQUAL_STRING("ioBroker?", m.statusText.c_str());
}
```

Register all eight:

```cpp
  RUN_TEST(test_appcore_selection_clamps);
  RUN_TEST(test_appcore_detail_toggle);
  RUN_TEST(test_appcore_detail_ignored_when_empty);
  RUN_TEST(test_appcore_mute_gates_beep);
  RUN_TEST(test_appcore_beep_is_one_shot);
  RUN_TEST(test_appcore_ack_request_is_one_shot);
  RUN_TEST(test_appcore_new_list_reclamps_selection);
  RUN_TEST(test_appcore_conn_down_shows_status);
```

Also add `#include <string>` is already pulled via contract.h; `std::to_string` needs `<string>` (already included by contract.h). No extra include required.

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL (`appcore.h` not found).

- [ ] **Step 3: Write the header**

Create `lib/alarmcore/appcore.h`:

```cpp
#pragma once
#include "contract.h"
#include "view.h"
#include <string>
#include <vector>

// UI state machine for the "dumb" button. Holds the latest contract payloads plus the
// interactive state (selection, detail view, mute) and emits one RenderModel per frame by
// composing the pure computeView(). Hardware-free / host-testable; the HAL renders it.
namespace alarmcore {

enum class Screen { LIST, DETAIL, STATUS };

struct RenderModel {
  LedMode led = LedMode::OFF;
  bool beep = false;                 // one-shot; already gated by mute
  Screen screen = Screen::LIST;
  std::vector<std::string> lines;    // LIST: "host name" per alarm
  int selectedIdx = 0;               // LIST
  int count = 0;
  std::string maxSeverity;           // LIST header colour
  std::string detailText;            // DETAIL
  std::string statusText;            // STATUS
};

class AppCore {
public:
  // data inputs (from MQTT later; from canned data in the demo)
  void setList(const ListPayload& list);
  void onNew(const NewPayload& n);            // one-shot, consumed by next render()
  void onHeartbeat(const Heartbeat& hb, bool stale);

  // events (from the HAL)
  void nav(int delta);
  void toggleDetail();
  void toggleMute();
  void acknowledge();

  bool takeAckRequest();                       // true once if an ack is pending, then clears
  bool muted() const { return muted_; }

  RenderModel render();                         // consumes the one-shot new event

private:
  void clampSelection();
  std::string detailText() const;

  ListPayload list_;
  NewPayload  new_;
  bool        newPending_ = false;
  Heartbeat   hb_;
  bool        stale_ = false;
  int         selectedIdx_ = 0;
  bool        detail_ = false;
  bool        muted_ = false;
  bool        ackPending_ = false;
};

} // namespace alarmcore
```

- [ ] **Step 4: Write the implementation**

Create `lib/alarmcore/appcore.cpp`:

```cpp
#include "appcore.h"

namespace alarmcore {

void AppCore::setList(const ListPayload& list) {
  list_ = list;
  if (!list_.valid || list_.count == 0) detail_ = false;
  clampSelection();
}

void AppCore::onNew(const NewPayload& n) { new_ = n; newPending_ = true; }

void AppCore::onHeartbeat(const Heartbeat& hb, bool stale) { hb_ = hb; stale_ = stale; }

void AppCore::nav(int delta) { selectedIdx_ += delta; clampSelection(); }

void AppCore::toggleDetail() {
  if (list_.valid && list_.count > 0) detail_ = !detail_;
}

void AppCore::toggleMute() { muted_ = !muted_; }

void AppCore::acknowledge() { ackPending_ = true; }

bool AppCore::takeAckRequest() {
  if (!ackPending_) return false;
  ackPending_ = false;
  return true;
}

void AppCore::clampSelection() {
  int n = (list_.valid ? list_.count : 0);
  if (n <= 0) { selectedIdx_ = 0; return; }
  if (selectedIdx_ < 0) selectedIdx_ = 0;
  if (selectedIdx_ > n - 1) selectedIdx_ = n - 1;
}

std::string AppCore::detailText() const {
  if (selectedIdx_ < 0 || selectedIdx_ >= (int)list_.alarms.size()) return "";
  const Alarm& a = list_.alarms[selectedIdx_];
  return a.host + "\n" + a.name + "\n" + a.summary + "\n" + a.since;
}

RenderModel AppCore::render() {
  ViewState v = computeView(list_, newPending_, new_, hb_, stale_);
  newPending_ = false;   // one-shot beep consumed

  RenderModel m;
  m.led = v.led;
  m.beep = v.beep && !muted_;
  m.count = v.count;
  m.maxSeverity = v.maxSeverity;
  m.lines = v.lines;
  m.selectedIdx = selectedIdx_;

  if (v.conn != Conn::OK) {
    m.screen = Screen::STATUS;
    m.statusText = v.statusText;
  } else if (detail_ && v.count > 0) {
    m.screen = Screen::DETAIL;
    m.detailText = detailText();
  } else {
    m.screen = Screen::LIST;
  }
  return m;
}

} // namespace alarmcore
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `pio test -e native`
Expected: PASS (all tests — original 13 + Tasks 1–4 additions).

- [ ] **Step 6: Commit**

```bash
git add lib/alarmcore/appcore.h lib/alarmcore/appcore.cpp test/test_alarmcore/test_alarmcore.cpp
git commit -m "feat(core): AppCore UI state machine (selection, detail, mute) over computeView

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: AxiometaHAL + minimal self-test firmware (hardware)

**Files:**
- Modify: `lib/alarmcore/hal.h` (one line — `showAlarmList` signature)
- Create: `src/AxiometaHAL.h`, `src/AxiometaHAL.cpp`
- Create: `src/main.cpp` (minimal self-test — replaced by the full demo in Task 6)

**Interfaces:**
- Consumes: `AlarmButtonHAL`, `StatusLedMode`, `AlertSound` (hal.h); `Debouncer`, `QuadratureDecoder`, `PressClassifier`, `PressEvent` (lib helpers); `Adafruit_ST7735`.
- Produces: `class AxiometaHAL : public alarmcore::AlarmButtonHAL` implementing the full interface. Getters are consume-on-read. `showAlarmList` now takes a third arg `const std::string& maxSeverity` (header colour).

> No native unit test (hardware). This task's deliverable is firmware that **builds, flashes, and shows a static self-test** on the LCD — proving the SPI/ST7735 wiring, the LED, and the buzzer in real firmware. The interactive behaviour is wired in Task 6.

- [ ] **Step 1: Extend the HAL interface to carry the header severity**

Modify `lib/alarmcore/hal.h` — change the `showAlarmList` pure-virtual signature:

```cpp
  virtual void showAlarmList(const std::vector<std::string>& lines, int selectedIdx,
                             const std::string& maxSeverity) = 0;
```

(Replaces the previous two-argument declaration. `maxSeverity` is `""`/`"warning"`/`"critical"`/`"info"` per the contract; the HAL colours the header from it.)

- [ ] **Step 2: Confirm the core still builds + tests pass (no consumer of `hal.h` yet)**

Run: `pio test -e native`
Expected: PASS (nothing in `lib/alarmcore` or the tests includes `hal.h`, so the signature change is inert until `AxiometaHAL` implements it).

- [ ] **Step 3: Write the HAL header**

Create `src/AxiometaHAL.h`:

```cpp
#pragma once
#include "hal.h"
#include "debounce.h"
#include "quadrature.h"
#include "pressclassifier.h"
#include <Adafruit_ST7735.h>
#include <string>
#include <vector>

// Hardware implementation of AlarmButtonHAL for the Axiometa Genesis Mini.
// Pins are the confirmed bring-up values (see CLAUDE.md "Module layout").
class AxiometaHAL : public alarmcore::AlarmButtonHAL {
public:
  AxiometaHAL();
  void init() override;
  void tick() override;

  bool acknowledgePressed() override;        // consume-on-read
  int  navDelta() override;                   // consume-on-read
  bool muteTogglePressed() override;          // consume-on-read
  bool detailTogglePressed() override;        // consume-on-read

  void setStatusLed(alarmcore::StatusLedMode mode) override;
  void playAlertSound(alarmcore::AlertSound level) override;
  void showAlarmList(const std::vector<std::string>& lines, int selectedIdx,
                     const std::string& maxSeverity) override;
  void showAlarmDetail(const std::string& text) override;
  void showStatus(const std::string& line) override;

private:
  void updateLed();
  uint16_t severityColor(const std::string& sev) const;

  Adafruit_ST7735 tft_;

  alarmcore::Debouncer         ackDeb_{5};
  alarmcore::Debouncer         pushDeb_{5};
  alarmcore::QuadratureDecoder quad_{4};
  alarmcore::PressClassifier   press_{1500};

  // latched abstract events (consume-on-read)
  bool ackEvent_ = false;
  int  navAccum_ = 0;
  bool muteEvent_ = false;
  bool detailEvent_ = false;

  alarmcore::StatusLedMode ledMode_ = alarmcore::StatusLedMode::OFF;
  std::string lastSig_;   // last rendered screen signature (skip redundant redraws)
};
```

- [ ] **Step 4: Write the HAL implementation**

Create `src/AxiometaHAL.cpp`:

```cpp
#include "AxiometaHAL.h"
#include <Arduino.h>
#include <SPI.h>

using namespace alarmcore;

// Confirmed pins (CLAUDE.md "Module layout (confirmed 2026-06-27)").
static const uint8_t PIN_LCD_MOSI = 12, PIN_LCD_SCK = 14;
static const uint8_t PIN_LCD_CS = 4, PIN_LCD_DC = 2, PIN_LCD_RST = 3;
static const uint8_t PIN_BUZZER = 6;
static const uint8_t PIN_BTN = 16;     // active-low (INPUT_PULLUP)
static const uint8_t PIN_LED = 15;     // active-high
static const uint8_t PIN_ENC_A = 17, PIN_ENC_B = 18, PIN_ENC_PUSH = 1;

static const uint16_t COL_ORANGE = 0xFD20;  // 565 orange (no named constant)

AxiometaHAL::AxiometaHAL() : tft_(PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST) {}

void AxiometaHAL::init() {
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_ENC_PUSH, INPUT_PULLUP);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  SPI.begin(PIN_LCD_SCK, -1, PIN_LCD_MOSI, -1);
  tft_.initR(INITR_MINI160x80);
  tft_.invertDisplay(false);
  tft_.setRotation(3);
  tft_.setTextWrap(false);
  tft_.fillScreen(ST77XX_BLACK);
  lastSig_.clear();
}

void AxiometaHAL::tick() {
  uint32_t now = millis();

  // Button (ack): active-low. A confirmed transition to "pressed" = a press event.
  bool btnRaw = (digitalRead(PIN_BTN) == LOW);
  if (ackDeb_.update(btnRaw, now) && ackDeb_.state()) ackEvent_ = true;

  // Encoder rotation.
  bool a = (digitalRead(PIN_ENC_A) == LOW);
  bool b = (digitalRead(PIN_ENC_B) == LOW);
  navAccum_ += quad_.update(a, b);

  // Encoder push: debounce, then classify short/long.
  bool pushRaw = (digitalRead(PIN_ENC_PUSH) == LOW);
  pushDeb_.update(pushRaw, now);
  switch (press_.update(pushDeb_.state(), now)) {
    case PressEvent::SHORT_CLICK: detailEvent_ = true; break;
    case PressEvent::LONG_PRESS:  muteEvent_ = true; break;
    case PressEvent::NONE: break;
  }

  updateLed();
}

bool AxiometaHAL::acknowledgePressed() { bool e = ackEvent_; ackEvent_ = false; return e; }
int  AxiometaHAL::navDelta()           { int d = navAccum_; navAccum_ = 0; return d; }
bool AxiometaHAL::muteTogglePressed()  { bool e = muteEvent_; muteEvent_ = false; return e; }
bool AxiometaHAL::detailTogglePressed(){ bool e = detailEvent_; detailEvent_ = false; return e; }

void AxiometaHAL::setStatusLed(StatusLedMode mode) { ledMode_ = mode; }

void AxiometaHAL::updateLed() {
  bool on = false;
  switch (ledMode_) {
    case StatusLedMode::OFF:        on = false; break;
    case StatusLedMode::SOLID:      on = true; break;
    case StatusLedMode::BLINK_SLOW: on = (millis() / 500) % 2; break;
    case StatusLedMode::BLINK_FAST: on = (millis() / 150) % 2; break;
  }
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

void AxiometaHAL::playAlertSound(AlertSound level) {
  if (level == AlertSound::OFF) return;
  tone(PIN_BUZZER, level == AlertSound::URGENT ? 3200 : 2700, 120);
}

uint16_t AxiometaHAL::severityColor(const std::string& sev) const {
  if (sev == "critical") return ST77XX_RED;
  if (sev == "warning")  return COL_ORANGE;
  return ST77XX_WHITE;   // info / unknown
}

void AxiometaHAL::showAlarmList(const std::vector<std::string>& lines, int selectedIdx,
                                const std::string& maxSeverity) {
  std::string sig = "L|" + std::to_string(selectedIdx) + "|" + maxSeverity;
  for (const auto& l : lines) sig += "|" + l;
  if (sig == lastSig_) return;     // unchanged -> skip redraw (no flicker)
  lastSig_ = sig;

  tft_.fillScreen(ST77XX_BLACK);
  tft_.setTextSize(1);

  tft_.setCursor(2, 2);
  tft_.setTextColor(lines.empty() ? ST77XX_GREEN : severityColor(maxSeverity));
  tft_.printf("ALARMS %d", (int)lines.size());
  tft_.drawFastHLine(0, 12, tft_.width(), ST77XX_WHITE);

  int y = 16;
  const int rowH = 9;
  int maxRows = (tft_.height() - y) / rowH;
  for (int i = 0; i < (int)lines.size() && i < maxRows; i++) {
    bool sel = (i == selectedIdx);
    if (sel) tft_.fillRect(0, y - 1, tft_.width(), rowH, ST77XX_WHITE);
    tft_.setTextColor(sel ? ST77XX_BLACK : ST77XX_WHITE);
    tft_.setCursor(2, y);
    tft_.print(sel ? ">" : " ");
    tft_.print(lines[i].substr(0, 25).c_str());
    y += rowH;
  }

  // connection-OK dot (the list screen is only shown when the link is up)
  tft_.fillCircle(tft_.width() - 5, tft_.height() - 5, 2, ST77XX_GREEN);
}

void AxiometaHAL::showAlarmDetail(const std::string& text) {
  std::string sig = "D|" + text;
  if (sig == lastSig_) return;
  lastSig_ = sig;

  tft_.fillScreen(ST77XX_BLACK);
  tft_.setTextSize(1);
  tft_.setTextWrap(true);
  tft_.setTextColor(ST77XX_WHITE);
  tft_.setCursor(2, 2);
  tft_.print(text.c_str());
  tft_.setTextWrap(false);
}

void AxiometaHAL::showStatus(const std::string& line) {
  std::string sig = "S|" + line;
  if (sig == lastSig_) return;
  lastSig_ = sig;

  tft_.fillScreen(ST77XX_BLACK);
  tft_.setTextSize(2);
  tft_.setTextColor(ST77XX_YELLOW);
  tft_.setCursor(4, 28);
  tft_.print(line.c_str());
  tft_.setTextSize(1);
}
```

- [ ] **Step 5: Write the minimal self-test firmware**

Create `src/main.cpp` (replaced by the full demo in Task 6 — this just proves the hardware path links + works):

```cpp
#include <Arduino.h>
#include "AxiometaHAL.h"
#include <string>
#include <vector>

using namespace alarmcore;

// Phase 0c HAL self-test: render a static list, blink the LED, beep once. Proves the
// ST7735/SPI wiring, the LED, and the buzzer in real firmware before the interactive demo.
static AxiometaHAL hal;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("phase 0c HAL self-test — static list + LED blink + one beep");
  hal.init();
  std::vector<std::string> lines = {"nutapp01 self-test", "nasapp01 ok", "dump1090 ok"};
  hal.showAlarmList(lines, 0, "critical");     // red header, row 0 selected
  hal.setStatusLed(StatusLedMode::BLINK_FAST);
  hal.playAlertSound(AlertSound::SHORT_BEEP);
}

void loop() {
  hal.tick();    // advances the LED blink
  delay(5);
}
```

- [ ] **Step 6: Build**

Run: `pio run -e axiometa-mini`
Expected: SUCCESS (compiles + links).

- [ ] **Step 7: Flash + verify on the board**

Run: `pio run -e axiometa-mini -t upload`
Then check the LCD/LED/buzzer:
1. LCD shows `ALARMS 3` (red header) + three rows, the first selected (`>` + white bar), a green dot bottom-right.
2. The button LED blinks fast.
3. A single short beep sounded on boot.

(If the LCD is blank/garbled, re-confirm the pins against CLAUDE.md; if no beep, confirm `PIN_BUZZER`.)

- [ ] **Step 8: Commit**

```bash
git add lib/alarmcore/hal.h src/AxiometaHAL.h src/AxiometaHAL.cpp src/main.cpp
git commit -m "feat(hal): AxiometaHAL on GPIO+ST7735 + minimal self-test firmware

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Wire AppCore into the interactive demo + bench verification

**Files:**
- Modify: `src/main.cpp` (replace the self-test with the full AppCore-driven demo)

**Interfaces:**
- Consumes: `AxiometaHAL`, `AppCore`, `RenderModel`, `Screen`, contract types, `StatusLedMode`, `AlertSound`, `LedMode`.
- Produces: the interactive phase-0c firmware.

- [ ] **Step 1: Replace `src/main.cpp` with the full demo**

Overwrite `src/main.cpp`:

```cpp
#include <Arduino.h>
#include "AxiometaHAL.h"
#include "appcore.h"
#include "contract.h"

using namespace alarmcore;

// Phase 0c demo: a canned alarm list drives the full HAL + AppCore loop on real hardware.
// No MQTT yet. Serial keys: 'n' inject a "new" event (one beep), 'r' restore the demo list.
static AxiometaHAL hal;
static AppCore app;

static StatusLedMode toStatusLed(LedMode m) {
  switch (m) {
    case LedMode::SOLID:      return StatusLedMode::SOLID;
    case LedMode::BLINK_FAST: return StatusLedMode::BLINK_FAST;
    case LedMode::OFF:
    default:                  return StatusLedMode::OFF;
  }
}

static ListPayload demoList() {
  ListPayload p; p.valid = true; p.count = 3; p.max_severity = "critical";
  auto add = [&](const char* id, const char* host, const char* name,
                 const char* sev, const char* summary) {
    Alarm a; a.id = id; a.host = host; a.name = name; a.severity = sev;
    a.summary = summary; a.since = "2026-06-27T10:00:00Z";
    p.alarms.push_back(a);
  };
  add("a1", "nutapp01", "USV auf Batterie", "critical",
      "nutapp01: USV laeuft auf Batterie - Stromausfall oder Training");
  add("b2", "nasapp01", "Disk Health", "warning",
      "nasapp01: S.M.A.R.T.-Status einer Platte verschlechtert");
  add("c3", "dump1090", "dump1090-fa down", "critical",
      "dump1090-fa service is not responding");
  return p;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("alarm-button phase 0c demo — 'n'=new beep, 'r'=restore list");
  hal.init();
  app.setList(demoList());
  Heartbeat hb; hb.valid = true; hb.grafana_ok = true; hb.poll_age_s = 2;
  app.onHeartbeat(hb, false);
}

void loop() {
  hal.tick();

  if (hal.acknowledgePressed())  { app.acknowledge();  Serial.println("ACK"); }
  int d = hal.navDelta();        if (d) app.nav(d);
  if (hal.detailTogglePressed()) { app.toggleDetail(); Serial.println("DETAIL toggle"); }
  if (hal.muteTogglePressed())   { app.toggleMute();   Serial.printf("MUTE=%d\n", app.muted()); }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'n') { NewPayload n; n.valid = true; n.count_new = 1; n.max_severity = "critical"; app.onNew(n); }
    if (c == 'r') { app.setList(demoList()); }
  }

  if (app.takeAckRequest()) {            // demo: ack clears the list
    ListPayload empty; empty.valid = true; empty.count = 0; empty.max_severity = "";
    app.setList(empty);
  }

  RenderModel m = app.render();
  hal.setStatusLed(toStatusLed(m.led));
  if (m.beep) hal.playAlertSound(AlertSound::SHORT_BEEP);
  switch (m.screen) {
    case Screen::LIST:   hal.showAlarmList(m.lines, m.selectedIdx, m.maxSeverity); break;
    case Screen::DETAIL: hal.showAlarmDetail(m.detailText);                        break;
    case Screen::STATUS: hal.showStatus(m.statusText);                            break;
  }
  delay(5);
}
```

- [ ] **Step 2: Build**

Run: `pio run -e axiometa-mini`
Expected: SUCCESS.

- [ ] **Step 3: Flash**

Run: `pio run -e axiometa-mini -t upload`
Expected: SUCCESS, board resets.

- [ ] **Step 4: Bench verification (manual — needs the operator)**

Open `pio device monitor -e axiometa-mini` and verify each on the board:

1. **List renders** — `ALARMS 3` (red header) + three rows, first selected.
2. **Nav** — rotating the encoder moves the selection, clamped at both ends. If it moves the *wrong way*, swap `PIN_ENC_A`/`PIN_ENC_B` in `AxiometaHAL.cpp`. If it advances two rows per detent (or needs two detents per row), change `QuadratureDecoder quad_{4}` to `{8}` (or `{2}`).
3. **Detail** — a short encoder-shaft click opens the full-screen detail of the selected alarm; another click returns to the list (serial: `DETAIL toggle`).
4. **Ack** — the LED push button clears the list (`ALARMS 0`, green header) and serial prints `ACK`; `r` on serial restores it.
5. **Beep** — `n` on serial fires one short beep.
6. **Mute** — hold the encoder shaft ≥1.5 s (serial `MUTE=1`); now `n` is silent. Hold again (`MUTE=0`); `n` beeps. List/LED unaffected by mute.
7. **Status LED** — alarms present → LED blinks fast; after ack (empty) → off.

Apply any one-line tuning fix found (encoder direction / steps-per-detent), rebuild, reflash, re-verify, then commit the fix together with Step 5.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat(firmware): phase 0c demo — canned list drives HAL + AppCore on hardware

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

- [ ] **Step 6: Update status docs + open the PR**

Update the **Status** section of `README.md` (and the relevant note in `CLAUDE.md` if appropriate) to record that phase 0c — HAL + core UI state machine + canned demo — is verified on hardware. Then:

```bash
git add README.md CLAUDE.md
git commit -m "docs: phase 0c HAL + state machine verified on hardware

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
git push -u origin feat/phase0c-axiometa-hal
gh pr create --base main --head feat/phase0c-axiometa-hal \
  --title "feat: phase 0c — AxiometaHAL + core UI state machine + canned demo" \
  --body "Implements docs/superpowers/specs/2026-06-27-axiometa-hal-design.md. Host-tested helpers (debounce, quadrature, press-classify) + AppCore UI state machine over computeView; AxiometaHAL on GPIO+ST7735; canned-data demo verified on hardware (list/nav/detail/ack/beep/mute/LED)."
```

---

## Notes for the implementer

- **One header per helper** keeps each unit small and individually testable; they have no dependencies on each other or on hardware.
- **Consume-on-read getters** in the HAL mean each event is delivered to `AppCore` exactly once per `loop()`; never read a getter twice per frame.
- **`computeView()` is unchanged** — `AppCore` composes it and adds only the interactive state. Do not duplicate its LED/beep/conn logic.
- If the encoder polling in `tick()` misses steps at fast rotation, that's acceptable for this iteration (the operator turns it slowly); a pin-change interrupt is a later optimization, out of scope.
