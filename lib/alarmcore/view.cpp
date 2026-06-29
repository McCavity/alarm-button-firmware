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
  // LED tri-state mirrors computeSignaltower: empty -> off, any unacked -> fast blink,
  // all acked -> solid. (The acked flag is the contract's per-alarm ack state, §3.1.)
  bool anyUnacked = false;
  if (last.valid)
    for (const auto& a : last.alarms)
      if (!a.acked) { anyUnacked = true; break; }
  if (v.count == 0)        v.led = LedMode::OFF;
  else if (anyUnacked)     v.led = LedMode::BLINK_FAST;
  else                     v.led = LedMode::SOLID;

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
