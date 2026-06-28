# Phase 1a — MQTT-Link Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect the hardware-verified firmware to the live ioBroker orchestrator over MQTT, replacing the canned alarm list with real `list`/`new`/`heartbeat` payloads and publishing `ack_all`.

**Architecture:** The hardware-free core (`lib/alarmcore/`) stays untouched (28 native tests remain green). All new code lives on the hardware side: a focused `MqttLink` class (`src/`) owns WiFi + MQTT + non-blocking SNTP and routes incoming payloads through the existing `parse*` into `AppCore`; `main.cpp` wires it in and maps connection/heartbeat state to the existing STATUS view.

**Tech Stack:** ESP32-S3 (Arduino framework, PlatformIO), `256dpi/MQTT` (arduino-mqtt), `bblanchon/ArduinoJson@^7.4.1` (already present), `WiFi` + `time.h` (SNTP).

## Global Constraints

- **Core stays hardware-free and unchanged:** no edits under `lib/alarmcore/`; `pio test -e native` must stay 28/28 green after every task.
- **MQTT library:** `256dpi/MQTT` (arduino-mqtt `MQTTClient`).
- **Buffer:** MQTT client buffer ≥ 8 KB (`MQTTClient client(8192)`).
- **Broker:** host/port from secrets, **port 1884 = TCP-MQTT** (never 1885 = WebSockets). Auth: username + password, no TLS.
- **DEVICE_ID:** `office` → topics `alarmbutton/office/{list,new,heartbeat,ack}`.
- **Client-ID:** `alarmbutton-office-<MAC-suffix>` — must never collide with `btn_*` (ButtonPlus wall switches share the broker).
- **SNTP:** non-blocking — never spin-wait on time sync; `ts` is omitted from the ack while time is invalid.
- **Ack semantics (1a):** `ack_all` only (ack_one/triage is Phase 1b).
- **Secrets:** `lib/secrets.h` is gitignored; `lib/secrets.h.example` is committed as the template.
- **Commits:** English `type(scope): …` headers; end with the Co-Authored-By trailer.

## File Structure

- Create `lib/secrets.h.example` — committed template (placeholder values).
- Create `lib/secrets.h` — real credentials, gitignored (not committed).
- Modify `.gitignore` — ignore `lib/secrets.h`.
- Modify `platformio.ini` — add `256dpi/MQTT` to `[env:axiometa-mini] lib_deps`.
- Create `src/MqttLink.h` / `src/MqttLink.cpp` — WiFi + MQTT + SNTP + routing + ack publish.
- Modify `src/main.cpp` — instantiate `MqttLink`, replace canned list, stale-watch, ack publish, keep serial keys.

**Note on verification:** This phase is hardware glue with no new host-testable logic, so tasks are verified by (a) `pio run -e axiometa-mini` compiling clean, (b) `pio test -e native` staying 28/28, and (c) human-in-the-loop (HIL) observation over the serial monitor against the live ioBroker. HIL steps require the board on a USB **data** cable (`/dev/cu.usbmodem*`). The selftest lane `0_userdata.0.alerting.sources.test` can drive the whole ioBroker chain without a real Grafana alarm.

---

### Task 1: Scaffolding — secrets template, gitignore, MQTT dependency

**Files:**
- Create: `lib/secrets.h.example`
- Create: `lib/secrets.h` (local only — fill with real values, gitignored)
- Modify: `.gitignore`
- Modify: `platformio.ini:27-29` (`[env:axiometa-mini] lib_deps`)

**Interfaces:**
- Produces: `secrets.h` macros — `WIFI_SSID`, `WIFI_PASS`, `MQTT_HOST`, `MQTT_PORT`, `MQTT_USER`, `MQTT_PASS`, `DEVICE_ID` (all `#define`, strings except `MQTT_PORT` int).

- [ ] **Step 1: Create the committed template `lib/secrets.h.example`**

```cpp
#pragma once
// Copy to lib/secrets.h (gitignored) and fill in. DO NOT commit lib/secrets.h.
#define WIFI_SSID  "your-ssid"
#define WIFI_PASS  "your-wifi-password"
#define MQTT_HOST  "192.168.x.x"   // ButtonPlus broker
#define MQTT_PORT  1884             // TCP-MQTT (NOT 1885 = WebSockets)
#define MQTT_USER  "your-mqtt-user"
#define MQTT_PASS  "your-mqtt-password"
#define DEVICE_ID  "office"
```

- [ ] **Step 2: Create the real `lib/secrets.h`**

Copy the template to `lib/secrets.h` and fill in the real homelab credentials (ask the user for the broker IP / MQTT user+password if unknown). Keep `DEVICE_ID "office"`.

- [ ] **Step 3: Gitignore the real secrets file**

Append to `.gitignore`:

```
# Local credentials — never commit (template is lib/secrets.h.example)
lib/secrets.h
```

- [ ] **Step 4: Add the MQTT library dependency**

In `platformio.ini`, under `[env:axiometa-mini]`, extend `lib_deps`:

```ini
lib_deps =
  bblanchon/ArduinoJson@^7.4.1
  adafruit/Adafruit ST7735 and ST7789 Library@^1.11.0
  256dpi/MQTT@^2.5.2
```

- [ ] **Step 5: Verify the build still compiles and secrets are ignored**

Run: `pio run -e axiometa-mini`
Expected: SUCCESS (main.cpp unchanged; new lib downloaded).

Run: `git status --porcelain lib/secrets.h`
Expected: empty output (file is ignored, not shown as untracked).

Run: `pio test -e native`
Expected: 28/28 PASS.

- [ ] **Step 6: Commit**

```bash
git add lib/secrets.h.example .gitignore platformio.ini
git commit -m "$(cat <<'EOF'
build(mqtt): add arduino-mqtt dep + gitignored secrets scaffolding

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: MqttLink — WiFi + MQTT connect/reconnect (non-blocking) + SNTP

**Files:**
- Create: `src/MqttLink.h`
- Create: `src/MqttLink.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `secrets.h` macros (Task 1); `alarmcore::ListPayload/NewPayload/Heartbeat` (`lib/alarmcore/contract.h`).
- Produces (full `MqttLink` public API — later tasks rely on these exact signatures):
  ```cpp
  class MqttLink {
  public:
    using ListCb      = std::function<void(const alarmcore::ListPayload&)>;
    using NewCb       = std::function<void(const alarmcore::NewPayload&)>;
    using HeartbeatCb = std::function<void(const alarmcore::Heartbeat&)>;
    void begin();                          // WiFi + MQTT + SNTP kickoff (non-blocking)
    void loop();                           // pump reconnect + client.loop()
    bool isConnected();                    // MQTT session up
    unsigned long lastHeartbeatMs();       // millis() of last heartbeat msg; 0 if none yet
    void onList(ListCb cb);                // Task 3
    void onNew(NewCb cb);                  // Task 3
    void onHeartbeat(HeartbeatCb cb);      // Task 3
    void publishAck(const char* action);  // Task 5
  };
  ```
  This task implements `begin/loop/isConnected/lastHeartbeatMs` and the connection state machine; the callback setters and `publishAck` are declared now but bodies are filled in Tasks 3/5.

- [ ] **Step 1: Create `src/MqttLink.h`**

```cpp
#pragma once
#include <WiFi.h>
#include <MQTT.h>
#include <functional>
#include "contract.h"

// Hardware-side WiFi + MQTT link for the alarm button. Owns the connection,
// routes incoming payloads through the core parsers into callbacks, and
// publishes acks. The core (lib/alarmcore) stays hardware-free; this glue lives in src/.
class MqttLink {
public:
  using ListCb      = std::function<void(const alarmcore::ListPayload&)>;
  using NewCb       = std::function<void(const alarmcore::NewPayload&)>;
  using HeartbeatCb = std::function<void(const alarmcore::Heartbeat&)>;

  void begin();
  void loop();
  bool isConnected() { return client_.connected(); }
  unsigned long lastHeartbeatMs() const { return lastHeartbeatMs_; }

  void onList(ListCb cb)           { listCb_ = cb; }
  void onNew(NewCb cb)             { newCb_ = cb; }
  void onHeartbeat(HeartbeatCb cb) { hbCb_ = cb; }

  void publishAck(const char* action);

private:
  void ensureWifi();
  void ensureMqtt();
  void subscribeTopics();
  void handleMessage(String& topic, String& payload);
  void buildClientId();
  bool isoTimeUtc(char* out, size_t n);   // false if SNTP not synced yet

  static void onMqttMessage(String& topic, String& payload);  // trampoline
  static MqttLink* instance_;

  WiFiClient net_;
  MQTTClient client_{8192};               // >= 8 KB buffer (worst-case list)
  String clientId_;
  unsigned long lastReconnectMs_ = 0;
  unsigned long lastHeartbeatMs_ = 0;

  ListCb listCb_;
  NewCb  newCb_;
  HeartbeatCb hbCb_;
};
```

- [ ] **Step 2: Create `src/MqttLink.cpp` (connection logic + stubs for routing/ack)**

```cpp
#include "MqttLink.h"
#include "parser.h"
#include "secrets.h"
#include <time.h>

MqttLink* MqttLink::instance_ = nullptr;

void MqttLink::buildClientId() {
  uint64_t mac = ESP.getEfuseMac();           // unique per board
  uint8_t a = (mac >> 16) & 0xFF, b = (mac >> 8) & 0xFF, c = mac & 0xFF;
  char buf[40];
  snprintf(buf, sizeof(buf), "alarmbutton-%s-%02X%02X%02X", DEVICE_ID, a, b, c);
  clientId_ = buf;                             // never collides with btn_*
}

void MqttLink::begin() {
  instance_ = this;
  buildClientId();
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);            // non-blocking; loop() drives the rest
  configTime(0, 0, "pool.ntp.org");            // SNTP, UTC; never wait on it
  client_.begin(MQTT_HOST, MQTT_PORT, net_);
  client_.onMessage(onMqttMessage);
  Serial.printf("[mqtt] client id %s\n", clientId_.c_str());
}

void MqttLink::ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  // WiFi.begin already called in begin(); the ESP auto-retries. Nothing to block on.
}

void MqttLink::ensureMqtt() {
  if (client_.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastReconnectMs_ < 2000) return;   // backoff: retry every 2 s
  lastReconnectMs_ = now;
  Serial.print("[mqtt] connecting... ");
  if (client_.connect(clientId_.c_str(), MQTT_USER, MQTT_PASS)) {
    Serial.println("ok");
    subscribeTopics();
  } else {
    Serial.printf("failed (rc=%d)\n", client_.lastError());
  }
}

void MqttLink::subscribeTopics() {
  client_.subscribe("alarmbutton/" DEVICE_ID "/list", 1);
  client_.subscribe("alarmbutton/" DEVICE_ID "/new", 1);
  client_.subscribe("alarmbutton/" DEVICE_ID "/heartbeat", 1);
}

void MqttLink::loop() {
  ensureWifi();
  ensureMqtt();
  client_.loop();
}

void MqttLink::onMqttMessage(String& topic, String& payload) {
  if (instance_) instance_->handleMessage(topic, payload);
}

// Filled in Task 3:
void MqttLink::handleMessage(String& topic, String& payload) { (void)topic; (void)payload; }

// Filled in Task 5:
bool MqttLink::isoTimeUtc(char* out, size_t n) { (void)out; (void)n; return false; }
void MqttLink::publishAck(const char* action) { (void)action; }
```

- [ ] **Step 3: Minimal wiring in `src/main.cpp` to run MqttLink on hardware**

Add the include and a global instance, and call `begin()`/`loop()`. Leave the canned demo list in place for now (Task 3 replaces it). At the top:

```cpp
#include "MqttLink.h"
```
After `static AppCore app;`:
```cpp
static MqttLink mqtt;
```
In `setup()` after `hal.init();`:
```cpp
mqtt.begin();
```
At the **start** of `loop()` (before `hal.tick();`):
```cpp
mqtt.loop();
```

- [ ] **Step 4: Verify build + native suite**

Run: `pio run -e axiometa-mini`
Expected: SUCCESS.

Run: `pio test -e native`
Expected: 28/28 PASS (core untouched; `src/` excluded from native).

- [ ] **Step 5: HIL — flash and confirm WiFi+MQTT connect**

Run: `pio run -e axiometa-mini -t upload && pio device monitor -e axiometa-mini`
Expected serial: `[mqtt] client id alarmbutton-office-XXXXXX` then `[mqtt] connecting... ok`.
Confirm reconnect: briefly stop the broker (or unplug LAN) → serial shows `failed`/retry every ~2 s → recovers to `ok` without a reboot and without blocking the UI (LCD still updates from the canned list).

- [ ] **Step 6: Commit**

```bash
git add src/MqttLink.h src/MqttLink.cpp src/main.cpp
git commit -m "$(cat <<'EOF'
feat(net): MqttLink WiFi+MQTT connect/reconnect (non-blocking) + SNTP

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Subscribe routing — parse payloads into AppCore (replace canned list)

**Files:**
- Modify: `src/MqttLink.cpp` (`handleMessage`)
- Modify: `src/main.cpp` (register callbacks, drop canned list)

**Interfaces:**
- Consumes: `MqttLink` callback setters + `handleMessage` (Task 2); `alarmcore::parseList/parseNew/parseHeartbeat` (`lib/alarmcore/parser.h`); `AppCore::setList/onNew` (`lib/alarmcore/appcore.h`).
- Produces: incoming `list`/`new`/`heartbeat` topics drive `AppCore`; `lastHeartbeatMs_` updated on each heartbeat.

- [ ] **Step 1: Implement `handleMessage` routing in `src/MqttLink.cpp`**

Replace the Task-2 stub:

```cpp
void MqttLink::handleMessage(String& topic, String& payload) {
  const char* json = payload.c_str();
  if (topic.endsWith("/list")) {
    alarmcore::ListPayload p = alarmcore::parseList(json);
    if (p.valid && listCb_) listCb_(p);             // invalid -> drop, keep last (defensive)
  } else if (topic.endsWith("/new")) {
    alarmcore::NewPayload n = alarmcore::parseNew(json);
    if (n.valid && newCb_) newCb_(n);
  } else if (topic.endsWith("/heartbeat")) {
    alarmcore::Heartbeat hb = alarmcore::parseHeartbeat(json);
    if (hb.valid) {
      lastHeartbeatMs_ = millis();
      if (hbCb_) hbCb_(hb);
    }
  }
}
```

- [ ] **Step 2: Register callbacks and remove the canned list in `src/main.cpp`**

Remove the `demoList()` function and its `setup()` usage. In `setup()`, replace the canned `app.setList(demoList())` + canned heartbeat block with callback registration (keep `mqtt.begin()`):

```cpp
  mqtt.onList([](const ListPayload& p){ app.setList(p); });
  mqtt.onNew([](const NewPayload& n){ app.onNew(n); });
  mqtt.onHeartbeat([](const Heartbeat& hb){ lastHb = hb; });
  mqtt.begin();
```
Add a file-scope `static Heartbeat lastHb;` near the globals (used by the stale-watch in Task 4). Keep the serial-key handler, but change `'r'` to no longer reference `demoList()` — instead make `'r'` clear to an empty list for local testing:

```cpp
    if (c == 'r') { ListPayload empty; empty.valid = true; empty.count = 0; empty.max_severity = ""; app.setList(empty); }
```
Remove the demo `takeAckRequest()` block that injected an empty list (lines 63-66) — ack no longer mutates the local list (Task 5 publishes instead).

- [ ] **Step 3: Verify build + native suite**

Run: `pio run -e axiometa-mini`
Expected: SUCCESS.

Run: `pio test -e native`
Expected: 28/28 PASS.

- [ ] **Step 4: HIL — confirm retained list renders + new triggers a beep**

Run: `pio run -e axiometa-mini -t upload && pio device monitor -e axiometa-mini`
Expected: on connect, the **retained** `list` from ioBroker renders on the LCD (real hosts, not the canned `nutapp01/nasapp01/dump1090`).
Drive the selftest lane: set `0_userdata.0.alerting.sources.test` to a test-alarm JSON (via the iobroker MCP or admin) → the orchestrator publishes `list` + `new` → LCD list updates and a single short beep fires (if not muted).

- [ ] **Step 5: Commit**

```bash
git add src/MqttLink.cpp src/main.cpp
git commit -m "$(cat <<'EOF'
feat(net): route MQTT list/new/heartbeat into AppCore, drop canned list

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: Stale-watch — map connection/heartbeat loss to the STATUS view

**Files:**
- Modify: `src/main.cpp` (`loop()`)

**Interfaces:**
- Consumes: `MqttLink::isConnected()`, `MqttLink::lastHeartbeatMs()` (Task 2); `AppCore::onHeartbeat(const Heartbeat&, bool stale)` (`lib/alarmcore/appcore.h`); the `static Heartbeat lastHb` from Task 3.
- Produces: each frame feeds AppCore a freshly-computed `stale` flag; stale maps to the existing `Conn::IOBROKER_DOWN` STATUS ("ioBroker?"). No `view.h` change.

- [ ] **Step 1: Compute stale each loop and feed AppCore**

In `src/main.cpp` `loop()`, after `mqtt.loop();` and before the render block, add:

```cpp
  // Stale = MQTT down, OR heartbeats were flowing and then stopped (>45 s, contract rule).
  // Before the first heartbeat (lastHeartbeatMs()==0) while connected we are NOT stale —
  // a freshly connected button shows the retained list, not "ioBroker?".
  bool stale = !mqtt.isConnected() ||
               (mqtt.lastHeartbeatMs() != 0 && millis() - mqtt.lastHeartbeatMs() > 45000UL);
  app.onHeartbeat(lastHb, stale);
```

- [ ] **Step 2: Verify build + native suite**

Run: `pio run -e axiometa-mini`
Expected: SUCCESS.

Run: `pio test -e native`
Expected: 28/28 PASS.

- [ ] **Step 3: HIL — confirm stale and recovery**

Flash + monitor. With heartbeats flowing, the LCD shows the list. Stop the ioBroker heartbeat (pause the orchestrator or the broker) → within ~45 s the LCD switches to STATUS "ioBroker?". Restore → next heartbeat clears stale and the list returns.
Also confirm: pulling MQTT (disconnect) flips to "ioBroker?" immediately (not after 45 s).

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "$(cat <<'EOF'
feat(net): stale-watch maps MQTT/heartbeat loss to STATUS view

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: Ack publish — `ack_all` on button press (+ non-blocking SNTP timestamp)

**Files:**
- Modify: `src/MqttLink.cpp` (`isoTimeUtc`, `publishAck`)
- Modify: `src/main.cpp` (`loop()` ack wiring)

**Interfaces:**
- Consumes: `MqttLink::publishAck(const char* action)` (declared Task 2); `AppCore::takeAckRequest()` (`lib/alarmcore/appcore.h`).
- Produces: button ack publishes `alarmbutton/office/ack` with a valid JSON payload.

- [ ] **Step 1: Implement `isoTimeUtc` + `publishAck` in `src/MqttLink.cpp`**

Replace the Task-2 stubs:

```cpp
bool MqttLink::isoTimeUtc(char* out, size_t n) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;            // SNTP not synced yet (before 2023-11)
  struct tm tm_utc;
  gmtime_r(&now, &tm_utc);
  strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return true;
}

void MqttLink::publishAck(const char* action) {
  char ts[32];
  char payload[160];
  if (isoTimeUtc(ts, sizeof(ts))) {
    snprintf(payload, sizeof(payload),
      "{\"schema_version\":1,\"device_id\":\"%s\",\"ts\":\"%s\",\"action\":\"%s\"}",
      DEVICE_ID, ts, action);
  } else {
    // SNTP not ready: omit ts rather than send a wrong one (ioBroker stamps on receipt).
    snprintf(payload, sizeof(payload),
      "{\"schema_version\":1,\"device_id\":\"%s\",\"action\":\"%s\"}",
      DEVICE_ID, action);
  }
  client_.publish("alarmbutton/" DEVICE_ID "/ack", payload, false, 1);  // no retain, QoS 1
  Serial.printf("[mqtt] ack -> %s\n", payload);
}
```

- [ ] **Step 2: Wire the ack in `src/main.cpp` `loop()`**

The existing render loop already calls `app.acknowledge()` on `hal.acknowledgePressed()`. Replace the (now-removed) demo `takeAckRequest()` block with a publish:

```cpp
  if (app.takeAckRequest()) mqtt.publishAck("ack_all");
```

- [ ] **Step 3: Verify build + native suite**

Run: `pio run -e axiometa-mini`
Expected: SUCCESS.

Run: `pio test -e native`
Expected: 28/28 PASS.

- [ ] **Step 4: HIL — confirm ack end-to-end**

Flash + monitor. With a live alarm in the list, press the LED button → serial shows `[mqtt] ack -> {"schema_version":1,"device_id":"office",...,"action":"ack_all"}`.
Confirm on the ioBroker side (requires the cross-repo ack-bridge — see spec "Offene Cross-Repo-Abhängigkeiten"): `mqtt.0.alarmbutton.office.ack` receives the payload → orchestrator sets `acked` → signaltower `fast_blink`→solid, Rundumleuchte off, **alarm stays in the list**.

- [ ] **Step 5: Commit**

```bash
git add src/MqttLink.cpp src/main.cpp
git commit -m "$(cat <<'EOF'
feat(net): publish ack_all on button press with non-blocking SNTP ts

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

---

## Final verification (whole phase)

- [ ] `pio test -e native` → 28/28 (core untouched).
- [ ] `pio run -e axiometa-mini` → clean build.
- [ ] HIL acceptance run against live ioBroker:
  - [ ] Boot → unique client id (no `btn_*` clash) → WiFi+MQTT connect → retained list renders.
  - [ ] `new` (via `sources.test`) → exactly one beep (unless muted).
  - [ ] Heartbeat loss > 45 s → "ioBroker?"; recovery → list returns; MQTT disconnect → immediate "ioBroker?".
  - [ ] Button press → valid `ack_all` JSON published; (with bridge) signaltower goes solid, alarm stays listed.
  - [ ] `git status` clean of `lib/secrets.h` (ignored); `lib/secrets.h.example` present.
