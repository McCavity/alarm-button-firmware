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

## Git

Solo maintainer, PR-based (branch protection: 0 required reviews, enforce_admins=false).
English `type(scope): …` commit headers. Update the org index after creating new repos.
