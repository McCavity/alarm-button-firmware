# CLAUDE.md — alarm-button-firmware

ESP32-S3-Firmware für den Homelab-Alarm-Button. Gegenstück zur ioBroker-Empfängerseite
([iobroker-scripts](https://github.com/McCavity/iobroker-scripts), `scripts/.../alarm-*`).

## Quellen der Wahrheit

- **MQTT-Vertrag + Designs liegen im privaten KI-OS-Vault** (`04-projects/alarm-button/`):
  `mqtt-contract.md` (Topics/Payloads, Schema 1), `design-2026-05-16-mini.md` (Hardware/HAL),
  `design-2026-06-07-orchestrator-selbsttest.md`. Bei Vertrags-/Verhaltensfragen dort nachsehen.
- Die Datentypen in `lib/alarmcore/contract.h` spiegeln den Vertrag — bei Vertragsänderung
  beide Seiten + `schema_version` gemeinsam anheben.

## Architektur-Regeln

- **Core ist hardware-frei.** Alles in `lib/alarmcore/` muss nativ kompilieren (kein
  `Arduino.h`, keine ESP-APIs). Hardware kommt ausschließlich hinter `AlarmButtonHAL`.
- **HAL liefert abstrakte Events** (`navDelta`, `acknowledgePressed` …), nie „Encoder-Drehung" —
  damit die parkierte LiliGo-3-Tasten-Variante unter denselben Core passt.
- **Native-Test-first:** neue Core-Logik mit `pio test -e native` (TDD), bevor Hardware-Code
  entsteht. Der ESP32-Build darf nie der einzige Verifikationspfad sein.
- Defensiv parsen (fail-safe Felder, Parse-Fehler → `valid=false`) — analog zur ioBroker-Seite.

## Build / Test / Flash

```bash
pio test -e native                    # Core host-testen
pio run -e axiometa-mini              # Firmware bauen
pio run -e axiometa-mini -t upload    # flashen
pio device monitor -e axiometa-mini   # serielle Konsole (115200)
```

## Hardware-Notizen

- Board: ESP32-S3, natives USB-CDC → erscheint als `/dev/cu.usbmodem*` (Datenkabel nötig,
  Ladekabel zeigt keinen Port). Flash 4 MB.
- **Vor dem ersten Flash immer die aktuelle Firmware sichern** (`esptool read_flash 0x0 0x400000 …`)
  und in `archive/` ablegen. Die Werk-Demo liegt bereits dort.
- AX22-Modul-Treiber / GENESIS-Arduino-Library: Bezugsquelle + `P<port>_IO<n>`-Pinschema beim
  Hardware-Tag mit dem Board in der Hand verifizieren (Design-Spec §1/Offene-Punkte).

## Git

Solo-Maintainer, PR-basiert (Branch Protection: 0 required Reviews, enforce_admins=false).
Commits englischer `type(scope): …`-Header. Nach neuen Repos org-index aktualisieren.
