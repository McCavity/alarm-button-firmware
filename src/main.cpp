#include <Arduino.h>
#include "AxiometaHAL.h"
#include "appcore.h"
#include "contract.h"
#include "MqttLink.h"

using namespace alarmcore;

static AxiometaHAL hal;
static AppCore app;
static MqttLink mqtt;
static Heartbeat lastHb;

static StatusLedMode toStatusLed(LedMode m) {
  switch (m) {
    case LedMode::SOLID:      return StatusLedMode::SOLID;
    case LedMode::BLINK_FAST: return StatusLedMode::BLINK_FAST;
    case LedMode::OFF:
    default:                  return StatusLedMode::OFF;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("alarm-button — 'n'=inject new, 'r'=clear list");
  hal.init();
  mqtt.onList([](const ListPayload& p){ app.setList(p); });
  mqtt.onNew([](const NewPayload& n){ app.onNew(n); });
  mqtt.onHeartbeat([](const Heartbeat& hb){ lastHb = hb; });
  mqtt.begin();
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
    if (c == 'r') { ListPayload empty; empty.valid = true; empty.count = 0; empty.max_severity = ""; app.setList(empty); }
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
