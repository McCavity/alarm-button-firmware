#pragma once
#include "contract.h"

// Reine JSON→Struct-Parser für die drei Downstream-Topics. Defensiv:
// fehlende/ungültige Felder fail-safe (severity → "warning"), Parse-Fehler → valid=false.
namespace alarmcore {

ListPayload parseList(const char* json);
NewPayload  parseNew(const char* json);
Heartbeat   parseHeartbeat(const char* json);

} // namespace alarmcore
