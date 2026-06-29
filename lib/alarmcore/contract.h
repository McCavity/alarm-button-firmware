#pragma once
#include <string>
#include <vector>

// Data types of the MQTT contract (alarmbutton/<device>/{list,new,heartbeat}).
// Mirrors 04-projects/alarm-button/mqtt-contract.md (schema version 1) in the KI-OS vault.
namespace alarmcore {

struct Alarm {
  std::string id;        // Grafana fingerprint (diff / ack_one key)
  std::string host;
  std::string name;
  std::string severity;  // "critical" | "warning" | "info"
  std::string summary;
  std::string since;     // ISO 8601 (offset tolerated, do not hardcode to Z)
  bool acked = false;    // per-alarm acknowledge state (contract §3.1, additive). ioBroker is truth.
};

struct ListPayload {
  bool valid = false;        // false = parse error / not a valid list payload
  int schema_version = 0;
  std::string device_id;
  int count = 0;
  std::string max_severity;  // "" when the list is empty
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
  int poll_age_s = -1;       // -1 = unknown / missing
};

} // namespace alarmcore
