# alarm-button-firmware

ESP32-S3-Firmware für den **Alarm-Button** — ein physischer Status-/Quittierungs-Knopf
für offene Grafana-Alerts im Homelab. Schwarze LED = alles ruhig, rot blinkend =
Aufmerksamkeit, kurzer Pieper bei neuen Alarmen.

Der Knopf ist bewusst **dumm**: er rendert eine Alarmliste, blinkt, piept und schickt
„Quittiert" zurück. Die gesamte Poll-/Diff-/Silence-Intelligenz liegt in ioBroker.

```
Grafana ──poll/silence──► ioBroker ──MQTT──► ESP32-Button (dieses Repo)
                          "das Gehirn"   :1884   "dummer" Anzeiger
```

Empfänger-Seite (ioBroker-Orchestrator + Signaltower + MQTT-Publish) ist fertig + live;
siehe Repo [iobroker-scripts](https://github.com/McCavity/iobroker-scripts) (alarm-*),
Vertrag + Design im privaten KI-OS-Vault (`04-projects/alarm-button/`).

## Hardware

**Axiometa Genesis Mini** (ESP32-S3, 4 MB Flash, 2 MB PSRAM), 4 AX22-Module:
Tactile LED Button (Ack + Status-LED) · Rotary Encoder (Nav + Mute-Long-Press) ·
Passive Buzzer · 0.96" IPS-LCD. Stromversorgung USB-C, kein Akku.

## Architektur

- **`lib/alarmcore/`** — host-testbarer Kern, **keine** Hardware-Abhängigkeit:
  - `contract.h` — Datentypen des MQTT-Vertrags (`list` / `new` / `heartbeat`, Schema 1)
  - `parser.*` — JSON → Structs (defensiv: fail-safe Felder, Parse-Fehler → `valid=false`)
  - `view.*` — reine Sicht-Ableitung (Alarme → LED/Buzzer/Display/Status)
  - `hal.h` — `AlarmButtonHAL`-Interface (abstrakte Events, damit beide HW-Varianten passen)
- **`src/main.cpp`** — ESP32-Entry (WiFi + MQTT + HAL + Render-Loop, **Phase 0b**)
- **`test/`** — Unity-Tests gegen den Core, laufen nativ auf dem Mac
- **`archive/`** — gesicherte Original-Demo-Firmware (Voll-Flash-Dump, **lokal**, nicht
  im Repo: Vendor-Blob mit fremdem Copyright)

## Build & Test

```bash
pio test -e native          # Core host-testen (kein Board nötig)
pio run -e axiometa-mini     # Firmware für den ESP32-S3 bauen
pio run -e axiometa-mini -t upload   # flashen (Board am USB-Datenkabel)
```

## Demo-Firmware zurückspielen

Die ab Werk aufgespielte Demo (Innentemperatur-Anzeige) ist als Voll-Flash-Dump lokal in
`archive/` gesichert (nicht im Repo — Vendor-Blob):

```bash
esptool --port /dev/cu.usbmodem* write_flash 0x0 archive/demo-firmware-full-4MB-2026-06-27.bin
```

## Status

- **Foundation ✓** (2026-06-27): host-testbarer Core (Parser + View, 13 Tests grün),
  Projekt-Skelett, Demo-Firmware archiviert.
- **Phase 0b (nächste, Hardware-Session):** WiFi + MQTT-Client (subscribe
  `alarmbutton/office/{list,new,heartbeat}`) + `AxiometaHAL` (LED/Buzzer/Encoder/LCD) +
  Render-Loop + Ack-Publish → Grafana-Silence. Voraussetzung: Hardware-Umbau
  (Temp-Sensor → Pushbutton) + Datenkabel.
- **Provisioning (Roadmap):** Tasmota-artiger Erst-Setup ohne fest verdrahtete Secrets —
  Board ohne Konfig spannt ein eigenes WLAN auf (Captive-Portal AP), kleines Web-Interface
  zum Hinterlegen von WLAN + WLAN-Security + MQTT-Credentials, Admin-Zugang per Passwort,
  optional API-Key-Erzeugung/-Rotation. Ziel: etwas hübscheres Design als Tasmota bei
  vertretbarem Flash-Footprint (Secrets dann in NVS, nicht im Code).

## Varianten

`axiometa-mini` (aktiv) · `lilygo-honyone` (parkiert — gleicher Core, andere HAL-Impl).
