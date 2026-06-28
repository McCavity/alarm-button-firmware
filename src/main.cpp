#include <Arduino.h>
#include "AxiometaHAL.h"
#include "appcore.h"
#include "contract.h"
#include "MqttLink.h"

using namespace alarmcore;

// Phase 0c demo: a canned alarm list drives the full HAL + AppCore loop on real hardware.
// No MQTT yet. Serial keys: 'n' inject a "new" event (one beep), 'r' restore the demo list.
static AxiometaHAL hal;
static AppCore app;
static MqttLink mqtt;

static StatusLedMode toStatusLed(LedMode m) {
  switch (m) {
    case LedMode::SOLID:      return StatusLedMode::SOLID;
    case LedMode::BLINK_FAST: return StatusLedMode::BLINK_FAST;
    case LedMode::OFF:
    default:                  return StatusLedMode::OFF;
  }
}

static ListPayload demoList() {
  ListPayload p; p.valid = true; p.count = 3; p.max_severity = "critical";
  auto add = [&](const char* id, const char* host, const char* name,
                 const char* sev, const char* summary) {
    Alarm a; a.id = id; a.host = host; a.name = name; a.severity = sev;
    a.summary = summary; a.since = "2026-06-27T10:00:00Z";
    p.alarms.push_back(a);
  };
  add("a1", "nutapp01", "USV auf Batterie", "critical",
      "nutapp01: USV laeuft auf Batterie - Stromausfall oder Training");
  add("b2", "nasapp01", "Disk Health", "warning",
      "nasapp01: S.M.A.R.T.-Status einer Platte verschlechtert");
  add("c3", "dump1090", "dump1090-fa down", "critical",
      "dump1090-fa service is not responding");
  return p;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("alarm-button phase 0c demo — 'n'=new beep, 'r'=restore list");
  hal.init();
  mqtt.begin();
  app.setList(demoList());
  Heartbeat hb; hb.valid = true; hb.grafana_ok = true; hb.poll_age_s = 2;
  app.onHeartbeat(hb, false);
}

void loop() {
  mqtt.loop();
  hal.tick();

  if (hal.acknowledgePressed())  { app.acknowledge();  Serial.println("ACK"); }
  int d = hal.navDelta();        if (d) app.nav(d);
  if (hal.detailTogglePressed()) { app.toggleDetail(); Serial.println("DETAIL toggle"); }
  if (hal.muteTogglePressed())   { app.toggleMute();   Serial.printf("MUTE=%d\n", app.muted()); }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'n') { NewPayload n; n.valid = true; n.count_new = 1; n.max_severity = "critical"; app.onNew(n); }
    if (c == 'r') { app.setList(demoList()); }
  }

  if (app.takeAckRequest()) {            // demo: ack clears the list
    ListPayload empty; empty.valid = true; empty.count = 0; empty.max_severity = "";
    app.setList(empty);
  }

  RenderModel m = app.render();
  hal.setStatusLed(toStatusLed(m.led));
  if (m.beep) hal.playAlertSound(AlertSound::SHORT_BEEP);
  switch (m.screen) {
    case Screen::LIST:   hal.showAlarmList(m.lines, m.selectedIdx, m.maxSeverity); break;
    case Screen::DETAIL: hal.showAlarmDetail(m.detailText);                        break;
    case Screen::STATUS: hal.showStatus(m.statusText);                            break;
  }
  delay(2);   // tight loop: sample the push often so quick taps aren't missed during a redraw
}
