#include <Arduino.h>

// Axiometa-Mini firmware entry. Phase 0b (next, hardware session): WiFi + MQTT client
// (subscribe alarmbutton/office/{list,new,heartbeat}) + AxiometaHAL + render loop over the
// host-tested core (lib/alarmcore: parser + view). Intentionally empty for now.
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("alarm-button-firmware — boot (phase 0b follows)");
}

void loop() {
  delay(1000);
}
