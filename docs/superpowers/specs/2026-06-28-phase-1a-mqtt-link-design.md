# Phase 1a — MQTT-Naht (WiFi + MQTT live anbinden)

> Erstellt: 2026-06-28
> Projekt: alarm-button-firmware · Counterpart: iobroker-scripts (Orchestrator)
> Status: Design approved (Brainstorm), bereit für Implementierungs-Plan
> Vorlauf: Phase 0a–0c abgeschlossen (Core + HAL + UI-State-Machine, 28 native Tests grün)

## Ziel

Die zwei fertigen Hälften — hardware-verifizierte Firmware (canned list) und live
ioBroker-Orchestrator — über MQTT verbinden. Aus der Demo wird das echte Produkt.

**Bewusste Abgrenzung:** Phase 1a behält das **jetzige Browse-Modell** und `ack_all`.
Es gibt **keinen Core-Umbau** → die 28 nativen Tests bleiben unangetastet grün. Das
Triage-Queue-Redesign ist Phase 1b (eigener Spec, Skizze unten).

## Entscheidungen (Brainstorm 2026-06-28)

| # | Frage | Entscheidung |
|---|---|---|
| 1 | MQTT-Library | **arduino-mqtt** (`256dpi/MQTT`, MQTTClient) — großer Buffer, subscribe QoS 1, saubere API |
| 2 | Ack-Semantik (1a) | **`ack_all`**, list-driven (ack_one + Triage = 1b) |
| 3 | Secrets | **`lib/secrets.h`** (gitignored) + eingecheckte `lib/secrets.h.example`; Captive-Portal später |
| 4 | DEVICE_ID | **`office`** |
| 5 | Broker-Auth | **User/PW, kein TLS**; Port **1884 = TCP-MQTT** (1885 = WebSockets, nicht für den nativen Client) |
| 6 | SNTP | **ja, non-blocking** — Loop läuft weiter, `ts` wird gültig sobald Sync da ist |
| 7 | Buffer | großzügig **≥ 8 KB** |
| 8 | Client-ID | **`alarmbutton-office-<MAC-Suffix>`** — eindeutig, kollidiert nicht mit `btn_*` (ButtonPlus-Wandschalter am selben Broker, andere Topics) |

## Architektur

Der Core (`lib/alarmcore/`) bleibt hardwarefrei und unberührt. Neue Arbeit lebt
ausschließlich auf der Hardware-Seite in `src/`.

```
WiFi ─► MQTTClient (arduino-mqtt) ─► Topic-Routing ─► parse* (Core) ─► AppCore ─► HAL ─► LCD/LED/Buzzer
                       ▲                                                  │
                       └──────────────── ack publish ◄────────────── takeAckRequest()
```

### Komponenten

#### 1. `lib/secrets.h` (gitignored) + `lib/secrets.h.example` (eingecheckt)
Konstanten: `WIFI_SSID`, `WIFI_PASS`, `MQTT_HOST`, `MQTT_PORT` (1884), `MQTT_USER`,
`MQTT_PASS`, `DEVICE_ID` (`"office"`). `.gitignore` um `lib/secrets.h` ergänzen
(die `.example` bleibt getrackt als Vorlage).

#### 2. `src/MqttLink.{h,cpp}` (neu — Hardware-Seite, eigene Datei)
Kapselt WiFi + MQTT, hardware-abhängig (daher in `src/`, nicht im Core). Verantwortung:
- **WiFi:** connect + **nicht-blockierender** Reconnect (State-Maschine über `millis()`,
  kein `while`-Spin).
- **MQTT:** arduino-mqtt `MQTTClient` über `WiFiClient`. Buffer per `begin(size, client)`
  auf **≥ 8 KB** gesetzt. Client-ID `alarmbutton-office-<MAC-Suffix>` (z.B. letzte 3
  MAC-Bytes hex). Connect mit `MQTT_USER`/`MQTT_PASS`. Nicht-blockierender Reconnect mit
  Backoff.
- **Subscribe** (QoS 1): `alarmbutton/office/list`, `…/new`, `…/heartbeat`.
- **Message-Routing:** Topic-Suffix → `parseList`/`parseNew`/`parseHeartbeat` → an
  Callbacks/AppCore. Parse-Fehler (`valid=false`) werden verworfen (defensiv, wie
  ioBroker-Seite).
- **Publish:** `publishAck()` → `alarmbutton/office/ack`.
- **Status nach außen:** `isConnected()` für die Stale-/Verbindungslogik.

Schnittstelle bewusst schmal: `begin()`, `loop()` (treibt WiFi+MQTT-Reconnect+`client.loop()`),
Setter für die drei Payload-Callbacks, `publishAck()`, `isConnected()`, `lastHeartbeatMs()`.

#### 3. SNTP (non-blocking)
`configTime()` einmalig in `begin()` anstoßen (NTP-Server: Router/Gateway oder Pool).
**Nie** blockierend auf Sync warten. `ts` für den Ack:
- Sync vorhanden → echte ISO-8601-UTC-Zeit.
- Sync noch nicht da → Fallback: `ts` weglassen (ioBroker stempelt beim Empfang) oder
  Uptime-basierter Platzhalter. **Gewählt:** Feld weglassen, solange Zeit ungültig — kein
  falscher Timestamp (defensiv).

#### 4. Stale-Watch & Verbindungs-Status
In `main.cpp` pro Loop:
```
stale = !mqtt.isConnected() || (millis() - mqtt.lastHeartbeatMs()) > 45000;
app.onHeartbeat(lastHeartbeat, stale);
```
Mappt auf die vorhandene `Conn::IOBROKER_DOWN`-STATUS-Anzeige ("ioBroker?"). **Keine
`view.h`-Änderung.** 45 s = Contract-Regel (≈3 verpasste Heartbeats). Beim Boot vor dem
ersten Heartbeat gilt stale (zeigt "ioBroker?" bis der erste retained Stand / Heartbeat da ist).

#### 5. `src/main.cpp` (umbauen)
- Canned-`demoList()`-Pfad raus; `MqttLink` instanziieren, Callbacks → `app.setList/onNew/onHeartbeat`.
- `setup()`: `hal.init()`, `mqtt.begin()`. Kein Blockieren auf WiFi.
- `loop()`: `mqtt.loop()`; HAL-Events → AppCore (unverändert); Stale berechnen; `render()` → HAL.
- `takeAckRequest()` → `mqtt.publishAck("ack_all")`.
- Serial-Keys `n`/`r` bleiben als Test-Harness/Fallback (lokaler `new`-Beep / Demo-Liste),
  nützlich wenn der Broker mal weg ist.

#### 6. `platformio.ini`
`256dpi/MQTT` zu `[env:axiometa-mini] lib_deps` ergänzen. `[env:native]` bleibt unberührt
(`build_src_filter = -<*>` schließt `src/` aus → `MqttLink` bricht die Host-Tests nicht).

## Ack-Payload (`alarmbutton/office/ack`, QoS 1, kein retain)
```json
{ "schema_version": 1, "device_id": "office", "ts": "<ISO-8601-UTC, optional>", "action": "ack_all" }
```

## Fehlerbehandlung / Robustheit
- **Defensives Parsen:** wie Core schon tut — `valid=false` → Payload verwerfen, alter
  Zustand bleibt. Unbekannte Felder (z.B. künftiges `acked`) werden ignoriert (verifiziert
  in `parser.cpp`: nur bekannte Felder mit `|`-Default).
- **WiFi/MQTT weg:** nicht-blockierender Reconnect; UI zeigt "ioBroker?" über die Stale-Logik.
- **Retained `list`:** kommt beim (Re-)Subscribe automatisch → frisch gebooteter Button hat
  sofort den Stand.
- **Buffer-Schutz langfristig:** Top-N-Cap nach Severity auf ioBroker-Seite (1b-Backlog)
  begrenzt den Worst Case zusätzlich — 8 KB sind damit komfortabel.

## Verifikation (on-hardware, HIL)
Kein neuer Core-Test (reine Glue). Verifikation gegen den live ioBroker via Serial-Monitor:
1. Boot → WiFi+MQTT connect → retained `list` erscheint auf dem LCD.
2. Nav/Detail/Mute unverändert funktionsfähig.
3. **Selbsttest-Bahn** statt echtem Grafana-Alarm: `0_userdata.0.alerting.sources.test`
   auf ein Test-Alarm-JSON setzen → treibt die komplette Kette (list + `new`-Beep + Telegram)
   durch denselben Orchestrator.
4. Ack-Druck → ioBroker empfängt `alarmbutton/office/ack` → setzt `acked`-Flag →
   Signaltower `fast_blink`→`on` (solid), Rundumleuchte aus, Alarm **bleibt in der Liste**.
5. Stale-Test: ioBroker/Heartbeat stoppen → nach 45 s "ioBroker?".

## Akzeptanzkriterien
- [ ] Board verbindet WiFi + MQTT (eindeutige Client-ID, kein `btn_*`-Konflikt), reconnectet non-blocking.
- [ ] Retained `list` rendert beim Boot; `new` löst genau einen Beep aus (sofern nicht muted).
- [ ] Heartbeat-Ausfall > 45 s → "ioBroker?"; Recovery → zurück zur Liste.
- [ ] Ack-Druck publisht gültiges `ack_all`-JSON; ioBroker setzt `acked`-Flag → Signaltower
      `fast_blink`→solid, Rundumleuchte aus, Alarm bleibt in der Liste.
- [ ] `pio test -e native` weiter 28/28 grün (Core unberührt).
- [ ] Secrets nicht im Repo; `secrets.h.example` als Vorlage vorhanden.

## Offene Cross-Repo-Abhängigkeiten (ioBroker-Seite)

- **Ack-Brücke MQTT → Datenpunkt:** Der Orchestrator hört heute nur den Datenpunkt
  `0_userdata.0.alerting.ack`, **nicht** das MQTT-Topic. Brücke nötig:
  `mqtt.0.alarmbutton.office.ack` (legt der Broker bei Client-Publish automatisch an) →
  `setState('0_userdata.0.alerting.ack', true)`. Ohne diese Brücke kommt der Button-Ack
  nie beim Orchestrator an — Voraussetzung für Akzeptanzkriterium/HIL #4.

---

## Phase 1b — Triage-Queue (Skizze, eigener Spec später)

Nicht Teil dieser Umsetzung. Festgehalten, weil die Brainstorm-Diskussion die Richtung geklärt hat:

- **Modell:** Unacked Alarme sofort als DETAIL (höchste Severity zuerst); kurzer Druck =
  `ack_one` des gezeigten → nächster unacked springt an; erst wenn alle acked → LIST zum
  Durchsehen. Verschwindet ein Alarm aus der `list` → lokal entfernen (Detail offen → Sprung
  zur LIST, sonst Listen-Refresh).
- **acked-Quelle: Contract-Flag `acked`** (nicht lokal-optimistisch). Begründung: Linchpin
  drüben verifiziert — ioBroker behält acked-but-firing Alarme in der Liste (`applyAck` setzt
  nur `acked:true`, entfernt nichts). Flag ist additiv vorbereitet (iobroker-scripts#5,
  schema bleibt 1, 34/34 grün, noch nicht gemergt/deployt). Vorteil: reboot-fest via retained
  list + ButtonPlus-Wandschalter-Ack spiegelt automatisch. Core: `bool acked` in `Alarm`
  ergänzen, Parser liest es, State-Machine leitet "unacked = firing ∧ !acked" ab.
- **Gesten-Mapping:** kurz = `ack_one`; long = `ack_all` (Massen-Event); Verortung von
  `ack_all` (LED-Button-Long vs. Encoder-Long) und mute-Umzug noch offen.
- **ack_one-Payload:** `action:"ack_one"` + `"id":"<fingerprint>"`.
- **LED = getreuer Mini-Signaltower (Tri-State).** Die Button-LED spiegelt die exakte
  Tri-State-Logik des Signaltowers (ioBroker `computeSignaltower` macht es vor):
  **irgendein unacked → `fast_blink` · alle acked → `on` (solid) · leer → `off`**. Das ist die
  präzise Form (nicht nur „solid für gesehen") — ersetzt die heutige Phase-1a-Logik
  [view.cpp:20](../../../lib/alarmcore/view.cpp:20) (`count>0 → BLINK_FAST`), sobald der
  `acked`-Flag im Core ankommt.

### 1b-Backlog
- **Top-N-Cap nach Severity** auf der ioBroker-Listenseite (gegen das echte 200-Alarme-Storm-
  Szenario — sprengt sonst Buffer + Button + Mensch).
- NVS-Persistenz (mute / last list über Reboot) — Phase 2.
