#include "parser.h"
#include <ArduinoJson.h>

namespace alarmcore {

ListPayload parseList(const char* json) {
  ListPayload out;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return out;       // valid stays false
  if (!doc["alarms"].is<JsonArrayConst>()) return out;
  out.schema_version = doc["schema_version"] | 0;
  out.device_id = std::string(doc["device_id"] | "");
  out.count = doc["count"] | 0;
  out.max_severity = std::string(doc["max_severity"] | "");
  for (JsonObjectConst a : doc["alarms"].as<JsonArrayConst>()) {
    Alarm al;
    al.id = std::string(a["id"] | "");
    al.host = std::string(a["host"] | "unknown");         // fail safe
    al.name = std::string(a["name"] | "");
    al.severity = std::string(a["severity"] | "warning"); // fail safe: never silent
    al.summary = std::string(a["summary"] | "");
    al.since = std::string(a["since"] | "");
    out.alarms.push_back(al);
  }
  out.valid = true;
  return out;
}

NewPayload parseNew(const char* json) {
  NewPayload out;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return out;
  if (!doc["count_new"].is<int>()) return out;
  out.count_new = doc["count_new"] | 0;
  out.max_severity = std::string(doc["max_severity"] | "");
  out.valid = true;
  return out;
}

Heartbeat parseHeartbeat(const char* json) {
  Heartbeat out;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return out;
  if (!doc["grafana_ok"].is<bool>()) return out;
  out.grafana_ok = doc["grafana_ok"] | false;
  out.poll_age_s = doc["poll_age_s"].isNull() ? -1 : (doc["poll_age_s"] | -1);
  out.valid = true;
  return out;
}

} // namespace alarmcore
