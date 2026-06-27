# Design: AxiometaHAL + core UI state machine (Phase 0c, T9 + T4)

> Created: 2026-06-27
> Branch: `feat/phase0c-axiometa-hal`
> Plan task: T9 (AxiometaHAL) + T4 (core state machine), `plan-2026-05-16-mini.md` (vault)
> Status: approved, ready for implementation plan

## Goal

Make the alarm button react on real hardware: render an alarm list / detail view on the
LCD, drive the status LED, beep the buzzer, and turn encoder + button input into abstract
events — all wired through a small, host-tested core. MQTT/WiFi is **out of scope** here
(separate task); this iteration is driven by a canned alarm list so every input and output
is verifiable on the bench.

## Scope

In scope:
- Pure, host-tested input helpers: debounce, quadrature decode, long-press/short-click.
- `AppCore` — UI state machine (selection, detail toggle, mute) wrapping the existing
  `computeView()`.
- `AxiometaHAL` — implements `AlarmButtonHAL` on the Genesis Mini hardware.
- `src/main.cpp` — canned-data demo + render loop exercising every I/O path.

Out of scope (later tasks): WiFi, MQTT client, secrets/provisioning, ack → ioBroker silence,
master-mute, NVS persistence, per-alarm snooze, the parked LilyGo variant.

## Architecture

Hardware-free, host-tested code lives in `lib/alarmcore/`; ESP32-only code lives in `src/`.

```
lib/alarmcore/
  debounce.h         NEW  pure: raw bool + now_ms -> stable state + edge events
  quadrature.h       NEW  pure: (A,B) transitions -> nav delta (-1/0/+1)
  pressclassifier.h  NEW  pure: (pressed,now_ms) -> ShortClick | LongPress
  appcore.h/.cpp     NEW  UI state machine; composes computeView(); emits RenderModel
  hal.h              existing  AlarmButtonHAL interface (abstract events)
  contract.h         existing  MQTT contract types
  parser.h/.cpp      existing  JSON -> structs
  view.h/.cpp        existing  computeView() (LED/beep/conn derivation)
src/
  AxiometaHAL.h/.cpp NEW  implements AlarmButtonHAL using GPIO + ST7735 + the helpers
  main.cpp           NEW  canned-data demo, render loop
```

Rationale for the input helpers living in `lib/alarmcore` (approach A, chosen over inlining
in the HAL): the trickiest logic (quadrature edge table, long-press timing) becomes
hardware-free and testable with `pio test -e native` before any GPIO code — per the
project's native-test-first rule. `AxiometaHAL` just feeds them pin reads + `millis()`.

## Components

### Input helpers (pure)

- **Debouncer** — holds last stable state + last-change timestamp; `update(raw, now_ms)`
  returns the debounced state and whether a (rising/falling) edge just occurred. Default
  ~5 ms window. Used for the LED button and the encoder push.
- **QuadratureDecoder** — holds last (A,B); `update(a, b)` returns -1/0/+1 using the standard
  Gray-code transition table. CW = +1; if the bench shows it reversed, swap A/B at the HAL
  call site (wiring left the A-vs-B order undetermined at bring-up — a one-line flip).
- **PressClassifier** — `update(pressed, now_ms)` emits `ShortClick` on release before the
  threshold and `LongPress` exactly once when the threshold (1500 ms, per design §3) is
  crossed while still held. No event while idle.

### AppCore (UI state machine)

Owns: latest `ListPayload`, `NewPayload` (one-shot), `Heartbeat` + stale flag; `selectedIdx`;
`viewMode ∈ {LIST, DETAIL}`; `muted`.

Inputs (from MQTT later, from canned data now):
`setList(ListPayload)`, `onNew(NewPayload)`, `onHeartbeat(Heartbeat, bool stale)`.

Events (from HAL): `nav(int delta)`, `toggleDetail()`, `toggleMute()`, `acknowledge()`.

Output: `RenderModel render()`:
- `led` (OFF / BLINK_FAST / SOLID) — from `computeView()`. `main` maps this onto the HAL's
  wider `StatusLedMode` (OFF→OFF, SOLID→SOLID, BLINK_FAST→BLINK_FAST); BLINK_SLOW is unused
  this iteration.
- `beep` (bool, one-shot) — from `computeView()`, **gated false when `muted`**.
- `screen` — `STATUS` when connection is down (overrides), else `DETAIL` when `viewMode ==
  DETAIL` and the list is non-empty, else `LIST`.
- list payload: `lines`, `selectedIdx`, `count`, `maxSeverity` for the LIST screen.
- detail text (host / name / summary / since of `alarms[selectedIdx]`) for DETAIL.
- `statusText` ("Connecting…" / "Grafana?" / "ioBroker?") for STATUS.

State rules:
- `selectedIdx` clamped to `[0, count-1]`; re-clamped whenever the list changes; 0 when empty.
- `nav` moves `selectedIdx` by delta, clamped (no wrap).
- `toggleDetail` flips LIST↔DETAIL; ignored (stays LIST) when the list is empty.
- `toggleMute` flips `muted`; affects the buzzer only (LED/LCD unchanged) — contract §3.5.
- `acknowledge` raises a pending ack request the caller can take (`takeAckRequest()`); core
  stays side-effect-free. In the demo, `main` clears the canned list on ack.
- A new list with `count == 0` returns the view to LIST.

`viewMode`, `muted`, and derived alerting (`count > 0`) are orthogonal — modelled as flags,
not four exclusive states — because you can be muted while alerting, etc.

### AxiometaHAL (hardware)

Implements `AlarmButtonHAL`. Pins per CLAUDE.md "Module layout (confirmed 2026-06-27)":

| Function | GPIO | Notes |
|---|---|---|
| LCD SPI | MOSI 12, SCK 14 | hardware SPI remapped to these pins |
| LCD CS / DC / RST | 4 / 2 / 3 | ST7735 `INITR_MINI160x80`, `setRotation(3)`, `invertDisplay(false)` |
| Buzzer | 6 | `tone()` |
| Button (ack) | 16 | `INPUT_PULLUP`, active-low, debounced |
| Button LED | 15 | active-high; OFF/SOLID/BLINK driven in `tick()` via millis |
| Encoder A / B | 17 / 18 | quadrature → `navDelta()` |
| Encoder push | 1 | `INPUT_PULLUP` → PressClassifier → detail (short) / mute (long) |

- `tick()` polls all inputs, runs debounce/quadrature/press-classify, latches the resulting
  abstract events for the getters (`acknowledgePressed`, `navDelta`, `muteTogglePressed`,
  `detailTogglePressed`), and advances the LED blink phase.
- `setStatusLed(mode)` stores the mode; the blink is produced in `tick()`.
- `playAlertSound(level)` → short `tone()` on GPIO6 (URGENT reserved for later).
- `showAlarmList(lines, selectedIdx)` / `showAlarmDetail(text)` / `showStatus(line)` render
  to the LCD (header colored by max severity, `▶` + inverted row on selection, status dot).

### LCD layout (160×80 landscape, ~26×10 chars at font size 1)

```
ALARMS 3                       header, bg/text colored by max_severity
------------------------------
> pveapp01 cpu                 selected: '>' + inverted background
  nasapp01 disk
  dwsapp01 rain
                         o      status dot (green ok / yellow connecting / red down)
```
Detail = full-screen wrapped text. Status screen = centered status line. Severity colors:
critical→red, warning→orange, info→white/grey (contract §5).

### main.cpp demo (no MQTT)

- Build a hardcoded `ListPayload` (2–3 alarms, mixed severity).
- Loop: `hal.tick()` → drain HAL events into `AppCore` → `AppCore.render()` →
  drive `hal.set*/show*`.
- Encoder rotates selection; click toggles detail; LED button acks (clears the list);
  long-press mutes. A serial key (and/or on-board GPIO45) injects an `onNew(...)` event to
  fire the one-shot beep and demonstrate mute gating it.

## Error handling

- Parser stays defensive (existing behavior): malformed input → `valid=false`, never crashes
  the render loop.
- `selectedIdx` always clamped, so an empty or shrinking list can never index out of range.
- LCD: render the STATUS screen whenever connection is down, so a dead link is visible rather
  than a stale list.

## Testing

- Native Unity tests (TDD, written before the hardware code) for `Debouncer`,
  `QuadratureDecoder`, `PressClassifier`, and `AppCore` (selection clamp, detail toggle
  on empty list, mute gates beep, ack request, new-list re-clamp, connection-down screen).
- `AxiometaHAL` + `main.cpp` verified by hand on the board: list renders, selection moves,
  detail toggles, ack clears, long-press mutes, injected "new" beeps once and is silenced
  while muted, severity colors correct.

## Open items deferred (not blocking)

- A-vs-B encoder direction: confirm sign on the bench, flip at the HAL call site if reversed.
- LED active-high assumption: confirmed at bring-up (drove GPIO15 high → lit).
- Exact list-row count / font size: tune on hardware if 7 rows feels cramped.
