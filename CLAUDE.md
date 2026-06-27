# CLAUDE.md — alarm-button-firmware

ESP32-S3 firmware for the homelab alarm button. Counterpart to the ioBroker receiver side
([iobroker-scripts](https://github.com/McCavity/iobroker-scripts), `scripts/.../alarm-*`).

## Sources of truth

- **The MQTT contract and design docs live in the author's private KI-OS vault**
  (`04-projects/alarm-button/`): `mqtt-contract.md` (topics/payloads, schema 1),
  `design-2026-05-16-mini.md` (hardware/HAL), `design-2026-06-07-orchestrator-selbsttest.md`.
  Consult those for contract/behaviour questions.
- The types in `lib/alarmcore/contract.h` mirror the contract — on a contract change,
  bump both sides + `schema_version` together.

## Architecture rules

- **The core is hardware-free.** Everything in `lib/alarmcore/` must compile natively (no
  `Arduino.h`, no ESP APIs). Hardware lives only behind `AlarmButtonHAL`.
- **The HAL exposes abstract events** (`navDelta`, `acknowledgePressed`, …), never "encoder
  rotation" — so the parked LiliGo 3-button variant fits under the same core.
- **Native-test-first:** drive new core logic with `pio test -e native` (TDD) before any
  hardware code. The ESP32 build must never be the only verification path.
- Parse defensively (fail-safe fields, parse error → `valid=false`) — like the ioBroker side.

## Build / test / flash

```bash
pio test -e native                    # host-test the core
pio run -e axiometa-mini              # build firmware
pio run -e axiometa-mini -t upload    # flash
pio device monitor -e axiometa-mini   # serial console (115200)
```

## Hardware notes

- Board: ESP32-S3, native USB-CDC → shows up as `/dev/cu.usbmodem*` (needs a data cable; a
  charge-only cable shows no port). Flash 4 MB.
- Board support (pin macros `P<port>_IO<n>`) is upstream in `espressif/arduino-esp32`
  (variants *Axiometa GENESIS One* / *Genesis Mini*). PlatformIO may lag the core version —
  if the variant is missing, either use a platform fork tracking arduino-esp32 3.x or map the
  `P*_IO*` GPIOs explicitly from the variant's `pins_arduino.h`.
- **Always back up the current firmware before the first flash** (`esptool read_flash 0x0
  0x400000 …`) and keep it in `archive/` (local only). The factory demo is already there.

## Module layout (confirmed 2026-06-27)

As wired (only change vs. the kit default: temp sensor → LED push button). GPIOs from the
upstream variant `pins_arduino.h` — verify the exact pin-within-module at bring-up.

| Slot | Module | Pins | Notes |
|---|---|---|---|
| 1 | OLED display | I2C `SDA=10`, `SCL=11` (shared bus) | **mono OLED** (likely SSD1306 128×64), NOT the colour IPS LCD the design assumed → no severity colours; use U8g2/Adafruit_SSD1306, verify I2C address |
| 2 | Passive buzzer | `P2_IO0=7` (IO1=6, IO2=5) | `tone()`/`ledc`; confirm which IO carries the signal |
| 3 | LED push button | `P3_IO0=9`, `IO1=16`, `IO2=15` | button input + LED; confirm which IO is button vs LED |
| 4 | Rotary encoder | `P4_IO0=1`, `IO1=17`, `IO2=18` | A / B / push |

On-board (no slot): RGB LED `GPIO21` (`neopixelWrite`), user button `GPIO45` — used by the
phase-0b bring-up smoke test in `src/main.cpp`.

This deviates from `design-2026-05-16-mini.md` (vault), which assumed display=slot 4,
button=slot 1, encoder=slot 2 + a colour IPS LCD. The HAL follows the **real** wiring above.

## Git

Solo maintainer, PR-based (branch protection: 0 required reviews, enforce_admins=false).
English `type(scope): …` commit headers. Update the org index after creating new repos.
