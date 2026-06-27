#pragma once
#include "contract.h"
#include <string>
#include <vector>

// Reine Sicht-Ableitung: aus letzter list + new-Event + heartbeat → was LED/Buzzer/Display
// zeigen sollen. KEINE Hardware-Abhängigkeit (host-testbar). Der "dumme" Knopf rendert nur;
// die Diff-/Eskalations-Intelligenz liegt in ioBroker (Contract §0).
namespace alarmcore {

enum class LedMode { OFF, BLINK_FAST, SOLID };
enum class Conn { OK, GRAFANA_DOWN, IOBROKER_DOWN };

struct ViewState {
  LedMode led = LedMode::OFF;
  bool beep = false;                 // einmaliger Beep diesen Zyklus (aus new-Event)
  int count = 0;
  std::string maxSeverity;
  std::vector<std::string> lines;    // "host name" pro Alarm
  Conn conn = Conn::OK;
  std::string statusText;            // "OK" | "Grafana?" | "ioBroker?"
};

// newEvent=true + gültiges newP → Beep. heartbeatStale=true → ioBroker-Verbindung tot
// (kein heartbeat seit Stale-Schwelle, vgl. Contract §3.3: >45 s ≈ 3 verpasste).
ViewState computeView(const ListPayload& last, bool newEvent, const NewPayload& newP,
                      const Heartbeat& hb, bool heartbeatStale);

} // namespace alarmcore
