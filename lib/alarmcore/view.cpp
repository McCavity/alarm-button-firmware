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
  // LED: alarms present -> blink fast (attention); none -> off.
  // (Solid/acked is what the signal tower shows from the ioBroker state; the list carries
  //  no acked field — if the button should mirror that later, extend the contract.)
  v.led = (v.count > 0) ? LedMode::BLINK_FAST : LedMode::OFF;

  // Beep only on a fresh new event with count_new > 0 (anti-spam: 1 beep per burst).
  v.beep = newEvent && newP.valid && newP.count_new > 0;

  // Connection status from the heartbeat.
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
