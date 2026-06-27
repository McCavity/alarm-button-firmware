#include "view.h"

namespace alarmcore {

ViewState computeView(const ListPayload& last, bool newEvent, const NewPayload& newP,
                      const Heartbeat& hb, bool heartbeatStale) {
  ViewState v;
  v.count = last.valid ? last.count : 0;
  v.maxSeverity = last.valid ? last.max_severity : "";
  if (last.valid) {
    for (const auto& a : last.alarms) {
      std::string line = a.host;
      if (!a.name.empty()) line += " " + a.name;
      v.lines.push_back(line);
    }
  }
  // LED: Alarme vorhanden → schnell blinken (Aufmerksamkeit); keine → aus.
  // (Solid/acked unterscheidet der Signaltower aus dem ioBroker-state; die list trägt
  //  kein acked-Feld — falls der Knopf das später spiegeln soll, Contract erweitern.)
  v.led = (v.count > 0) ? LedMode::BLINK_FAST : LedMode::OFF;

  // Beep nur bei frischem new-Event mit count_new > 0 (Anti-Spam: 1 Beep pro Burst).
  v.beep = newEvent && newP.valid && newP.count_new > 0;

  // Verbindungsstatus aus dem heartbeat.
  if (heartbeatStale) {
    v.conn = Conn::IOBROKER_DOWN;
    v.statusText = "ioBroker?";
  } else if (hb.valid && !hb.grafana_ok) {
    v.conn = Conn::GRAFANA_DOWN;
    v.statusText = "Grafana?";
  } else {
    v.conn = Conn::OK;
    v.statusText = "OK";
  }
  return v;
}

} // namespace alarmcore
