#include "view.h"

namespace alarmcore {

ViewState computeView(const ListPayload& last, const Heartbeat& hb, bool heartbeatStale) {
  ViewState v;
  v.count = last.valid ? last.count : 0;
  v.maxSeverity = last.valid ? last.max_severity : "";
  if (last.valid) {
    for (const auto& a : last.alarms) {
      Row r;
      r.severity = a.severity;
      r.acked = a.acked;
      r.text = a.host;
      if (!a.name.empty()) r.text += " " + a.name;
      v.rows.push_back(r);
      if (a.severity == "critical") v.critCount++;
      else if (a.severity == "warning") v.warnCount++;
    }
    v.omitted = last.omitted;
    v.total = (int)last.alarms.size() + last.omitted;
  }
  // LED tri-state mirrors computeSignaltower: empty -> off, any unacked -> fast blink,
  // all acked -> solid. (The acked flag is the contract's per-alarm ack state, §3.1.)
  // Gate on the TOTAL set (visible alarms[] + omitted): counts visible unacked, plus the
  // server's honest omitted_unacked count. Never gate on the loose count field.
  bool anyUnacked = false;
  if (last.valid)
    for (const auto& a : last.alarms)
      if (!a.acked) { anyUnacked = true; break; }
  bool hiddenUnacked = last.valid && last.omitted_unacked > 0;
  int totalAlarms = (int)last.alarms.size() + (last.valid ? last.omitted : 0);
  if (totalAlarms <= 0)                v.led = LedMode::OFF;
  else if (anyUnacked || hiddenUnacked) v.led = LedMode::BLINK_FAST;
  else                                  v.led = LedMode::SOLID;

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
