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

struct ViewState {
  LedMode led = LedMode::OFF;
  bool beep = false;                 // one-shot beep this cycle (from a new event)
  int count = 0;
  std::string maxSeverity;
  std::vector<std::string> lines;    // "host name" per alarm
  Conn conn = Conn::OK;
  std::string statusText;            // "OK" | "Grafana?" | "ioBroker?"
};

// newEvent=true + valid newP -> beep. heartbeatStale=true -> ioBroker connection dead
// (no heartbeat past the stale threshold; cf. contract heartbeat: >45 s ~ 3 missed).
ViewState computeView(const ListPayload& last, bool newEvent, const NewPayload& newP,
                      const Heartbeat& hb, bool heartbeatStale);

} // namespace alarmcore
