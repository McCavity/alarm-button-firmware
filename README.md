# alarm-button-firmware

ESP32-S3 firmware for the **alarm button** — a physical status & acknowledge device for
open Grafana alerts in a homelab. Dark LED = all quiet, blinking red = attention, short
beep on new alarms.

The button is deliberately **dumb**: it renders an alarm list, blinks, beeps and sends
"acknowledged" back. All polling / diffing / silencing intelligence lives in ioBroker.

```
Grafana ──poll/silence──► ioBroker ──MQTT──► ESP32 button (this repo)
                          "the brain"  :1884   "dumb" display
```

The receiver side (ioBroker orchestrator + signal tower + MQTT publish) is built and live.

## Hardware

**Axiometa Genesis Mini** ([axiometa.io](https://www.axiometa.io)) — modular ESP32-S3 kit
(4 MB flash, 2 MB PSRAM), 4 AX22 module slots: Tactile LED Button (ack + status LED) ·
Rotary Encoder (nav + mute long-press) · Passive Buzzer · 0.96" IPS LCD. USB-C powered.

Board support is upstream in [espressif/arduino-esp32](https://github.com/espressif/arduino-esp32)
(variants *Axiometa GENESIS One* / *Genesis Mini*), so the `P<port>_IO<n>` pin macros are
available with the standard ESP32 Arduino core — no vendor library required.
[Arduino how-to](https://www.axiometa.io/pages/how-to-use-axiometa-genesis-with-arduino).

## Architecture

- **`lib/alarmcore/`** — host-testable core, **no** hardware dependency:
  - `contract.h` — MQTT contract data types (`list` / `new` / `heartbeat`, schema 1)
  - `parser.*` — JSON → structs (defensive: fail-safe fields, parse error → `valid=false`)
  - `view.*` — pure view derivation (alarms → LED / buzzer / display / status)
  - `hal.h` — `AlarmButtonHAL` interface (abstract events, so both HW variants fit underneath)
- **`src/main.cpp`** — ESP32 entry (WiFi + MQTT + HAL + render loop, **phase 0b**)
- **`test/`** — Unity tests against the core, run natively on the dev machine
- **`archive/`** — original demo firmware (full flash dump, **local only**, not committed:
  vendor blob)

## Build & test

```bash
pio test -e native            # host-test the core (no board needed)
pio run -e axiometa-mini      # build firmware for the ESP32-S3
pio run -e axiometa-mini -t upload   # flash (board on a USB data cable)
pio run -e bringup -t upload  # hardware self-test probe (verify module wiring — see tools/hw-bringup/)
```

## Restoring the demo firmware

The factory demo (internal-temperature display) is kept as a full flash dump in `archive/`
(local only — vendor blob, not in the repo):

```bash
esptool --port /dev/cu.usbmodem* write_flash 0x0 archive/demo-firmware-full-4MB-2026-06-27.bin
```

## Status

- **Foundation ✓** (2026-06-27): host-testable core (parser + view, 13 tests green),
  project skeleton, demo firmware archived.
- **Phase 0b ✓** (2026-06-27): on-board LED + button bring-up on hardware.
- **Phase 0c ✓** (2026-06-27): all four module pin maps confirmed on hardware (`bringup`
  probe); `AxiometaHAL` (LCD / LED / buzzer / encoder) + host-tested core UI state machine
  (`AppCore`: selection, detail toggle, mute; 28 native tests green); canned-data demo
  verified end-to-end on the board (list / nav / detail / ack / beep / mute / status LED).
- **Phase 1a ✓** (2026-06-28): WiFi + MQTT client (`MqttLink`, arduino-mqtt) subscribing
  `alarmbutton/office/{list,new,heartbeat}` → `parse*` → `AppCore` (canned list replaced),
  non-blocking reconnect + SNTP, `ack_all` publish on button press. HIL-verified end-to-end
  against the live ioBroker: connect (unique client id), retained list renders, `new`→beep,
  heartbeat-stale→"ioBroker?" (recovers), ack→signal tower solid. 28 native tests still green.
- **Phase 1b (next):** triage queue (unacked → detail, `ack_one`, list reconciliation) +
  contract `acked` flag → button LED mirrors the signal tower's tri-state (unacked blink /
  all acked solid / empty off); sustained alert sound (≤30 s or until ack).
- **Provisioning (roadmap):** Tasmota-style first-time setup without hard-coded secrets —
  an unconfigured board opens its own Wi-Fi (captive-portal AP) with a small web UI to set
  Wi-Fi + Wi-Fi security + MQTT credentials, password-protect admin access, optionally
  generate/rotate API keys. Goal: a nicer design than Tasmota at a reasonable flash
  footprint (secrets in NVS, not in code).

## Variants

`axiometa-mini` (active) · `lilygo-honyone` (parked — same core, different HAL impl).

## Disclaimer

This project is largely **AI-generated** (Claude Code, pair-programmed with the author).
Treat it as a hobby/portfolio project: review before relying on it, and use at your own risk.

## License

MIT — see [LICENSE](LICENSE).
