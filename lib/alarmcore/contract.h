#pragma once
#include <string>
#include <vector>

// Datentypen des MQTT-Vertrags (alarmbutton/<device>/{list,new,heartbeat}).
// Spiegelt 04-projects/alarm-button/mqtt-contract.md (Schema-Version 1) im KI-OS-Vault.
namespace alarmcore {

struct Alarm {
  std::string id;        // Grafana-fingerprint (Diff-/ack_one-Schlüssel)
  std::string host;
  std::string name;
  std::string severity;  // "critical" | "warning" | "info"
  std::string summary;
  std::string since;     // ISO 8601 (Offset toleriert, nicht auf Z hardcoden)
};

struct ListPayload {
  bool valid = false;        // false = Parse-Fehler / kein gültiges list-JSON
  int schema_version = 0;
  std::string device_id;
  int count = 0;
  std::string max_severity;  // "" wenn Liste leer
  std::vector<Alarm> alarms;
};

struct NewPayload {
  bool valid = false;
  int count_new = 0;
  std::string max_severity;
};

struct Heartbeat {
  bool valid = false;
  bool grafana_ok = false;
  int poll_age_s = -1;       // -1 = unbekannt/fehlend
};

} // namespace alarmcore
