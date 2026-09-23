#pragma once
#include "contract.h"
#include <string>
#include <vector>

// Pure view derivation: from the latest list + new event + heartbeat, decide what the
// LED / buzzer / display should show. NO hardware dependency (host-testable). The "dumb"
// button only renders; the diff/escalation intelligence lives in ioBroker (contract).
namespace alarmcore {

enum class LedMode { OFF, BLINK_FAST, SOLID };
enum class Conn { OK, GRAFANA_DOWN, IOBROKER_DOWN };

struct Row {
  std::string severity;   // "critical" | "warning" | "info"
  bool acked = false;
  std::string text;       // "host name"
};

struct ViewState {
  LedMode led = LedMode::OFF;
  int count = 0;
  std::string maxSeverity;
  std::vector<Row> rows;    // structured alarm rows
  int critCount = 0;
  int warnCount = 0;
  int omitted = 0;
  int total = 0;            // alarms.size() + omitted
  Conn conn = Conn::OK;
  std::string statusText;   // "OK" | "Grafana?" | "ioBroker?"
};

// heartbeatStale=true -> ioBroker connection dead (no heartbeat past the stale threshold;
// cf. contract heartbeat: >45 s ~ 3 missed). Sound lives in AppCore (it needs a clock).
ViewState computeView(const ListPayload& last, const Heartbeat& hb, bool heartbeatStale);

} // namespace alarmcore
