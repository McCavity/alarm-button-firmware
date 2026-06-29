# Phase 1b — Triage, acked tri-state, ack_one, sustained sound

> Design spec · 2026-06-29 · alarm-button-firmware (Axiometa Genesis Mini, ESP32-S3)
> Counterpart: ioBroker side already merged + HIL-tested
> ([iobroker-scripts#8](https://github.com/McCavity/iobroker-scripts/pull/8),
> builds on [#5](https://github.com/McCavity/iobroker-scripts/pull/5) `acked` in `list`).
> Contract: KI-OS vault `04-projects/alarm-button/mqtt-contract.md` §3.1 (`acked`, additive
> since 2026-06-28) and §3.4 (`ack_one`/`ack_all`).

## Goal

Make the button a triage tool: per-alarm `acked` from the contract drives the button LED as a
tri-state mirror of the ioBroker signal tower, the ACK button acknowledges the **one** alarm in
focus (`ack_one`), an unacked list auto-walks the user through the unacked alarms in detail, and a
new alarm sounds a sustained urgent tone until acknowledged.

Five chunks: (1) core `acked` flag, (2) LED tri-state, (3) `ack_one` gesture, (4) triage flow,
(5) sustained alert sound.

## Architecture constraints (unchanged)

- The core (`lib/alarmcore/`) stays hardware-free and host-testable (`pio test -e native`). No
  `Arduino.h`, no ESP APIs. TDD-first: drive new core logic with native tests before hardware.
- The HAL exposes abstract events/actuators. Hardware-side changes (buzzer level behaviour, MQTT
  publish) live in `src/`, verified by HIL, not by the native suite.
- Parse defensively: missing/garbled field → fail-safe value, never a silent "all clear".

## Decisions taken in brainstorming

- **`ack_all` placement:** the button emits **only `ack_one`**. `ack_all` remains purely the MQTT
  action used by the wall switch via ioBroker — no button `ack_all`, no long-press classifier on
  the ACK button.
- **Sustained sound scope:** in scope for 1b (chunk 5).
- **Sound stop:** first `ack_one` press stops it (even if other unacked remain); also 30 s elapse
  or mute. Each `new` event **re-arms** the 30 s window.
- **Optimistic ack (Approach A):** on `ack_one`, mark the focused alarm `acked` locally at once
  and advance, rather than waiting up to 15 s for the authoritative `list` republish (the
  `list` is `retain=false`, republished every 15 s). The next republish reconciles/corrects.
- **Focus by fingerprint id**, not list index — a 15 s republish or a wall-switch ack must not
  make the focus jump.

---

## Chunk 1 — Core `acked` flag

**`lib/alarmcore/contract.h`** — add to `Alarm`:

```cpp
bool acked = false;   // per-alarm acknowledge state (contract §3.1, additive). ioBroker is truth.
```

**`lib/alarmcore/parser.cpp`** — in the `parseList` alarm loop:

```cpp
al.acked = a["acked"] | false;   // missing -> false (fail-safe: unacked = attention)
```

Default `false` covers a not-yet-deployed ioBroker (live ioBroker has `acked` from PR #5; field is
additive so an old payload simply lacks it → treated as unacked).

## Chunk 2 — LED tri-state (`computeView`)

Replaces the current `view.cpp:20` (`led = count>0 ? BLINK_FAST : OFF`). Mirrors the ioBroker
`computeSignaltower` tri-state:

| List state                  | `LedMode`    |
|-----------------------------|--------------|
| `count == 0` (empty)        | `OFF`        |
| ≥1 alarm with `!acked`      | `BLINK_FAST` |
| `count > 0`, all `acked`    | `SOLID`      |

`LedMode::SOLID` already exists; this is a `computeView` change only. The "any unacked" scan runs
over `last.alarms`.

`computeView` stays the **stateless** LED/connection-status derivation. The beep is **removed**
from `computeView` (`v.beep` deleted) — sound timing moves to `AppCore` (chunk 5), which has a
clock. `computeView` keeps: `led`, `count`, `maxSeverity`, `lines`, `conn`, `statusText`.

## Chunk 3 — `ack_one` gesture

The ACK button (slot-3 push, active-low) emits one `ack_one` per press for the **focused** alarm.

- `AppCore`: replace `bool takeAckRequest()` with

  ```cpp
  bool takeAckOne(std::string& id);   // true + fills focused fingerprint; false if no focus
  ```

  Empty/no focus (e.g. empty list) → returns false → nothing published.

- `src/MqttLink.h/.cpp`: `void publishAck(const char* action, const char* id = nullptr);`
  When `id != nullptr` (i.e. `ack_one`) add `"id": "<fingerprint>"` to the JSON. Existing
  best-effort `ts` logic (omit until SNTP synced, contract §3.4) is unchanged.

- `src/main.cpp`:

  ```cpp
  if (hal.acknowledgePressed()) app.acknowledge();
  std::string id;
  if (app.takeAckOne(id)) mqtt.publishAck("ack_one", id.c_str());
  ```

No `PressClassifier` on the ACK button (no long-press). The encoder push keeps its classifier
(short = detail toggle, long = mute) unchanged.

## Chunk 4 — Triage flow (`AppCore`)

State machine drives which screen shows and which alarm is focused. Focus tracked by
`std::string focusId_` (fingerprint), with `selectedIdx_` mirroring it.

**Screen priority:** `STATUS` (connection down) > `DETAIL` (triage active) > `LIST`.

**Reconcile, run after every `setList`:**

1. `focusId_` still present in the new list **and** that alarm is `!acked` → keep focus
   (no jump). Screen `DETAIL`. `selectedIdx_` ← its index.
2. Otherwise the focused alarm is gone (resolved) or now `acked` → it must move:
   - any `!acked` alarm exists → focus the **first unacked** (contract order: severity, then
     `since`). Screen `DETAIL`. `selectedIdx_` ← its index.
   - no unacked (all acked, or list empty) → `focusId_ = ""`, screen `LIST`, `selectedIdx_ = 0`
     (top of list).

The forced jump happens **only** on focus loss, never on a plain 15 s republish of the same
unacked set.

**Acknowledge (optimistic):** `acknowledge()` →
- record the focused fingerprint for `takeAckOne`,
- mark that alarm `acked = true` in the local `list_` (optimistic),
- advance focus per the reconcile rule (next unacked, or `LIST` if none),
- cancel the urgent sound window (chunk 5).

The next authoritative `list` republish re-runs reconcile and corrects if ioBroker disagrees.

**Manual nav / detail:** `nav()` moves `selectedIdx_` within `LIST` as today. The encoder
short-press (`toggleDetail`) still toggles `LIST`/`DETAIL` manually; `toggleMute` (long-press)
unchanged. (No manual-override suppression of auto-triage in 1b — auto-entry on a new unacked set
is the intended behaviour.)

## Chunk 5 — Sustained alert sound

The urgent tone has a duration, so it needs a clock injected into the core (mirrors the
`PressClassifier` injected-millis pattern).

- `AppCore::render()` → `AppCore::render(uint32_t nowMs)`.
- New state `uint32_t urgentUntilMs_ = 0;`
- In `render(nowMs)`, when the one-shot new event is consumed (`newPending_` && valid &&
  `count_new > 0`): `urgentUntilMs_ = nowMs + 30000;` (re-arm on every new event).
- `acknowledge()` sets `urgentUntilMs_ = 0` (first ack stops the sound).
- Sound output each frame:
  - `URGENT` when `nowMs < urgentUntilMs_` **and** `!muted_`,
  - else `OFF`.
- `RenderModel`: replace `bool beep` with `AlertSound sound = AlertSound::OFF;` (`AlertSound`
  from `hal.h`; `appcore.h` includes `hal.h`, which is hardware-free).

**HAL (hardware side, HIL-tested):** `AxiometaHAL::playAlertSound` becomes level/state-based
instead of one-shot. It stores the requested level; `tick()` re-triggers the buzzer periodically
(e.g. a short `tone()` every ~600 ms) while the level is `URGENT`, and calls `noTone()` on `OFF`.
`SHORT_BEEP` (if still requested anywhere) remains a single chirp. `src/main.cpp` calls
`hal.playAlertSound(m.sound)` every frame.

---

## Out of scope (1b)

- Button-side `ack_all` (decided: button is `ack_one`-only).
- `list` `retain=true` server-side change — the 15 s republish + optimistic local ack covers the
  boot/lag gap (logged tradeoff; revisit server-side separately if the boot wait bites).
- Provisioning / captive portal (roadmap).

## Test plan

**Native core (`pio test -e native`, TDD-first):**

- Parser: `acked` parsed true/false; missing `acked` → `false`.
- LED tri-state: empty → `OFF`; mixed (some `!acked`) → `BLINK_FAST`; all `acked` → `SOLID`.
- `ack_one` id capture: `takeAckOne` returns the focused fingerprint; no focus → false.
- Triage reconcile:
  - first unacked focused + `DETAIL` on a fresh unacked list;
  - focus held across a republish of the same unacked set;
  - focused alarm resolved (id gone) + other unacked present → jump to first unacked;
  - focused alarm becomes `acked` (wall switch) → jump to next unacked;
  - all acked / empty → `LIST`, `selectedIdx_ = 0`;
  - optimistic ack: after `acknowledge()`, focused alarm reads `acked`, focus advanced,
    LED reflects remaining unacked without a new `setList`.
- Urgent window: arms on new event; `URGENT` while in window & unmuted; stops on `acknowledge`;
  stops at 30 s; muted → `OFF`; re-arm on a second new event extends the deadline.
- Update existing beep tests to the new `RenderModel::sound`.

**Hardware / HIL (after native green):**

- `pio run -e axiometa-mini` builds; flash; serial monitor.
- Buzzer plays a sustained urgent pattern on a new alarm and stops on ACK / after 30 s / on mute.
- `publishAck("ack_one", id)` emits valid JSON with the fingerprint; ioBroker flips that alarm to
  `acked:true` (PR #8 path), signal tower `fast_blink → solid` when it was the last unacked.
- Button LED tracks tri-state against the live `list` (blink → solid → off).
- End-to-end triage against live ioBroker: inject test alarm(s) (ZigBee remote), walk the queue
  with ACK, confirm focus advance + LED + tower.

## Acceptance criteria

- [ ] Native suite green (existing + new triage/sound/acked tests).
- [ ] `acked` parsed defensively (missing → unacked).
- [ ] Button LED is the signal-tower tri-state (off / fast-blink / solid).
- [ ] ACK button publishes `ack_one` with the focused fingerprint; no button `ack_all`.
- [ ] Triage auto-walks unacked alarms by id; focus loss jumps to first unacked or LIST-top;
      optimistic ack feels immediate (no 15 s wait).
- [ ] Sustained urgent sound: starts on new, stops on first ack / 30 s / mute, re-arms on new.
- [ ] HIL pass against live ioBroker; README status + contract cross-refs updated.
