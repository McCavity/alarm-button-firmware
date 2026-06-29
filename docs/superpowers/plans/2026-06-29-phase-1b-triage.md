# Phase 1b — Triage / acked tri-state / ack_one / sustained sound — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the button into a triage tool — per-alarm `acked` drives a tri-state LED, the ACK button acknowledges the focused alarm via `ack_one`, an unacked list auto-walks the user through detail views, and a new alarm sounds a sustained urgent tone until acknowledged.

**Architecture:** All decision logic lands in the hardware-free core (`lib/alarmcore/`), driven by native Unity tests first. Focus is tracked as the fingerprint under the list cursor and re-located across each `list` republish (`retain=false`, ~15 s). Acks apply optimistically to the local list; the next republish is the authoritative correction. Only the buzzer's sustained-tone behaviour and the MQTT publish live in `src/` (HIL-verified).

**Tech Stack:** C++17, PlatformIO (`native` + `axiometa-mini` envs), Unity test framework, ArduinoJson 7, Adafruit ST7735, 256dpi/MQTT.

## Global Constraints

- The core (`lib/alarmcore/`) MUST compile natively: no `Arduino.h`, no ESP APIs. Verify with `pio test -e native`.
- Parse defensively: a missing/garbled field becomes a fail-safe value, never a silent "all clear" (missing `acked` → `false` = unacked = attention).
- The button emits **only `ack_one`** — no button `ack_all` (that stays the wall-switch/ioBroker MQTT action). Contract: vault `mqtt-contract.md` §3.1 (`acked`), §3.4 (`ack`).
- English `type(scope): …` commit headers. Solo maintainer, PR-based; work on branch `feat/phase1b-triage`.
- Sound timing is injected (`render(uint32_t nowMs)`), mirroring the `PressClassifier` injected-millis pattern — no wall-clock reads in the core.

---

### Task 1: Core `acked` flag (contract + parser)

**Files:**
- Modify: `lib/alarmcore/contract.h` (struct `Alarm`)
- Modify: `lib/alarmcore/parser.cpp` (`parseList` alarm loop)
- Test: `test/test_alarmcore/test_alarmcore.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `Alarm::acked` (`bool`, default `false`); `parseList` fills it from JSON `"acked"`, missing → `false`.

- [ ] **Step 1: Write the failing tests**

Add after `test_parseList_host_failsafe` (≈ line 49):

```cpp
void test_parseList_acked() {
  ListPayload p = parseList(
    R"({"count":2,"alarms":[)"
    R"({"id":"a","host":"h","severity":"warning","acked":true},)"
    R"({"id":"b","host":"h2","severity":"warning","acked":false}]})");
  TEST_ASSERT_TRUE(p.valid);
  TEST_ASSERT_TRUE(p.alarms[0].acked);
  TEST_ASSERT_FALSE(p.alarms[1].acked);
}

void test_parseList_acked_missing_defaults_false() {
  ListPayload p = parseList(R"({"count":1,"alarms":[{"id":"a","host":"h","severity":"warning"}]})");
  TEST_ASSERT_FALSE(p.alarms[0].acked);
}
```

Register them in `main()` after `RUN_TEST(test_parseList_host_failsafe);`:

```cpp
  RUN_TEST(test_parseList_acked);
  RUN_TEST(test_parseList_acked_missing_defaults_false);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL — compile error, `Alarm` has no member `acked`.

- [ ] **Step 3: Add the field and parse it**

In `lib/alarmcore/contract.h`, add to `struct Alarm` after `since`:

```cpp
  bool acked = false;    // per-alarm acknowledge state (contract §3.1, additive). ioBroker is truth.
```

In `lib/alarmcore/parser.cpp`, inside the `parseList` alarm loop, after `al.since = …`:

```cpp
    al.acked = a["acked"] | false;   // missing -> false (fail-safe: unacked = attention)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `pio test -e native`
Expected: PASS (all tests, including the two new ones).

- [ ] **Step 5: Commit**

```bash
git add lib/alarmcore/contract.h lib/alarmcore/parser.cpp test/test_alarmcore/test_alarmcore.cpp
git commit -m "feat(core): parse per-alarm acked flag (contract §3.1)"
```

---

### Task 2: LED tri-state in `computeView`

**Files:**
- Modify: `lib/alarmcore/view.cpp` (replace `view.cpp:20` LED logic)
- Test: `test/test_alarmcore/test_alarmcore.cpp`

**Interfaces:**
- Consumes: `Alarm::acked` (Task 1).
- Produces: `computeView(...).led` is now `OFF` (empty) / `BLINK_FAST` (≥1 unacked) / `SOLID` (count>0, all acked). Signature unchanged this task.

- [ ] **Step 1: Write the failing tests**

Add after `test_view_empty_led_off` (≈ line 87):

```cpp
void test_view_all_acked_solid() {
  ListPayload p = parseList(LIST_JSON);
  for (auto& a : p.alarms) a.acked = true;
  Heartbeat h; h.valid = true; h.grafana_ok = true; h.poll_age_s = 2;
  ViewState v = computeView(p, false, NewPayload{}, h, false);
  TEST_ASSERT_EQUAL_INT((int)LedMode::SOLID, (int)v.led);
}

void test_view_partial_acked_blinks() {
  ListPayload p = parseList(LIST_JSON);
  p.alarms[0].acked = true;        // one acked, one still unacked
  Heartbeat h; h.valid = true; h.grafana_ok = true; h.poll_age_s = 2;
  ViewState v = computeView(p, false, NewPayload{}, h, false);
  TEST_ASSERT_EQUAL_INT((int)LedMode::BLINK_FAST, (int)v.led);
}
```

Register after `RUN_TEST(test_view_empty_led_off);`:

```cpp
  RUN_TEST(test_view_all_acked_solid);
  RUN_TEST(test_view_partial_acked_blinks);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL — `test_view_all_acked_solid` gets `BLINK_FAST`, expected `SOLID`.

- [ ] **Step 3: Replace the LED logic**

In `lib/alarmcore/view.cpp`, replace the single line (currently `view.cpp:20`):

```cpp
  v.led = (v.count > 0) ? LedMode::BLINK_FAST : LedMode::OFF;
```

with the tri-state mirror of the ioBroker signal tower:

```cpp
  // LED tri-state mirrors computeSignaltower: empty -> off, any unacked -> fast blink,
  // all acked -> solid. (The acked flag is the contract's per-alarm ack state, §3.1.)
  bool anyUnacked = false;
  if (last.valid)
    for (const auto& a : last.alarms)
      if (!a.acked) { anyUnacked = true; break; }
  if (v.count == 0)        v.led = LedMode::OFF;
  else if (anyUnacked)     v.led = LedMode::BLINK_FAST;
  else                     v.led = LedMode::SOLID;
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `pio test -e native`
Expected: PASS (existing `test_view_alarms_blink_ok` still blinks — its alarms have no `acked` field → `false`).

- [ ] **Step 5: Commit**

```bash
git add lib/alarmcore/view.cpp test/test_alarmcore/test_alarmcore.cpp
git commit -m "feat(core): LED tri-state (off/blink/solid) from acked flag"
```

---

### Task 3: Triage flow — focus by fingerprint, auto-detail (read side)

**Files:**
- Modify: `lib/alarmcore/appcore.h` (private helpers)
- Modify: `lib/alarmcore/appcore.cpp` (`setList` → reconcile; new helpers)
- Test: `test/test_alarmcore/test_alarmcore.cpp`

**Interfaces:**
- Consumes: `Alarm::acked` (Task 1).
- Produces: after `setList`, the cursor (`selectedIdx_`) tracks an unacked alarm and `detail_` auto-engages. New private helpers: `std::string focusId() const`, `int firstUnacked() const`, `void reconcileFocus(const std::string& prevId)`. Screen logic (`render`) unchanged: `STATUS` > (`detail_ && count>0` ? `DETAIL` : `LIST`). `acknowledge()`/`takeAckRequest()` still as today (Task 4 replaces them).

- [ ] **Step 1: Write the failing tests**

Add a helper + tests after `makeList` (≈ line 175):

```cpp
static ListPayload makeListAllAcked(int n) {
  ListPayload p = makeList(n);
  for (auto& a : p.alarms) a.acked = true;
  return p;
}

void test_appcore_triage_enters_detail_on_unacked() {
  AppCore app; app.setList(makeList(2));        // both unacked
  RenderModel m = app.render();
  TEST_ASSERT_EQUAL_INT((int)Screen::DETAIL, (int)m.screen);
  TEST_ASSERT_EQUAL_INT(0, m.selectedIdx);      // first unacked
}

void test_appcore_triage_all_acked_shows_list() {
  AppCore app; app.setList(makeListAllAcked(2));
  RenderModel m = app.render();
  TEST_ASSERT_EQUAL_INT((int)Screen::LIST, (int)m.screen);
}

void test_appcore_triage_focus_held_across_republish() {
  AppCore app; app.setList(makeList(3)); app.nav(+2);   // focus id2
  TEST_ASSERT_EQUAL_INT(2, app.render().selectedIdx);
  app.setList(makeList(3));                              // same set republished
  TEST_ASSERT_EQUAL_INT(2, app.render().selectedIdx);   // still id2, no jump
}

void test_appcore_triage_focus_lost_jumps_to_first_unacked() {
  AppCore app; app.setList(makeList(3)); app.nav(+2);    // focus id2
  ListPayload p = makeList(3);
  p.alarms.pop_back(); p.count = 2;                      // id2 resolved (gone)
  app.setList(p);
  RenderModel m = app.render();
  TEST_ASSERT_EQUAL_INT(0, m.selectedIdx);              // jumped to first unacked id0
  TEST_ASSERT_EQUAL_INT((int)Screen::DETAIL, (int)m.screen);
}

void test_appcore_triage_focus_acked_jumps_to_next() {
  AppCore app; app.setList(makeList(3));                 // focus id0
  ListPayload p = makeList(3);
  p.alarms[0].acked = true;                             // id0 acked by wall switch
  app.setList(p);
  TEST_ASSERT_EQUAL_INT(1, app.render().selectedIdx);  // jumped to id1 (first unacked)
}
```

Replace the body of the existing `test_appcore_detail_toggle` (≈ lines 185-193) with the manual-peek semantics (keep the function name):

```cpp
void test_appcore_detail_toggle() {
  AppCore app; app.setList(makeList(3));        // auto-DETAIL on first unacked
  TEST_ASSERT_EQUAL_INT((int)Screen::DETAIL, (int)app.render().screen);
  app.toggleDetail();                            // manual peek to LIST
  TEST_ASSERT_EQUAL_INT((int)Screen::LIST, (int)app.render().screen);
  app.toggleDetail();
  TEST_ASSERT_EQUAL_INT((int)Screen::DETAIL, (int)app.render().screen);
}
```

Register the new tests after `RUN_TEST(test_appcore_conn_down_shows_status);`:

```cpp
  RUN_TEST(test_appcore_triage_enters_detail_on_unacked);
  RUN_TEST(test_appcore_triage_all_acked_shows_list);
  RUN_TEST(test_appcore_triage_focus_held_across_republish);
  RUN_TEST(test_appcore_triage_focus_lost_jumps_to_first_unacked);
  RUN_TEST(test_appcore_triage_focus_acked_jumps_to_next);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL — `test_appcore_triage_enters_detail_on_unacked` gets `LIST` (no auto-detail yet).

- [ ] **Step 3: Implement reconcile**

In `lib/alarmcore/appcore.h`, add to the `private:` section (after `void clampSelection();`):

```cpp
  std::string focusId() const;              // fingerprint under the cursor in the current list
  int firstUnacked() const;                 // index of first !acked alarm, or -1
  void reconcileFocus(const std::string& prevId);  // re-locate cursor after a list update
```

In `lib/alarmcore/appcore.cpp`, replace the whole `setList` body:

```cpp
void AppCore::setList(const ListPayload& list) {
  if (!list_.valid || list_.count == 0) detail_ = false;
  clampSelection();
}
```

with:

```cpp
void AppCore::setList(const ListPayload& list) {
  std::string prevId = focusId();   // fingerprint under the cursor in the OLD list
  list_ = list;
  reconcileFocus(prevId);
}

std::string AppCore::focusId() const {
  if (list_.valid && selectedIdx_ >= 0 && selectedIdx_ < (int)list_.alarms.size())
    return list_.alarms[selectedIdx_].id;
  return "";
}

int AppCore::firstUnacked() const {
  for (int i = 0; i < (int)list_.alarms.size(); i++)
    if (!list_.alarms[i].acked) return i;
  return -1;
}

void AppCore::reconcileFocus(const std::string& prevId) {
  int n = list_.valid ? (int)list_.alarms.size() : 0;
  if (n == 0) { selectedIdx_ = 0; detail_ = false; return; }
  // Hold focus if the same fingerprint is still present AND still unacked (no yank on republish).
  int keep = -1;
  if (!prevId.empty())
    for (int i = 0; i < n; i++)
      if (list_.alarms[i].id == prevId) { keep = i; break; }
  if (keep >= 0 && !list_.alarms[keep].acked) {
    selectedIdx_ = keep;                              // leave detail_ as the user left it
  } else {
    int fu = firstUnacked();
    if (fu >= 0) { selectedIdx_ = fu; detail_ = true; }   // jump to first unacked, auto-detail
    else         { selectedIdx_ = 0; detail_ = false; }   // all acked -> list top
  }
  clampSelection();
}
```

(`setList` no longer calls `clampSelection()` directly — `reconcileFocus` ends with it.)

- [ ] **Step 4: Run tests to verify they pass**

Run: `pio test -e native`
Expected: PASS. Note `test_appcore_new_list_reclamps_selection` still passes: focus id2 is gone in `makeList(1)` → jump to first unacked id0 → `selectedIdx 0`.

- [ ] **Step 5: Commit**

```bash
git add lib/alarmcore/appcore.h lib/alarmcore/appcore.cpp test/test_alarmcore/test_alarmcore.cpp
git commit -m "feat(core): triage focus by fingerprint with auto-detail + reconcile"
```

---

### Task 4: `ack_one` — optimistic ack on the focused alarm (write side)

**Files:**
- Modify: `lib/alarmcore/appcore.h` (`takeAckRequest` → `takeAckOne`; add `ackId_`)
- Modify: `lib/alarmcore/appcore.cpp` (`acknowledge`, `takeAckOne`)
- Modify: `src/MqttLink.h`, `src/MqttLink.cpp` (`publishAck` gains optional `id`)
- Modify: `src/main.cpp` (ack wiring)
- Test: `test/test_alarmcore/test_alarmcore.cpp`

**Interfaces:**
- Consumes: `firstUnacked()` (Task 3), `Alarm::acked`.
- Produces: `bool AppCore::takeAckOne(std::string& id)` — true once after a press, fills the acked alarm's fingerprint; `acknowledge()` marks that alarm `acked=true` locally and advances the cursor; `void MqttLink::publishAck(const char* action, const char* id = nullptr)`.

- [ ] **Step 1: Write the failing tests**

Replace the existing `test_appcore_ack_request_is_one_shot` (≈ lines 221-226) with:

```cpp
void test_appcore_ack_one_captures_focus_id() {
  AppCore app; app.setList(makeList(3));        // focus -> id0 (first unacked)
  app.acknowledge();
  std::string id;
  TEST_ASSERT_TRUE(app.takeAckOne(id));
  TEST_ASSERT_EQUAL_STRING("id0", id.c_str());
  TEST_ASSERT_FALSE(app.takeAckOne(id));        // one-shot
}

void test_appcore_ack_one_optimistic_advance() {
  AppCore app; app.setList(makeList(3));        // id0 focused, all unacked, DETAIL
  app.acknowledge();                             // ack id0 -> advance to id1
  RenderModel m = app.render();
  TEST_ASSERT_EQUAL_INT((int)Screen::DETAIL, (int)m.screen);
  TEST_ASSERT_EQUAL_INT(1, m.selectedIdx);       // advanced to next unacked
  TEST_ASSERT_EQUAL_INT((int)LedMode::BLINK_FAST, (int)m.led);  // id1,id2 still unacked
}

void test_appcore_ack_one_last_goes_solid_list() {
  AppCore app; app.setList(makeList(1));        // single unacked, focus id0, DETAIL
  app.acknowledge();                             // ack id0 -> none left
  RenderModel m = app.render();
  TEST_ASSERT_EQUAL_INT((int)Screen::LIST, (int)m.screen);
  TEST_ASSERT_EQUAL_INT((int)LedMode::SOLID, (int)m.led);
}

void test_appcore_ack_one_no_focus_when_empty() {
  AppCore app; app.setList(makeList(0));
  app.acknowledge();
  std::string id;
  TEST_ASSERT_FALSE(app.takeAckOne(id));
}
```

In `main()`, replace `RUN_TEST(test_appcore_ack_request_is_one_shot);` with:

```cpp
  RUN_TEST(test_appcore_ack_one_captures_focus_id);
  RUN_TEST(test_appcore_ack_one_optimistic_advance);
  RUN_TEST(test_appcore_ack_one_last_goes_solid_list);
  RUN_TEST(test_appcore_ack_one_no_focus_when_empty);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL — compile error, `AppCore` has no member `takeAckOne`.

- [ ] **Step 3: Implement optimistic ack in the core**

In `lib/alarmcore/appcore.h`: replace `bool takeAckRequest();` with

```cpp
  bool takeAckOne(std::string& id);            // true once after a press; fills focused fingerprint
```

and add to `private:` (near `ackPending_`):

```cpp
  std::string ackId_;
```

In `lib/alarmcore/appcore.cpp`, replace `acknowledge()` and `takeAckRequest()`:

```cpp
void AppCore::acknowledge() { ackPending_ = true; }

bool AppCore::takeAckRequest() {
  if (!ackPending_) return false;
  ackPending_ = false;
  return true;
}
```

with:

```cpp
void AppCore::acknowledge() {
  int n = list_.valid ? (int)list_.alarms.size() : 0;
  if (n == 0 || selectedIdx_ < 0 || selectedIdx_ >= n) return;   // no focus -> no-op
  ackId_ = list_.alarms[selectedIdx_].id;
  ackPending_ = true;
  list_.alarms[selectedIdx_].acked = true;        // optimistic: don't wait for the republish
  // advance to the next unacked alarm, or drop to the list when none remain
  int fu = firstUnacked();
  if (fu >= 0) { selectedIdx_ = fu; detail_ = true; }
  else         { selectedIdx_ = 0; detail_ = false; }
  clampSelection();
}

bool AppCore::takeAckOne(std::string& id) {
  if (!ackPending_) return false;
  ackPending_ = false;
  id = ackId_;
  return true;
}
```

- [ ] **Step 4: Run core tests to verify they pass**

Run: `pio test -e native`
Expected: PASS.

- [ ] **Step 5: Extend `publishAck` and wire main.cpp**

In `src/MqttLink.h`, change the declaration:

```cpp
  void publishAck(const char* action, const char* id = nullptr);
```

In `src/MqttLink.cpp`, replace the `publishAck` body with an id-aware version (adds `"id"` for `ack_one`; keeps the SNTP best-effort `ts`):

```cpp
void MqttLink::publishAck(const char* action, const char* id) {
  char ts[32];
  char payload[224];
  bool haveTs = isoTimeUtc(ts, sizeof(ts));
  char idField[160] = "";
  if (id && id[0]) snprintf(idField, sizeof(idField), ",\"id\":\"%s\"", id);
  if (haveTs) {
    snprintf(payload, sizeof(payload),
      "{\"schema_version\":1,\"device_id\":\"%s\",\"ts\":\"%s\",\"action\":\"%s\"%s}",
      DEVICE_ID, ts, action, idField);
  } else {
    snprintf(payload, sizeof(payload),
      "{\"schema_version\":1,\"device_id\":\"%s\",\"action\":\"%s\"%s}",
      DEVICE_ID, action, idField);
  }
  client_.publish("alarmbutton/" DEVICE_ID "/ack", payload, false, 1);  // no retain, QoS 1
  Serial.printf("[mqtt] ack -> %s\n", payload);
}
```

In `src/main.cpp`, replace the two ack lines (currently `main.cpp:38-39`):

```cpp
  if (hal.acknowledgePressed())  { app.acknowledge();  Serial.println("ACK"); }
  if (app.takeAckRequest()) mqtt.publishAck("ack_all");
```

with:

```cpp
  if (hal.acknowledgePressed()) { app.acknowledge(); Serial.println("ACK"); }
  std::string ackId;
  if (app.takeAckOne(ackId)) mqtt.publishAck("ack_one", ackId.c_str());
```

Add `#include <string>` near the top of `src/main.cpp` if not already present (it is pulled in transitively via the core headers, but make it explicit).

- [ ] **Step 6: Verify the firmware still builds**

Run: `pio run -e axiometa-mini`
Expected: build SUCCESS (no flash yet).

- [ ] **Step 7: Commit**

```bash
git add lib/alarmcore/appcore.h lib/alarmcore/appcore.cpp src/MqttLink.h src/MqttLink.cpp src/main.cpp test/test_alarmcore/test_alarmcore.cpp
git commit -m "feat: ack_one on focused alarm (optimistic core + id in publishAck)"
```

---

### Task 5: Sustained urgent sound (core side)

**Files:**
- Modify: `lib/alarmcore/view.h`, `lib/alarmcore/view.cpp` (drop `beep`, simplify signature)
- Modify: `lib/alarmcore/appcore.h` (`render(uint32_t)`, `RenderModel::sound`, `urgentUntilMs_`)
- Modify: `lib/alarmcore/appcore.cpp` (`render` sound logic; `acknowledge` cancels window)
- Modify: `src/main.cpp` (`render(millis())`, `playAlertSound(m.sound)`)
- Test: `test/test_alarmcore/test_alarmcore.cpp`

**Interfaces:**
- Consumes: `AlertSound` (from `hal.h`), `acknowledge()` (Task 4).
- Produces: `RenderModel AppCore::render(uint32_t nowMs = 0)`; `RenderModel::sound` (`AlertSound`, default `OFF`); `computeView(const ListPayload&, const Heartbeat&, bool)` (new-event params removed); `ViewState::beep` removed.

- [ ] **Step 1: Update existing sound tests + add window tests (write the failing tests)**

Replace `test_appcore_mute_gates_beep` and `test_appcore_beep_is_one_shot` (≈ lines 201-219) with:

```cpp
void test_appcore_mute_gates_sound() {
  NewPayload n; n.valid = true; n.count_new = 1; n.max_severity = "critical";

  AppCore muted; muted.setList(makeList(2));
  muted.toggleMute();
  muted.onNew(n);
  TEST_ASSERT_EQUAL_INT((int)AlertSound::OFF, (int)muted.render(1000).sound);

  AppCore loud; loud.setList(makeList(2));
  loud.onNew(n);
  TEST_ASSERT_EQUAL_INT((int)AlertSound::URGENT, (int)loud.render(1000).sound);
}

void test_appcore_urgent_window_times_out() {
  AppCore app; app.setList(makeList(2));
  NewPayload n; n.valid = true; n.count_new = 1; n.max_severity = "critical";
  app.onNew(n);
  TEST_ASSERT_EQUAL_INT((int)AlertSound::URGENT, (int)app.render(0).sound);      // armed at t=0 (until 30000)
  TEST_ASSERT_EQUAL_INT((int)AlertSound::URGENT, (int)app.render(29999).sound);  // still within window
  TEST_ASSERT_EQUAL_INT((int)AlertSound::OFF, (int)app.render(30000).sound);     // 30 s elapsed
}

void test_appcore_urgent_stops_on_ack() {
  AppCore app; app.setList(makeList(2));
  NewPayload n; n.valid = true; n.count_new = 1; n.max_severity = "critical";
  app.onNew(n);
  TEST_ASSERT_EQUAL_INT((int)AlertSound::URGENT, (int)app.render(0).sound);
  app.acknowledge();
  TEST_ASSERT_EQUAL_INT((int)AlertSound::OFF, (int)app.render(1000).sound);
}

void test_appcore_urgent_rearms_on_new() {
  AppCore app; app.setList(makeList(2));
  NewPayload n; n.valid = true; n.count_new = 1; n.max_severity = "critical";
  app.onNew(n);
  app.render(0);                     // arm: until 30000
  app.onNew(n);
  app.render(20000);                 // re-arm: until 50000
  TEST_ASSERT_EQUAL_INT((int)AlertSound::URGENT, (int)app.render(40000).sound);  // off without re-arm
}
```

Delete `test_view_new_triggers_beep` (≈ lines 89-95) and remove the line `TEST_ASSERT_FALSE(v.beep);` from `test_view_alarms_blink_ok` (≈ line 77).

In `main()`: replace the two lines
```cpp
  RUN_TEST(test_appcore_mute_gates_beep);
  RUN_TEST(test_appcore_beep_is_one_shot);
```
with
```cpp
  RUN_TEST(test_appcore_mute_gates_sound);
  RUN_TEST(test_appcore_urgent_window_times_out);
  RUN_TEST(test_appcore_urgent_stops_on_ack);
  RUN_TEST(test_appcore_urgent_rearms_on_new);
```
and delete the line `RUN_TEST(test_view_new_triggers_beep);`.

Update the remaining `computeView(...)` call sites to the new 3-arg signature:
- in `test_view_alarms_blink_ok`: `computeView(p, false, NewPayload{}, h, false)` → `computeView(p, h, false)`
- in `test_view_empty_led_off`: `computeView(p, false, NewPayload{}, Heartbeat{}, false)` → `computeView(p, Heartbeat{}, false)`
- in `test_view_stale_iobroker_down`: `computeView(ListPayload{}, false, NewPayload{}, Heartbeat{}, true)` → `computeView(ListPayload{}, Heartbeat{}, true)`
- in `test_view_grafana_down`: `computeView(p, false, NewPayload{}, h, false)` → `computeView(p, h, false)`
- in `test_view_all_acked_solid` / `test_view_partial_acked_blinks` (Task 2): `computeView(p, false, NewPayload{}, h, false)` → `computeView(p, h, false)`

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL — compile error: `render` takes no argument / `RenderModel` has no member `sound` / `computeView` arity mismatch.

- [ ] **Step 3: Drop beep from the view**

In `lib/alarmcore/view.h`: remove the `bool beep = false;` field from `ViewState`, and change the declaration to:

```cpp
// heartbeatStale=true -> ioBroker connection dead (no heartbeat past the stale threshold;
// cf. contract heartbeat: >45 s ~ 3 missed). Sound lives in AppCore (it needs a clock).
ViewState computeView(const ListPayload& last, const Heartbeat& hb, bool heartbeatStale);
```

In `lib/alarmcore/view.cpp`: change the signature to match and delete the beep block:

```cpp
ViewState computeView(const ListPayload& last, const Heartbeat& hb, bool heartbeatStale) {
```

Remove these two lines:

```cpp
  // Beep only on a fresh new event with count_new > 0 (anti-spam: 1 beep per burst).
  v.beep = newEvent && newP.valid && newP.count_new > 0;
```

- [ ] **Step 4: Move sound into AppCore**

In `lib/alarmcore/appcore.h`:
- add `#include "hal.h"` near the other includes;
- in `struct RenderModel`, replace `bool beep = false;` with:

```cpp
  AlertSound sound = AlertSound::OFF;   // OFF | URGENT (sustained, gated by mute)
```

- change the method declaration `RenderModel render();` to:

```cpp
  RenderModel render(uint32_t nowMs = 0);       // consumes the one-shot new event; nowMs drives the alert window
```

- add to `private:` (near `muted_`):

```cpp
  uint32_t urgentUntilMs_ = 0;          // urgent-sound window deadline (0 = not sounding)
```

In `lib/alarmcore/appcore.cpp`, replace the whole `render()` body:

```cpp
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
```

with:

```cpp
RenderModel AppCore::render(uint32_t nowMs) {
  ViewState v = computeView(list_, hb_, stale_);

  // A fresh new event arms a 30 s urgent window (re-arm on every new); first ack / mute /
  // timeout end it. acknowledge() zeroes the deadline.
  if (newPending_ && new_.valid && new_.count_new > 0) urgentUntilMs_ = nowMs + 30000;
  newPending_ = false;   // one-shot consumed

  RenderModel m;
  m.led = v.led;
  m.sound = (nowMs < urgentUntilMs_ && !muted_) ? AlertSound::URGENT : AlertSound::OFF;
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
```

In the same file, add one line to `acknowledge()` (after `ackPending_ = true;`) so the first ack stops the sound:

```cpp
  urgentUntilMs_ = 0;   // first ack stops the urgent sound
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `pio test -e native`
Expected: PASS (all tests).

- [ ] **Step 6: Wire main.cpp to the new sound output**

In `src/main.cpp`, replace `RenderModel m = app.render();` with:

```cpp
  RenderModel m = app.render(millis());
```

and replace `if (m.beep) hal.playAlertSound(AlertSound::SHORT_BEEP);` with:

```cpp
  hal.playAlertSound(m.sound);
```

- [ ] **Step 7: Verify the firmware builds**

Run: `pio run -e axiometa-mini`
Expected: build SUCCESS. (The buzzer is still one-shot in the HAL — fixed in Task 6 — so do not flash yet.)

- [ ] **Step 8: Commit**

```bash
git add lib/alarmcore/view.h lib/alarmcore/view.cpp lib/alarmcore/appcore.h lib/alarmcore/appcore.cpp src/main.cpp test/test_alarmcore/test_alarmcore.cpp
git commit -m "feat(core): sustained urgent sound window (30s / until ack / mute, re-arm)"
```

---

### Task 6: Sustained buzzer in the HAL (hardware side)

**Files:**
- Modify: `src/AxiometaHAL.h` (state members + `updateSound`)
- Modify: `src/AxiometaHAL.cpp` (`playAlertSound` becomes level-based; `tick` re-triggers)

**Interfaces:**
- Consumes: `RenderModel::sound` via `main.cpp` calling `hal.playAlertSound(m.sound)` every frame (Task 5).
- Produces: a sustained beep-beep pattern while the level is `URGENT`, silence on `OFF`. No native test (hardware-only); verified by build + HIL in Task 7.

- [ ] **Step 1: Make `playAlertSound` level-based**

In `src/AxiometaHAL.h`, add to `private:` (after `void updateLed();`):

```cpp
  void updateSound(uint32_t now);
```

and add state members (after `alarmcore::StatusLedMode ledMode_ = …;`):

```cpp
  alarmcore::AlertSound soundMode_ = alarmcore::AlertSound::OFF;
  uint32_t lastBeepMs_ = 0;
```

In `src/AxiometaHAL.cpp`, replace the current one-shot `playAlertSound`:

```cpp
void AxiometaHAL::playAlertSound(AlertSound level) {
  if (level == AlertSound::OFF) return;
  tone(PIN_BUZZER, level == AlertSound::URGENT ? 3200 : 2700, 120);
}
```

with a state setter plus a tick-driven re-trigger:

```cpp
void AxiometaHAL::playAlertSound(AlertSound level) { soundMode_ = level; }

void AxiometaHAL::updateSound(uint32_t now) {
  switch (soundMode_) {
    case AlertSound::URGENT:
      if (now - lastBeepMs_ >= 600) { tone(PIN_BUZZER, 3200, 200); lastBeepMs_ = now; }
      break;
    case AlertSound::SHORT_BEEP:
      tone(PIN_BUZZER, 2700, 120); soundMode_ = AlertSound::OFF;   // single chirp
      break;
    case AlertSound::OFF:
    default:
      noTone(PIN_BUZZER);
      break;
  }
}
```

In `tick()`, add the sound pump next to `updateLed();` (end of the method):

```cpp
  updateSound(now);
```

- [ ] **Step 2: Verify the firmware builds**

Run: `pio run -e axiometa-mini`
Expected: build SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add src/AxiometaHAL.h src/AxiometaHAL.cpp
git commit -m "feat(hal): sustained urgent buzzer pattern (level-based playAlertSound)"
```

---

### Task 7: Flash, HIL verification, docs

**Files:**
- Modify: `README.md` (Status section)
- Modify: `lib/alarmcore/contract.h` (header comment cross-ref, if needed)

**Interfaces:**
- Consumes: everything above; the live ioBroker `ack_one` path is already merged + tested (iobroker-scripts#8).

- [ ] **Step 1: Confirm the full native suite is green**

Run: `pio test -e native`
Expected: PASS (all tests).

- [ ] **Step 2: Flash the firmware**

Run: `pio run -e axiometa-mini -t upload`
Expected: upload SUCCESS to `/dev/cu.usbmodem*`.

- [ ] **Step 3: HIL — drive the board over serial (per the HIL bench workflow)**

Open the serial monitor (`pio device monitor -e axiometa-mini`, 115200) and, against the live ioBroker (inject a Warning/Critical test alarm via the ZigBee remote):

- New alarm → buzzer plays the sustained urgent pattern; the LCD auto-enters DETAIL on the alarm; the button LED `BLINK_FAST`.
- Press ACK → serial prints `[mqtt] ack -> {…"action":"ack_one","id":"<fingerprint>"…}`; sound stops immediately; ioBroker flips that alarm to `acked:true`.
- When it was the last unacked alarm → button LED `SOLID`, signal tower `fast_blink → solid`, list view returns.
- Two alarms: ACK walks focus to the next unacked; LED stays `BLINK_FAST` until both acked.
- Let an unacked new alarm ring without acking → sound stops by itself at ~30 s; LED keeps blinking.
- Mute (encoder long-press) silences the urgent sound.
- Resolve an alarm in ioBroker (or auto-clear) → on the next list republish the focus jumps to the first remaining unacked, or to the empty/solid list.

Record results in the session log / `phase1b-status` memory.

- [ ] **Step 4: Update the README status**

In `README.md`, under `## Status`, change the `Phase 1b (next)` bullet to a `✓` entry dated today, e.g.:

```markdown
- **Phase 1b ✓** (2026-06-29): per-alarm `acked` consumed → button LED is the signal-tower
  tri-state (off / blink / solid); triage queue auto-walks unacked alarms in detail with focus
  pinned by fingerprint (optimistic local ack, reconciled on republish); ACK button publishes
  `ack_one` for the focused alarm (no button `ack_all`); sustained urgent sound on a new alarm
  (≤30 s, stops on ack / mute, re-arms per new event). Native suite green; HIL-verified against
  live ioBroker (iobroker-scripts#8 path).
```

Optionally tighten the `contract.h` top comment to note `acked` is consumed by the button.

- [ ] **Step 5: Commit**

```bash
git add README.md lib/alarmcore/contract.h
git commit -m "docs(phase1b): mark 1b complete (acked tri-state, triage, ack_one, urgent sound)"
```

- [ ] **Step 6: Finish the branch**

Use the `superpowers:finishing-a-development-branch` skill to open the PR (`feat/phase1b-triage` → `main`) and update the org index per the project convention.

---

## Self-Review

**Spec coverage:**
- Chunk 1 (acked flag) → Task 1. ✓
- Chunk 2 (LED tri-state, replaces `view.cpp:20`) → Task 2. ✓
- Chunk 3 (ack_one only, focused alarm, id in publish) → Task 4. ✓
- Chunk 4 (triage by fingerprint, reconcile, focus-loss → first unacked / list top, optimistic ack) → Task 3 (read) + Task 4 (ack). ✓
- Chunk 5 (sustained sound, 30 s / first ack / mute, re-arm, injected clock) → Task 5 (core) + Task 6 (HAL). ✓
- Test plan (native cases + HIL) → covered across Tasks 1–5 (native) and Task 7 (HIL). ✓
- Acceptance criteria → Task 7 HIL + README. ✓

**Placeholder scan:** No TBD/TODO/"add error handling"/"similar to Task N"; every code step shows full code. ✓

**Type consistency:** `takeAckOne(std::string&)` (Tasks 3-ref/4), `firstUnacked()`/`focusId()`/`reconcileFocus()` (Task 3, used in Task 4), `render(uint32_t)` + `RenderModel::sound`/`AlertSound` + `computeView(ListPayload, Heartbeat, bool)` (Task 5), `publishAck(action, id)` (Task 4), `playAlertSound`/`updateSound`/`soundMode_` (Task 6) — names match across tasks. `acknowledge()` becomes optimistic in Task 4 and gains the `urgentUntilMs_ = 0` line in Task 5 (consistent member from Task 5's header change). ✓
