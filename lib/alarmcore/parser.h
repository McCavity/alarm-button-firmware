#pragma once
#include "contract.h"

// Pure JSON->struct parsers for the three downstream topics. Defensive:
// missing/invalid fields fail safe (severity -> "warning"), parse error -> valid=false.
namespace alarmcore {

ListPayload parseList(const char* json);
NewPayload  parseNew(const char* json);
Heartbeat   parseHeartbeat(const char* json);

} // namespace alarmcore
